/**
 * @file guante.c
 * @brief Lectura de sensores Hall del guante usando MUX y ADC en la Pico.
 *
 * Se multiplexan 5 canales analógicos hacia el ADC0 y se entregan valores
 * normalizados para cada dedo.
 */

#include "guante.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

// --- CONFIGURACIÓN DE PINES ---
/** @brief Pin GPIO conectado a la línea A del MUX. */
#define MUX_PIN_A 16 
/** @brief Pin GPIO conectado a la línea B del MUX. */
#define MUX_PIN_B 17 
/** @brief Pin GPIO conectado a la línea C del MUX. */
#define MUX_PIN_C 18 

/** @brief Pin GPIO usado como entrada analógica del ADC. */
#define ADC_PIN      26      
/** @brief Canal de ADC correspondiente al pin configurado. */
#define ADC_CHANNEL  0       

// --- RANGOS Y CALIBRACIÓN ---
// Mantenemos RAW_MIN/MAX conservadores. La lógica fina se hace en el Server.
/** @brief Valor mínimo de ADC esperado (crudo) para el mapeo. */
static const long RAW_MIN = 1200;   
/** @brief Valor máximo de ADC esperado (crudo) para el mapeo. */
static const long RAW_MAX = 3350;   

/** @brief Valor mínimo de salida normalizada. */
static const long OUTPUT_MIN = 0;
/** @brief Valor máximo de salida normalizada (0–9). */
static const long OUTPUT_MAX = 9; // Ajustado a 9 para dar máxima resolución al server

/** @brief Indica si el guante ya fue inicializado. */
static bool guante_inicializado = false;

// ---- Helpers internos (Optimizados para velocidad) ----

/**
 * @brief Mapea linealmente un valor desde un rango de entrada a uno de salida.
 *
 * @param x Valor a convertir.
 * @param in_min Límite inferior del rango de entrada.
 * @param in_max Límite superior del rango de entrada.
 * @param out_min Límite inferior del rango de salida.
 * @param out_max Límite superior del rango de salida.
 * @return Valor mapeado al nuevo rango.
 */
static inline long map_sensor(long x, long in_min, long in_max,
                              long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * @brief Limita un valor al intervalo [min, max].
 *
 * @param x Valor original.
 * @param min Límite inferior.
 * @param max Límite superior.
 * @return Valor recortado dentro del rango.
 */
static inline long constrain_val(long x, long min, long max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

/**
 * @brief Selecciona un canal del MUX mediante las líneas A, B y C.
 *
 * Actualiza las salidas digitales que controlan el MUX y espera un corto
 * tiempo de asentamiento para que el voltaje en la salida sea estable
 * antes de leer el ADC.
 *
 * @param channel Canal del MUX a seleccionar (0–7).
 */
static void select_mux_channel(int channel) {
    gpio_put(MUX_PIN_A, channel & 1);
    gpio_put(MUX_PIN_B, (channel >> 1) & 1);
    gpio_put(MUX_PIN_C, (channel >> 2) & 1);
    
    // TIEMPO DE ASENTAMIENTO (CRÍTICO)
    // Se usan 50us para permitir que el voltaje se estabilice físicamente
    // tras el cambio de canal, evitando el "efecto fantasma" del dedo 4.
    // Esto es un retardo de hardware (necesario), no de lógica.
    sleep_us(50);
}

// ---- API pública ----

/**
 * @brief Inicializa los pines del MUX y el ADC para lectura del guante.
 *
 * Configura las líneas A/B/C del MUX como salidas y el ADC0 sobre GPIO26.
 * Solo hace la inicialización una vez.
 *
 * @return true si la inicialización se realizó correctamente.
 */
bool guante_init(void) {
    if (guante_inicializado) return true;

    gpio_init(MUX_PIN_A); gpio_set_dir(MUX_PIN_A, GPIO_OUT);
    gpio_init(MUX_PIN_B); gpio_set_dir(MUX_PIN_B, GPIO_OUT);
    gpio_init(MUX_PIN_C); gpio_set_dir(MUX_PIN_C, GPIO_OUT);

    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(ADC_CHANNEL);

    guante_inicializado = true;
    return true;
}

/**
 * @brief Lee los valores de los dedos del guante y los normaliza.
 *
 * Recorre los canales del MUX, lee el ADC, mapea los valores crudos
 * a un rango 0–9 y los escribe en el arreglo de salida.
 *
 * @param[out] out Arreglo de tamaño GUANTE_NUM_DEDOS con el valor de cada dedo.
 */
void guante_leer_dedos(uint8_t out[GUANTE_NUM_DEDOS]) {
    if (!guante_inicializado) guante_init();

    adc_select_input(ADC_CHANNEL);

    // Ciclo de lectura atómico (Non-blocking para la aplicación)
    // Tarda aprox 5 * (50us + 2us) = ~260us total.
    for (int channel = 0; channel < GUANTE_NUM_DEDOS; channel++) {
        
        select_mux_channel(channel);
        
        uint16_t raw_value = adc_read();

        // Mapeo lineal rápido
        long mapped = map_sensor((long)raw_value, RAW_MIN, RAW_MAX, OUTPUT_MIN, OUTPUT_MAX);
        long constrained = constrain_val(mapped, OUTPUT_MIN, OUTPUT_MAX);

        // NOTA: Entregamos el valor "crudo normalizado" (0..9).
        // La inversión de lógica (abrir/cerrar) se delega al Servidor 
        // para mantener esta librería agnóstica del actuador.
        out[channel] = (uint8_t)constrained;
    }
}
