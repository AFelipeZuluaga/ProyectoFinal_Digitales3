// Pico_Client.c

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "hardware/watchdog.h"

#include "lib/guante/guante.h"

// --- CONFIGURACIÓN RED (hotspot iOS) ---
#define WIFI_SSID     "Apto 1516"
#define WIFI_PASSWORD "SY15YRG3"

// OJO: PON AQUÍ LA IP QUE IMPRIME EL SERVER EN CONSOLA
#define SERVER_IP     "172.20.10.6"   

#define UDP_PORT      4242

static struct udp_pcb *udp_client_pcb = NULL;
static bool udp_ready = false;
static uint32_t tx_packet_count = 0;

// --- UDP ---

static bool udp_client_connect(void) {
    if (udp_client_pcb) {
        udp_remove(udp_client_pcb);
        udp_client_pcb = NULL;
    }

    udp_client_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!udp_client_pcb) {
        printf("[UDP] No se pudo crear udp_client_pcb\n");
        return false;
    }

    ip_addr_t srv_ip;
    ip4addr_aton(SERVER_IP, &srv_ip);

    err_t err = udp_connect(udp_client_pcb, &srv_ip, UDP_PORT);
    if (err != ERR_OK) {
        printf("[UDP] Error en udp_connect: %d\n", err);
        udp_remove(udp_client_pcb);
        udp_client_pcb = NULL;
        return false;
    }

    printf("[UDP] Conectado a %s:%d\n", SERVER_IP, UDP_PORT);
    udp_ready = true;
    return true;
}

static void send_string(const char *data) {
    if (!udp_ready || !udp_client_pcb) return;

    size_t len = strlen(data);
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (!p) {
        printf("[UDP] Error: no se pudo allocar pbuf\n");
        return;
    }

    memcpy(p->payload, data, len);
    err_t err = udp_send(udp_client_pcb, p);
    if (err != ERR_OK) {
        printf("[UDP] Error udp_send: %d\n", err);
    }

    pbuf_free(p);
}

// --- MAIN ---

int main() {
    stdio_init_all();
    sleep_ms(3000);
    printf("=== GUANTE (CLIENTE UDP en hotspot iOS) ===\n");

    //watchdog_enable(8000, 1);

    if (cyw43_arch_init()) {
        printf("ERROR: cyw43_arch_init\n");
        while (1) tight_loop_contents();
    }

    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);
    cyw43_arch_enable_sta_mode();

    printf("[WIFI] Conectando a SSID=%s ...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(
            WIFI_SSID, WIFI_PASSWORD,
            CYW43_AUTH_WPA2_AES_PSK,
            15000)) {
        printf("[WIFI] No se pudo conectar al hotspot.\n");
        while (1) tight_loop_contents();
    }
    printf("[WIFI] Conectado al hotspot.\n");

    // Opcional: ver IP del guante
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    const ip4_addr_t *ip = netif_ip4_addr(n);
    printf("[WIFI] IP CLIENTE (guante): %s\n", ip4addr_ntoa(ip));

    // Iniciar guante
    if (!guante_init()) {
        printf("[GUANTE] Error inicializando MUX/ADC.\n");
    } else {
        printf("[GUANTE] OK.\n");
    }

    // Conectar UDP al server
    udp_ready = udp_client_connect();

    char buffer_trama[64];
    uint32_t last_send_ms = to_ms_since_boot(get_absolute_time());

    while (1) {
        //watchdog_update();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Enviar cada 250 ms
        if (udp_ready && (now - last_send_ms >= 250)) {
            last_send_ms = now;

            uint8_t dedos[GUANTE_NUM_DEDOS];
            guante_leer_dedos(dedos);

            printf("GUANTE NORM: %u,%u,%u,%u,%u\n",
                   dedos[0], dedos[1], dedos[2], dedos[3], dedos[4]);

            int val0 = dedos[0]; // pulgar
            int val1 = dedos[1]; // índice
            int val2 = dedos[2]; // medio
            int val3 = dedos[3]; // anular
            int val4 = dedos[4]; // meñique

            // Mantengo tu mapeo a trama:
            snprintf(buffer_trama, sizeof(buffer_trama),
                     "H,%d,%d,%d,%d,%d",
                     val1,  // V0
                     val0,  // V1
                     val2,  // V2
                     val3,  // V3
                     val4); // V4

            tx_packet_count++;
            printf("TX[%lu] %s\n",
                   (unsigned long)tx_packet_count,
                   buffer_trama);

            send_string(buffer_trama);
        }

        sleep_ms(10);
    }
}
