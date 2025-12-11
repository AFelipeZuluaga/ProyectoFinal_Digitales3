/**
 * @file servo.h
 * @brief Interfaz del driver PCA9685 para servomotores.
 */

#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

/** Instancia I2C usada para el controlador de servos. */
#define SERVO_I2C         i2c1
/** Pin SDA del bus I2C. */
#define SERVO_SDA_PIN     2
/** Pin SCL del bus I2C. */
#define SERVO_SCL_PIN     3

/** Dirección I2C del PCA9685. */
#define PCA9685_ADDR      0x40
/** Frecuencia PWM para servos (Hz). */
#define SERVO_FREQ_HZ     50.0f

/* --- RANGOS DE TRABAJO --- */
/** Pulso mínimo del servo (µs). */
#define SERVO_US_MIN      500.0f
/** Pulso central del servo (µs). */
#define SERVO_US_CENTER   1500.0f
/** Pulso máximo del servo (µs). */
#define SERVO_US_MAX      2400.0f

/**
 * @brief Descriptor del controlador PCA9685.
 */
typedef struct {
    i2c_inst_t *i2c;  /**< Instancia I2C asociada. */
    uint8_t addr;     /**< Dirección I2C del PCA9685. */
    float freq_hz;    /**< Frecuencia PWM configurada. */
} servo_pca_t;

/**
 * @brief Inicializa el PCA9685 y el bus I2C.
 * @param dev Estructura del dispositivo a inicializar.
 * @return true en éxito, false en error.
 */
bool servo_init(servo_pca_t *dev);

/**
 * @brief Configura el pulso de un canal en microsegundos.
 * @param dev     Dispositivo PCA9685.
 * @param channel Canal [0..15].
 * @param us      Ancho de pulso en µs.
 * @return true en éxito, false en error.
 */
bool servo_set_us(servo_pca_t *dev, uint8_t channel, float us);

#endif /* SERVO_H */
