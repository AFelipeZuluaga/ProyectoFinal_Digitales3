/**
 * @file Pico_Client.c
 * @brief Cliente UDP (guante) para enviar posiciones de los dedos a la mano robótica.
 *
 * Lee los sensores del guante mediante MUX + ADC y envía una trama UDP periódica
 * hacia el servidor (mano) usando la Pico W y lwIP en modo NO_SYS.
 */

// Pico_Client.c
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "hardware/timer.h"

#include "lib/guante/guante.h"

// --- CONFIGURACIÓN RED ---
/** @brief SSID del hotspot Wi-Fi al que se conecta el guante. */
#define WIFI_SSID     "iPhone de Felipe"
/** @brief Contraseña del hotspot Wi-Fi. */
#define WIFI_PASSWORD "ff11223344"
/** @brief Dirección IP del servidor (mano robótica). */
#define SERVER_IP     "172.20.10.2"
/** @brief Puerto UDP usado para enviar las tramas al servidor. */
#define UDP_PORT      4242

// --- VARIABLES VOLÁTILES (Compartidas entre IRQ y Main) ---
// volatile es OBLIGATORIO para variables modificadas en interrupciones
/** @brief Bandera levantada por la IRQ de timer para indicar envío periódico. */
static volatile bool flag_timer_send = false;

/** @brief PCB UDP del cliente (guante). */
static struct udp_pcb *udp_client_pcb = NULL;
/** @brief Indica si la conexión UDP está lista para enviar. */
static bool udp_ready = false;
/** @brief Contador de paquetes enviados por el cliente. */
static uint32_t tx_packet_count = 0;

// --- RUTINA DE INTERRUPCIÓN (TIMER IRQ) ---
// Esta función se ejecuta automáticamente cada 250ms
/**
 * @brief Callback del timer periódico para disparar el envío de datos.
 *
 * Solo levanta una bandera para que el envío real se haga en el main,
 * manteniendo la ISR lo más corta posible.
 *
 * @param t Puntero al timer que generó la interrupción.
 * @return true para que el timer siga repitiéndose.
 */
bool send_timer_callback(struct repeating_timer *t) {
    // Solo levantamos la bandera. Mantener la IRQ lo más corta posible.
    flag_timer_send = true;
    return true; // true para mantener el timer repitiéndose
}

// --- UDP ---
/**
 * @brief Crea y conecta el PCB UDP al servidor configurado.
 *
 * Crea un nuevo PCB UDP, lo conecta a la IP y puerto del servidor y
 * marca udp_ready en caso de éxito.
 *
 * @return true si la conexión UDP se configuró correctamente.
 */
static bool udp_client_connect(void) {
    if (udp_client_pcb) {
        udp_remove(udp_client_pcb);
        udp_client_pcb = NULL;
    }
    udp_client_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!udp_client_pcb) return false;

    ip_addr_t srv_ip;
    ip4addr_aton(SERVER_IP, &srv_ip);

    err_t err = udp_connect(udp_client_pcb, &srv_ip, UDP_PORT);
    if (err != ERR_OK) {
        udp_remove(udp_client_pcb);
        return false;
    }
    udp_ready = true;
    return true;
}

/**
 * @brief Envía una cadena de texto por UDP al servidor.
 *
 * Reserva un pbuf, copia la cadena y la envía usando el PCB UDP del cliente.
 *
 * @param data Puntero a la cadena terminada en '\0' a enviar.
 */
static void send_string(const char *data) {
    if (!udp_ready || !udp_client_pcb) return;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)strlen(data), PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, data, strlen(data));
    udp_send(udp_client_pcb, p);
    pbuf_free(p);
}

// --- MAIN ---
/**
 * @brief Punto de entrada del cliente (guante).
 *
 * Inicializa stdio, Wi-Fi, la librería del guante y la conexión UDP.
 * Configura un timer en IRQ cada 250 ms y, en el loop principal, realiza
 * polling de la pila de red y envía tramas periódicas con las lecturas de los dedos.
 *
 * @return 0 en operación normal, 1 si falla la inicialización.
 */
int main() {
    stdio_init_all();
    sleep_ms(3000); // Espera inicial única para USB
    printf("=== GUANTE (CLIENTE): Arq. Polling + IRQ ===\n");

    if (cyw43_arch_init()) return 1;
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 15000)) {
        printf("Fallo conexion WiFi\n");
        return 1;
    }
    printf("WiFi Conectado.\n");

    if (!guante_init()) printf("Error Guante MUX/ADC\n");

    udp_ready = udp_client_connect();

    // --- CONFIGURACIÓN DEL TIMER (IRQ) ---
    // Configura una interrupción por hardware cada 250 ms
    struct repeating_timer timer;
    add_repeating_timer_ms(-250, send_timer_callback, NULL, &timer);

    char buffer_trama[64];

    // --- LOOP PRINCIPAL (POLLING) ---
    while (1) {
        // 1. Polling de la pila de red (necesario para lwIP NO_SYS)
        cyw43_arch_poll();

        // 2. Polling de la Bandera de Interrupción
        if (flag_timer_send) {
            // Bajamos la bandera inmediatamente para no re-entrar
            flag_timer_send = false; 

            // Ejecutamos la lógica "pesada" fuera de la interrupción
            uint8_t dedos[GUANTE_NUM_DEDOS];
            guante_leer_dedos(dedos);

            snprintf(buffer_trama, sizeof(buffer_trama),
                     "H,%d,%d,%d,%d,%d",
                     dedos[4], dedos[3], dedos[2], dedos[0], dedos[1]);

            send_string(buffer_trama);
            
            tx_packet_count++;
            printf("TX[%lu]: %s\n", tx_packet_count, buffer_trama);
        }
        
    }
}
