#ifndef GUANTE_H
#define GUANTE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @def GUANTE_NUM_DEDOS
 * @brief Número total de dedos leídos por el guante.
 */
#define GUANTE_NUM_DEDOS 5

/**
 * @brief Inicializa el hardware del guante.
 *
 * Configura el multiplexor (líneas A, B, C) y el ADC0 (GPIO26) para
 * poder leer los sensores Hall de los dedos.
 *
 * @return true si la inicialización fue correcta, false en caso de error.
 */
bool guante_init(void);

/**
 * @brief Lee los 5 sensores del guante y entrega valores normalizados.
 *
 * Obtiene una lectura por cada dedo y devuelve valores en el rango 0–7
 * en el siguiente orden dentro del arreglo:
 *   out[0] -> Pulgar  (canal 0)  
 *   out[1] -> Índice  (canal 1)  
 *   out[2] -> Medio   (canal 2)  
 *   out[3] -> Anular  (canal 3)  
 *   out[4] -> Meñique (canal 4)
 *
 * @param[out] out Arreglo de tamaño GUANTE_NUM_DEDOS con los valores de cada dedo.
 */
void guante_leer_dedos(uint8_t out[GUANTE_NUM_DEDOS]);

#endif // GUANTE_H
