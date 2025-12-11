// Pico_Server.c

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"

#include "lib/servo/servo.h"

// --- CONFIGURACIÓN WI-FI (STA en hotspot iOS) ---
#define WIFI_SSID     "iPhone de Felipe"
#define WIFI_PASSWORD "ff11223344"
#define UDP_PORT      4242

// --- CONFIGURACIÓN MANO / SERVOS ---
#define NUM_FINGERS         5
#define VMAX                9          // valores esperados 0..9
#define INVERT_FINGER_INDEX 4          // dedo invertido (si uno gira al revés)
#define SENSOR_FLOOR 2

static servo_pca_t servo_dev;
static struct udp_pcb *udp_server_pcb = NULL;

static uint32_t packet_count = 0;
static uint32_t last_packet_ms = 0;

// --- UTILIDADES ---

static int clampi(int x, int a, int b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

static float value_to_us(int finger_index, int v) {
    // 1. Aseguramos que 'v' no exceda los límites lógicos
    v = clampi(v, 0, VMAX);

    // 2. Aplicamos la corrección de rango (Offset)
    // Si llega un 0, 1 o 2, forzamos a que sea el mínimo.
    int v_adjusted = v;
    if (v_adjusted < SENSOR_FLOOR) {
        v_adjusted = SENSOR_FLOOR;
    }

    // 3. Calculamos la norma sobre el "Rango Efectivo"
    // El rango ya no es 0..9, sino 2..9 (un ancho de 7 pasos)
    // Fórmula: (Valor - Piso) / (Max - Piso)
    float range_span = (float)(VMAX - SENSOR_FLOOR); // 9 - 2 = 7
    
    // Evitamos división por cero por seguridad
    if (range_span < 1.0f) range_span = 1.0f; 

    float norm = ((float)v_adjusted - (float)SENSOR_FLOOR) / range_span;
    // Ahora:
    // Si v=2 -> (2-2)/7 = 0.0 (Cierre total)
    // Si v=9 -> (9-2)/7 = 1.0 (Apertura total)

    // 4. Inversión de lógica (lo que hicimos en el paso anterior)
    // Para que 1.0 sea cerrar o abrir según necesites.
    // Como pediste invertir: 
    norm = 1.0f - norm;

    // 5. Inversión por hardware específico (dedo montado al revés)
    if (finger_index == INVERT_FINGER_INDEX) {
        norm = 1.0f - norm;
    }

    return SERVO_US_MIN + norm * (SERVO_US_MAX - SERVO_US_MIN);
}

static void apply_values(servo_pca_t *dev, const int v[NUM_FINGERS]) {
    if (dev->freq_hz == 0.0f) return;

    for (int i = 0; i < NUM_FINGERS; i++) {
        float us = value_to_us(i, v[i]);
        servo_set_us(dev, (uint8_t)i, us);
    }
}

static bool parse_and_execute(char *line) {
    const char *delims = ", ";
    char *tokens[16];
    int count = 0;
    char buf_copy[128];

    strncpy(buf_copy, line, sizeof(buf_copy) - 1);
    buf_copy[sizeof(buf_copy) - 1] = '\0';

    char *tok = strtok(buf_copy, delims);
    while (tok && count < 16) {
        if (*tok != '\0') {
            tokens[count++] = tok;
        }
        tok = strtok(NULL, delims);
    }

    if (count != 6 || strcmp(tokens[0], "H") != 0) {
        return false;
    }

    int vals[NUM_FINGERS];
    for (int i = 0; i < NUM_FINGERS; i++) {
        char *end = NULL;
        long v = strtol(tokens[i + 1], &end, 10);
        if (!end || *end != '\0') {
            return false;
        }
        vals[i] = clampi((int)v, 0, VMAX);
    }

    apply_values(&servo_dev, vals);

    printf("RX H,%d,%d,%d,%d,%d\n",
           vals[0], vals[1], vals[2], vals[3], vals[4]);
    return true;
}

// --- CALLBACK UDP ---

static void udp_server_recv(void *arg,
                            struct udp_pcb *pcb,
                            struct pbuf *p,
                            const ip_addr_t *addr,
                            u16_t port) {
    if (!p) return;

    char buffer[128];
    u16_t len = p->len > 127 ? 127 : p->len;
    memcpy(buffer, p->payload, len);
    buffer[len] = '\0';

    packet_count++;
    last_packet_ms = to_ms_since_boot(get_absolute_time());

    bool ok = parse_and_execute(buffer);

    printf("[PKT %lu] %s  (ok=%d)\n",
           (unsigned long)packet_count,
           buffer,
           ok ? 1 : 0);

    pbuf_free(p);
}

// --- MAIN ---

int main() {
    stdio_init_all();
    sleep_ms(3000);
    printf("=== SERVER MANO (STA en hotspot iOS, UDP) ===\n");

    if (cyw43_arch_init()) {
        printf("ERROR: cyw43_arch_init\n");
        return 1;
    }

    // Opcional: quitar powersave
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

    cyw43_arch_enable_sta_mode();

    printf("[WIFI] Conectando a SSID=%s ...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(
            WIFI_SSID, WIFI_PASSWORD,
            CYW43_AUTH_WPA2_AES_PSK,
            15000)) {
        printf("[WIFI] No se pudo conectar al hotspot.\n");
        return 1;
    }
    printf("[WIFI] Conectado al hotspot.\n");

    // Imprimir IP que le dio el iPhone al server
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    const ip4_addr_t *ip = netif_ip4_addr(n);
    printf("[WIFI] IP SERVER (mano): %s\n", ip4addr_ntoa(ip));
    printf("   -> Usa esta IP en SERVER_IP del cliente (guante)\n");

    // Iniciar servos
    printf("--> Iniciando I2C para servos...\n");
    bool servos_ok = servo_init(&servo_dev);
    if (!servos_ok) {
        printf("!!! NO PCA9685 detectado, solo Rx UDP !!!\n");
        servo_dev.freq_hz = 0.0f;
    } else {
        int v_init[NUM_FINGERS] = {0, 0, 0, 0, 0};
        apply_values(&servo_dev, v_init);
        printf("--> Servos OK.\n");
    }

    // Crear servidor UDP
    udp_server_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!udp_server_pcb) {
        printf("ERROR: no se pudo crear PCB UDP\n");
        return 1;
    }

    err_t err = udp_bind(udp_server_pcb, IP_ANY_TYPE, UDP_PORT);
    if (err != ERR_OK) {
        printf("ERROR: udp_bind = %d\n", err);
        udp_remove(udp_server_pcb);
        return 1;
    }

    udp_recv(udp_server_pcb, udp_server_recv, NULL);
    printf("--> Servidor UDP escuchando en puerto %d\n", UDP_PORT);

    last_packet_ms = to_ms_since_boot(get_absolute_time());
    uint32_t last_led_ms = last_packet_ms;
    bool led_state = false;

    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // LED heartbeat
        if (now - last_led_ms > 500) {
            last_led_ms = now;
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        }

        // Solo para debug: avisar si pasan > 3s sin paquetes
        if (now - last_packet_ms > 3000) {
            printf("WARN: >3s sin paquetes UDP (packet_count=%lu)\n",
                   (unsigned long)packet_count);
            last_packet_ms = now; // para no spamear
        }

        sleep_ms(10);
    }
}
