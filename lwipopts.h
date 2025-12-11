/**
 * @file lwipopts.h
 * @brief Configuración de lwIP para la Raspberry Pi Pico W en modo sin RTOS.
 *
 * Define parámetros de memoria, TCP/UDP, ARP y opciones de integración
 * específicas para el uso de la Raw API en este proyecto.
 */

#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// --- Configuración General ---
/**
 * @brief Modo sin sistema operativo (Raw API).
 *
 * NO_SYS = 1 indica que lwIP se ejecuta sin RTOS, usando la Raw API.
 */
#define NO_SYS                      1
/** @brief Deshabilita la API de sockets de lwIP. */
#define LWIP_SOCKET                 0
/** @brief Deshabilita la API Netconn de lwIP. */
#define LWIP_NETCONN                0
/** @brief Deshabilita soporte IGMP (no se usan grupos multicast). */
#define LWIP_IGMP                   0
/** @brief Habilita ICMP (por ejemplo, respuestas a ping). */
#define LWIP_ICMP                   1

// --- Gestión de Memoria ---
/** @brief Usa el gestor de memoria interno de lwIP (no malloc de libc). */
#define MEM_LIBC_MALLOC             0
/** @brief Alineación de memoria (en bytes). */
#define MEM_ALIGNMENT               4
/** @brief Tamaño del heap interno de lwIP (en bytes). */
#define MEM_SIZE                    4000
/** @brief Número de segmentos TCP disponibles. */
#define MEMP_NUM_TCP_SEG            32
/** @brief Número de entradas en la cola ARP. */
#define MEMP_NUM_ARP_QUEUE          10
/** @brief Tamaño del pool de pbufs para RX/TX. */
#define PBUF_POOL_SIZE              24

// --- Configuración TCP ---
/** @brief Habilita soporte TCP en la pila lwIP. */
#define LWIP_TCP                    1
/** @brief Tamaño máximo de segmento TCP (MSS). */
#define TCP_MSS                     1460
/** @brief Tamaño de la ventana TCP. */
#define TCP_WND                     (8 * TCP_MSS)
/** @brief Tamaño del buffer de envío TCP. */
#define TCP_SND_BUF                 (8 * TCP_MSS)
/** @brief Longitud de la cola de segmentos TCP listos para envío. */
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

// --- Configuración UDP ---
/** @brief Habilita soporte UDP en la pila lwIP. */
#define LWIP_UDP                    1

// --- Configuración ARP y Ethernet ---
/** @brief Habilita la caché ARP. */
#define LWIP_ARP                    1
/** @brief Indica que se usa interfaz Ethernet (base para Wi-Fi). */
#define LWIP_ETHERNET               1
/** @brief Habilita el cliente DHCP para obtener IP automáticamente. */
#define LWIP_DHCP                   1  // Importante para obtener IP del router

// --- Integración con la Pico ---
/**
 * @brief Generador de números aleatorios para lwIP.
 *
 * Usa la función estándar rand() de C como fuente de aleatoriedad.
 */
#define LWIP_RAND() ((u32_t)rand())

#endif
