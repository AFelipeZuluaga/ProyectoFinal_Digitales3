#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// --- Configuración General ---
// NO_SYS = 1 significa que lo usaremos sin un Sistema Operativo complejo (FreeRTOS)
// usaremos la "Raw API" que es más rápida y simple.
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define LWIP_IGMP                   0
#define LWIP_ICMP                   1

// --- Gestión de Memoria ---
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

// --- Configuración TCP ---
#define LWIP_TCP                    1
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

// --- Configuración UDP ---
#define LWIP_UDP                    1

// --- Configuración ARP y Ethernet ---
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_DHCP                   1  // Importante para obtener IP del router

// --- Integración con la Pico ---
// Esto permite que lwIP use las funciones de la Pico para números aleatorios, etc.
#define LWIP_RAND() ((u32_t)rand())

#endif
