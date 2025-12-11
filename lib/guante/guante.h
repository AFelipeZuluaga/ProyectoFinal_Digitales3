#ifndef GUANTE_H
#define GUANTE_H

#include <stdint.h>
#include <stdbool.h>

#define GUANTE_NUM_DEDOS 5

/**
 * Inicializa MUX (A,B,C) y ADC0 (GPIO26).
 */
bool guante_init(void);

/**
 * Lee los 5 sensores del guante y devuelve valores normalizados 0–7
 * en este orden:
 *   out[0] -> Pulgar  (canal 0)
 *   out[1] -> Índice  (canal 1)
 *   out[2] -> Medio   (canal 2)
 *   out[3] -> Anular  (canal 3)
 *   out[4] -> Meñique (canal 4)
 */
void guante_leer_dedos(uint8_t out[GUANTE_NUM_DEDOS]);

#endif // GUANTE_H
