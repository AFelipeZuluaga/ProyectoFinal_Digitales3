#include <string.h>
#include <stdio.h>
#include <stdlib.h> // Necesario para rand()
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "hardware/watchdog.h"

// --- CONFIGURACIÓN DE RED ---
#define WIFI_SSID "MANO_ROBOTICA_NET"
#define WIFI_PASSWORD "12345678"
#define SERVER_IP "192.168.4.1"
#define TCP_PORT 4242

struct tcp_pcb *client_pcb;
bool connected = false;

// --- RED ---
err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err == ERR_OK) {
        printf(">>> CONECTADO. Iniciando SIMULACION DE DATOS.\n");
        connected = true;
    } else {
        printf("!!! Fallo conexión TCP (%d)\n", err);
        connected = false;
    }
    return err;
}

void send_string(const char *data) {
    if (!connected || client_pcb == NULL) return;
    
    err_t err = tcp_write(client_pcb, data, strlen(data), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("Error enviando datos: %d\n", err);
        connected = false; 
    } else {
        tcp_output(client_pcb);
    }
}

// --- MAIN ---
int main() {
    stdio_init_all();
    sleep_ms(3000); 
    printf("=== GUANTE WI-FI (MODO SIMULACION) ===\n");

    // Inicializar semilla aleatoria (usamos tiempo o algo variable si es posible,
    // pero para tests simples esto basta).
    srand(time_us_32());

    // 1. ACTIVAR WATCHDOG
    watchdog_enable(8000, 1); 

    // 2. INICIAR WI-FI
    if (cyw43_arch_init()) {
        printf("Fallo Wi-Fi. Reiniciando...\n");
        while(1); 
    }
    cyw43_arch_enable_sta_mode();

    // IP Estática
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    ip_addr_t ip, netmask, gateway;
    IP4_ADDR(&ip, 192, 168, 4, 2);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 4, 1);
    netif_set_addr(n, &ip, &netmask, &gateway);
    netif_set_up(n);

    // Conexión Wi-Fi
    printf("Conectando Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 15000)) {
        printf("No se pudo conectar. Esperando reinicio...\n");
        while(1); 
    }
    printf("Wi-Fi OK.\n");

    // Conexión TCP
    client_pcb = tcp_new();
    tcp_bind(client_pcb, &ip, 0);
    ip_addr_t server_ip;
    ip4addr_aton(SERVER_IP, &server_ip);
    tcp_connect(client_pcb, &server_ip, TCP_PORT, tcp_client_connected);

    uint8_t seq = 0;
    char buffer_trama[64];

    // --- BUCLE PRINCIPAL (SIMULACIÓN) ---
    while (true) {
        watchdog_update(); 

        if (connected) {
            // Generar valores aleatorios entre 0 y 9
            // Simulamos movimientos independientes para ver si el servo responde
            int sim_val_0 = rand() % 10; // Simula Pulgar (o Indice tras el swap)
            int sim_val_1 = rand() % 10; // Simula Indice (o Pulgar tras el swap)
            int sim_val_2 = rand() % 10; // Simula Medio
            
            // Construimos la trama manteniendo TU lógica de SWAP original:
            // Original: V0 recibe señal de ADC1 (Indice real -> V0 Robot)
            //           V1 recibe señal de ADC0 (Pulgar real -> V1 Robot)
            //           V2 recibe señal de ADC2 (Medio real  -> V2 Robot)
            
            snprintf(buffer_trama, sizeof(buffer_trama), 
                     "H,%d,0,0,%d,%d,%d", 
                     (int)seq, 
                     sim_val_1,  // Slot V0: Asignamos valor aleatorio 1
                     sim_val_0,  // Slot V1: Asignamos valor aleatorio 0
                     sim_val_2); // Slot V2: Asignamos valor aleatorio 2

            printf("TX SIMULADO: %s\n", buffer_trama);
            send_string(buffer_trama);
            seq++;
        } else {
            printf("Esperando conexión...\n");
        }

        // Enviamos un poco más lento para ver claramente el movimiento (250ms)
        sleep_ms(250);
    }
}