#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "servo.h" 

// --- CONFIGURACIÓN ---
#define AP_SSID "MANO_ROBOTICA_NET"
#define AP_PASSWORD "12345678"
#define TCP_PORT 4242

#define NUM_FINGERS 5
#define VMAX 9
#define INVERT_FINGER_INDEX 4 

servo_pca_t servo_dev;

// --- FUNCIONES DE SERVOS ---
static int clampi(int x, int a, int b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

static float value_to_us(int finger_index, int v) {
    v = clampi(v, 0, VMAX);
    float norm = (float)v / (float)VMAX;
    if (finger_index == INVERT_FINGER_INDEX) norm = 1.0f - norm;
    return SERVO_US_MIN + norm * (SERVO_US_MAX - SERVO_US_MIN);
}

static void apply_values(servo_pca_t *dev, const int v[NUM_FINGERS]) {
    // Si el dispositivo no se inició correctamente (freq = 0), no intentamos mover
    if (dev->freq_hz == 0) return; 
    
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
    strncpy(buf_copy, line, sizeof(buf_copy)-1);
    buf_copy[sizeof(buf_copy)-1] = 0;

    char *tok = strtok(buf_copy, delims);
    while (tok && count < 16) {
        if (*tok != '\0') tokens[count++] = tok;
        tok = strtok(NULL, delims);
    }

    if (count != 7 || strcmp(tokens[0], "H") != 0) return false;

    int vals[NUM_FINGERS];
    for (int i = 0; i < NUM_FINGERS; i++) {
        char *end = NULL;
        long v = strtol(tokens[i + 2], &end, 10);
        if (!end || *end != '\0') return false;
        vals[i] = clampi((int)v, 0, VMAX);
    }

    apply_values(&servo_dev, vals);
    printf("MOVIMIENTO: [%d %d %d %d %d]\n", vals[0], vals[1], vals[2], vals[3], vals[4]);
    return true;
}

// --- CALLBACKS RED ---
err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) { tcp_close(tpcb); return ERR_OK; }

    char buffer[128];
    u16_t len = p->len > 127 ? 127 : p->len;
    memcpy(buffer, p->payload, len);
    buffer[len] = '\0'; 

    printf("RX: %s\n", buffer);
    if (parse_and_execute(buffer)) {
        tcp_write(tpcb, "ACK", 3, TCP_WRITE_FLAG_COPY);
    } 
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("NUEVO CLIENTE CONECTADO!\n");
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// --- MAIN ---
int main() {
    stdio_init_all();
    sleep_ms(5000); 
    printf("=== INICIANDO SISTEMA (SERVER V2) ===\n");

    // 1. INICIAR WI-FI PRIMERO (Prioridad Crítica)
    // Así garantizamos que la red exista aunque los servos fallen
    if (cyw43_arch_init()) {
        printf("ERROR FATAL: Fallo al iniciar hardware Wi-Fi\n");
        return 1;
    }
    
    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    struct netif *n = &cyw43_state.netif[CYW43_ITF_AP];
    ip_addr_t ip, netmask, gateway;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 192, 168, 4, 1);
    netif_set_addr(n, &ip, &netmask, &gateway);
    netif_set_up(n);

    printf("--> RED WI-FI CREADA: %s\n", AP_SSID);

    // 2. INICIAR SERVOS (Con protección)
    printf("--> Iniciando I2C para Servos...\n");
    bool servos_ok = servo_init(&servo_dev);
    
    if (!servos_ok) {
        printf("!!! ALERTA: No se detecta PCA9685. Revisa cables y energía !!!\n");
        printf("    (El sistema seguira funcionando solo con Wi-Fi)\n");
        // Marcamos frecuencia en 0 para evitar intentos de escritura
        servo_dev.freq_hz = 0; 
    } else {
        printf("--> Servos OK. Moviendo a Home.\n");
        int v_init[NUM_FINGERS] = {0,0,0,0,0};
        apply_values(&servo_dev, v_init);
    }

    // 3. INICIAR TCP
    struct tcp_pcb *pcb = tcp_new();
    tcp_bind(pcb, IP_ADDR_ANY, TCP_PORT);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, tcp_server_accept);

    printf("--> Servidor escuchando en puerto %d\n", TCP_PORT);
    printf("--> SISTEMA LISTO.\n");

    while (1) {
        // Parpadeo lento para indicar vida
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(500);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(500);
    }
}