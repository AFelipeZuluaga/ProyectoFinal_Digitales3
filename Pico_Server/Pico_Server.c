// Pico_Server.c

/**
 * @file Pico_Server.c
 * @brief Servidor UDP para controlar una mano robótica con la Raspberry Pi Pico W.
 *
 * Recibe tramas desde un guante con sensores Hall vía Wi-Fi (UDP) y actualiza los
 * servomotores de cada dedo usando un PCA9685.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "hardware/timer.h" 

#include "lib/servo/servo.h"

// --- CONFIGURACIÓN WI-FI ---
/** @brief SSID de la red Wi-Fi (hotspot) a la que se conecta la Pico W. */
#define WIFI_SSID     "iPhone de Felipe"
/** @brief Contraseña de la red Wi-Fi (hotspot). */
#define WIFI_PASSWORD "ff11223344"
/** @brief Puerto UDP en el que escucha el servidor de la mano. */
#define UDP_PORT      4242

// --- CONFIGURACIÓN MANO ---
/** @brief Número de dedos controlados por la mano robótica. */
#define NUM_FINGERS         5
/** @brief Valor máximo esperado desde el guante (rango efectivo). */
#define VMAX                9          // Rango máximo efectivo del guante
/** @brief Umbral mínimo del sensor para filtrar ruido. */
#define SENSOR_FLOOR        2          // Offset para ignorar ruido bajo (0-2)
/** @brief Índice de dedo cuyo movimiento debe invertirse por montaje físico. */
#define INVERT_FINGER_INDEX 4          // Ajuste hardware por si un servo está al revés

/** @brief Estructura del controlador PCA9685 usado para los servomotores. */
static servo_pca_t servo_dev;
/** @brief PCB UDP usado como servidor para recibir datos desde el guante. */
static struct udp_pcb *udp_server_pcb = NULL;

// --- VARIABLES COMPARTIDAS (VOLATILES PARA IRQ) ---
/** @brief Indica que se han recibido nuevos datos válidos desde UDP. */
static volatile bool flag_new_data = false;    
/** @brief Valores pendientes por aplicar a los dedos, recibidos desde el guante. */
static volatile int  pending_values[NUM_FINGERS]; 

// --- INTERRUPCIÓN DE TIMER (HEARTBEAT) ---
/**
 * @brief Callback periódico del timer para generar un "heartbeat" con el LED.
 *
 * Parpadea el LED de la Pico W para indicar que el sistema está en ejecución.
 *
 * @param t Puntero al temporizador que generó la interrupción.
 * @return true para mantener el temporizador repitiéndose.
 */
bool heartbeat_timer_callback(struct repeating_timer *t) {
    static bool led_state = false;
    led_state = !led_state;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
    return true;
}

// --- UTILIDADES MATEMÁTICAS ---
/**
 * @brief Limita un valor entero al rango cerrado [a, b].
 *
 * @param x Valor de entrada.
 * @param a Límite inferior.
 * @param b Límite superior.
 * @return Valor de x recortado al intervalo [a, b].
 */
static int clampi(int x, int a, int b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

/**
 * @brief Convierte un valor del guante a un tiempo en microsegundos para el servo.
 *
 * Aplica clamping, un piso mínimo de lectura, normaliza al rango [0, 1] y,
 * opcionalmente, invierte la dirección para un dedo específico.
 *
 * @param finger_index Índice del dedo (0 a NUM_FINGERS-1).
 * @param v Valor crudo recibido del guante para ese dedo.
 * @return Ancho de pulso en microsegundos dentro del rango permitido del servo.
 */
static float value_to_us(int finger_index, int v) {
    // 1. Clamping de seguridad
    v = clampi(v, 0, VMAX);
    
    // 2. Corrección de "Piso" (Offset)
    // Si llega un valor < 2, lo tratamos como 2 para evitar valores negativos en la norma
    int v_adjusted = v;
    if (v_adjusted < SENSOR_FLOOR) v_adjusted = SENSOR_FLOOR;
    
    // 3. Normalización
    float range_span = (float)(VMAX - SENSOR_FLOOR);
    if (range_span < 1.0f) range_span = 1.0f;

    // Calculamos porcentaje de 0.0 a 1.0
    float norm = ((float)v_adjusted - (float)SENSOR_FLOOR) / range_span;
    
    // 4. Inversión Hardware Específica (Solo si un dedo físico está montado al revés)
    if (finger_index == INVERT_FINGER_INDEX) {
        norm = 1.0f - norm;
    }

    return SERVO_US_MIN + norm * (SERVO_US_MAX - SERVO_US_MIN);
}

/**
 * @brief Aplica un vector de valores a todos los dedos de la mano.
 *
 * Recorre los dedos, convierte cada valor a microsegundos y actualiza el PWM
 * correspondiente en el controlador de servos.
 *
 * @param v Arreglo de tamaño NUM_FINGERS con los valores de cada dedo.
 */
static void apply_values_logic(const int v[NUM_FINGERS]) {
    for (int i = 0; i < NUM_FINGERS; i++) {
        float us = value_to_us(i, v[i]);
        servo_set_us(&servo_dev, (uint8_t)i, us);
    }
}

/**
 * @brief Parsea una trama recibida del guante en formato CSV.
 *
 * Se espera una línea con el formato: `H,d0,d1,d2,d3,d4`.
 *
 * @param line Cadena recibida vía UDP.
 * @param out_vals Arreglo de salida (tamaño NUM_FINGERS) con los valores parseados.
 * @return true si la trama es válida y se llenó out_vals, false en caso contrario.
 */
static bool parse_trama(char *line, int out_vals[NUM_FINGERS]) {
    if (line[0] != 'H') return false;
    
    int v[5];
    int read_count = sscanf(line, "H,%d,%d,%d,%d,%d", 
                            &v[0], &v[1], &v[2], &v[3], &v[4]);
    
    if (read_count != 5) return false;

    for(int i=0; i<5; i++) out_vals[i] = v[i];
    return true;
}

// --- CALLBACK UDP (EVENTO) ---
/**
 * @brief Callback de recepción UDP para procesar tramas del guante.
 *
 * Copia la carga útil a un buffer local, intenta parsear la trama y, si es válida,
 * actualiza los valores pendientes y levanta la bandera @ref flag_new_data.
 *
 * @param arg Puntero opcional de usuario (no usado).
 * @param pcb PCB UDP que recibe los datos.
 * @param p Estructura pbuf con la carga útil recibida.
 * @param addr Dirección IP del emisor.
 * @param port Puerto UDP de origen.
 */
static void udp_server_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port) {
    if (!p) return;

    char buffer[64];
    u16_t len = p->len > 63 ? 63 : p->len;
    memcpy(buffer, p->payload, len);
    buffer[len] = '\0';

    int vals[NUM_FINGERS];
    if (parse_trama(buffer, vals)) {
        // Copia atómica a variables compartidas
        for(int i=0; i<NUM_FINGERS; i++) {
            pending_values[i] = vals[i];
        }
        flag_new_data = true; // Notificar al Main
        printf("%s\n", buffer);
    }

    pbuf_free(p);
}

// --- MAIN ---
/**
 * @brief Punto de entrada del servidor de la mano robótica.
 *
 * Inicializa UART/STDIO, Wi-Fi, el controlador de servos, configura el servidor
 * UDP y un timer de heartbeat. En el bucle principal, realiza polling de Wi-Fi
 * y aplica nuevos valores de dedos cuando llegan tramas válidas.
 *
 * @return 0 en funcionamiento normal, 1 en caso de error de inicialización.
 */
int main() {
    stdio_init_all();
    sleep_ms(3000);
    printf("=== SERVER (MANO): Lógica Directa ===\n");

    if (cyw43_arch_init()) return 1;
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 15000)) {
        printf("Fallo WiFi\n");
        return 1;
    }
    printf("IP SERVER: %s\n", ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));

    if (!servo_init(&servo_dev)) printf("Error PCA9685\n");

    udp_server_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    udp_bind(udp_server_pcb, IP_ANY_TYPE, UDP_PORT);
    udp_recv(udp_server_pcb, udp_server_recv, NULL);

    // Timer Heartbeat (Interrupción)
    struct repeating_timer timer;
    add_repeating_timer_ms(500, heartbeat_timer_callback, NULL, &timer);

    // Bucle Principal (Polling)
    while (1) {
        // 1. Polling WiFi
        cyw43_arch_poll();

        // 2. Polling Lógica Aplicación
        if (flag_new_data) {
            flag_new_data = false;
            
            int current_vals[NUM_FINGERS];
            for(int i=0; i<NUM_FINGERS; i++) current_vals[i] = pending_values[i];

            apply_values_logic(current_vals);
        }
    }
}
