#include "guante.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

// --- CONFIGURACIÓN DE PINES ---
// Pines de Control del Multiplexor (A, B, C)
#define MUX_PIN_A 16 // GP16
#define MUX_PIN_B 17 // GP17
#define MUX_PIN_C 18 // GP18

// Pin de Lectura Analógica (Salida del Multiplexor al Pico)
#define ADC_PIN      26      // GP26
#define ADC_CHANNEL  0       // Corresponde a ADC0

// --- RANGOS DE CALIBRACIÓN Y NORMALIZACIÓN ---
// Ajusta estos valores según tus lecturas reales
// (con el main de prueba que tenías antes)
static const long RAW_MIN = 1200;   // mano "abierta" (imán cerca)
static const long RAW_MAX = 3350;   // mano "cerrada" (imán lejos)

// Rango de salida para la trama (0–7 para no reventar servos)
static const long OUTPUT_MIN = 0;
static const long OUTPUT_MAX = 7;

static bool guante_inicializado = false;

// ---- Helpers internos ----

static long map_sensor(long x, long in_min, long in_max,
                       long out_min, long out_max) {
    if (in_max == in_min) {
        return out_min; // evita división por 0 si algo raro pasa
    }
    return (x - in_min) * (out_max - out_min) / (in_max - in_min)
           + out_min;
}

static long constrain_val(long x, long min, long max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

static void select_mux_channel(int channel) {
    gpio_put(MUX_PIN_A, channel & 1);
    gpio_put(MUX_PIN_B, (channel >> 1) & 1);
    gpio_put(MUX_PIN_C, (channel >> 2) & 1);
    // Pequeño tiempo para que se asiente el MUX
    sleep_us(10);
}

// ---- API pública ----

bool guante_init(void) {
    if (guante_inicializado) {
        return true;
    }

    // Pines del MUX
    gpio_init(MUX_PIN_A); gpio_set_dir(MUX_PIN_A, GPIO_OUT);
    gpio_init(MUX_PIN_B); gpio_set_dir(MUX_PIN_B, GPIO_OUT);
    gpio_init(MUX_PIN_C); gpio_set_dir(MUX_PIN_C, GPIO_OUT);

    // ADC
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(ADC_CHANNEL);

    guante_inicializado = true;
    return true;
}

void guante_leer_dedos(uint8_t out[GUANTE_NUM_DEDOS]) {
    if (!guante_inicializado) {
        guante_init();
    }

    // Aseguramos que estamos en el canal correcto del ADC
    adc_select_input(ADC_CHANNEL);

    for (int channel = 0; channel < GUANTE_NUM_DEDOS; channel++) {
        select_mux_channel(channel);

        // Pequeña pausa para que el ADC "vea" el nuevo canal del MUX
        sleep_us(5);
        uint16_t raw_value = adc_read();

        long mapped = map_sensor((long)raw_value,
                                 RAW_MIN, RAW_MAX,
                                 OUTPUT_MIN, OUTPUT_MAX);

        long constrained = constrain_val(mapped,
                                         OUTPUT_MIN, OUTPUT_MAX);

        // Aquí invertimos el sentido:
        // antes: 0 = flexionado, 7 = estirado
        // ahora: 0 = estirado, 7 = flexionado
        long inverted = OUTPUT_MAX - constrained;

        out[channel] = (uint8_t)inverted;
    }
}
