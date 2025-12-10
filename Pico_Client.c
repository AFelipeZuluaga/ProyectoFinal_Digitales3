#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

#define WIFI_SSID "MANO_ROBOTICA_NET"
#define WIFI_PASSWORD "12345678"
#define SERVER_IP "192.168.4.1"
#define TCP_PORT 4242

struct tcp_pcb *client_pcb;
bool connected = false;

err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err == ERR_OK) {
        printf("CONECTADO AL ROBOT!\n");
        printf("Escribe la trama (ej: H,1,0,9,5,0,0) y presiona ENTER:\n");
        connected = true;
    } else {
        printf("Fallo conexion (%d)\n", err);
    }
    return err;
}

void send_string(const char *data) {
    if (!connected || client_pcb == NULL) {
        printf("Error: No conectado.\n");
        return;
    }
    // Enviar datos
    tcp_write(client_pcb, data, strlen(data), TCP_WRITE_FLAG_COPY);
    tcp_output(client_pcb); // Forzar envío inmediato
}

int main() {
    stdio_init_all();
    sleep_ms(5000); // Espera para abrir monitor
    printf("--- TRANSMISOR DE COMANDOS MANUAL ---\n");

    if (cyw43_arch_init()) return 1;
    cyw43_arch_enable_sta_mode();

    // Configurar IP Estática (Cliente)
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    ip_addr_t ip, netmask, gateway;
    IP4_ADDR(&ip, 192, 168, 4, 2);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 4, 1);
    netif_set_addr(n, &ip, &netmask, &gateway);
    netif_set_up(n);

    printf("Conectando Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Error al conectar Wi-Fi.\n");
        return 1;
    }
    printf("Wi-Fi OK.\n");

    // Conectar TCP
    client_pcb = tcp_new();
    tcp_bind(client_pcb, &ip, 0); // Bind a IP local
    
    ip_addr_t server_ip;
    ip4addr_aton(SERVER_IP, &server_ip);
    
    tcp_connect(client_pcb, &server_ip, TCP_PORT, tcp_client_connected);

    // Bucle de lectura de terminal
    char input_buffer[128];
    while (true) {
        if (connected) {
            // Leer línea de la terminal
            // fgets se bloquea hasta que das Enter
            if (fgets(input_buffer, sizeof(input_buffer), stdin)) {
                
                // Eliminar el salto de línea al final (\n) que agrega fgets
                input_buffer[strcspn(input_buffer, "\r\n")] = 0;

                if (strlen(input_buffer) > 0) {
                    printf("Enviando: %s ...\n", input_buffer);
                    send_string(input_buffer);
                }
            }
        }
        sleep_ms(10); // Pequeña pausa
    }
}