#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

/* --- Config básica --- */
#define SERVO_I2C         i2c1
#define SERVO_SDA_PIN     2
#define SERVO_SCL_PIN     3

#define PCA9685_ADDR      0x40
#define SERVO_FREQ_HZ     50.0f

/* Rango típico seguro SG90 */
#define SERVO_US_MIN      800.0f
#define SERVO_US_CENTER   1500.0f
#define SERVO_US_MAX      2200.0f

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    float freq_hz;
} servo_pca_t;

/**
 * Inicializa I2C en GP2/GP3 e inicializa PCA9685 a 50 Hz.
 */
bool servo_init(servo_pca_t *dev);

/**
 * Setea un pulso en microsegundos en un canal (0..15).
 * Ej: 1000, 1500, 2000.
 */
bool servo_set_us(servo_pca_t *dev, uint8_t channel, float us);

#endif /* SERVO_H */
