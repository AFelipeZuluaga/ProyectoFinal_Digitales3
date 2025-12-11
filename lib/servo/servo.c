/**
 * @file servo.c
 * @brief Implementación del driver PCA9685 para servomotores.
 */

#include "servo.h"
#include "pico/stdlib.h"
#include <math.h>

/* --- Registros PCA9685 --- */
/** Registro MODE1 del PCA9685. */
#define MODE1       0x00
/** Registro MODE2 del PCA9685. */
#define MODE2       0x01
/** Registro de prescaler de frecuencia. */
#define PRESCALE    0xFE
/** Registro base LED0_ON_L del primer canal. */
#define LED0_ON_L   0x06

/* Bits */
/** Bit de SLEEP en MODE1. */
#define MODE1_SLEEP (1u << 4)
/** Bit de Auto-Increment en MODE1. */
#define MODE1_AI    (1u << 5)
/** Bit de salida tipo totem-pole en MODE2. */
#define MODE2_OUTDRV (1u << 2)

/** Frecuencia del oscilador interno del PCA9685 (Hz). */
#define PCA_OSC_HZ  25000000.0f

// Helpers de I2C (Atómicos)
/**
 * @brief Escribe un byte en un registro vía I2C.
 * @param i2c  Instancia I2C.
 * @param addr Dirección I2C del dispositivo.
 * @param reg  Registro a escribir.
 * @param data Dato de 8 bits a escribir.
 * @return true si se escribieron 2 bytes, false en error.
 */
static bool write_byte(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg, data };
    // Escritura bloqueante rápida (decenas de microsegundos)
    return i2c_write_blocking(i2c, addr, buf, 2, false) == 2;
}

/**
 * @brief Lee un byte de un registro vía I2C.
 * @param i2c  Instancia I2C.
 * @param addr Dirección I2C del dispositivo.
 * @param reg  Registro a leer.
 * @param out  Puntero de salida para el byte leído.
 * @return true si se leyó 1 byte, false en error.
 */
static bool read_byte(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *out) {
    if (i2c_write_blocking(i2c, addr, &reg, 1, true) != 1) return false;
    return i2c_read_blocking(i2c, addr, out, 1, false) == 1;
}

/**
 * @brief Calcula el prescaler para una frecuencia PWM dada.
 * @param freq_hz Frecuencia deseada en Hz.
 * @return Valor de prescaler (3–255).
 */
static uint8_t calc_prescale(float freq_hz) {
    float prescale_f = (PCA_OSC_HZ / (4096.0f * freq_hz)) - 1.0f;
    int prescale = (int)lroundf(prescale_f);
    if (prescale < 3) prescale = 3;
    if (prescale > 255) prescale = 255;
    return (uint8_t)prescale;
}

/**
 * @brief Configura la frecuencia de PWM del PCA9685.
 * @param dev     Dispositivo PCA9685.
 * @param freq_hz Frecuencia deseada en Hz.
 * @return true en éxito, false en error.
 */
static bool set_freq(servo_pca_t *dev, float freq_hz) {
    uint8_t old_mode = 0;
    if (!read_byte(dev->i2c, dev->addr, MODE1, &old_mode)) return false;

    // Configuración requiere modo sleep momentáneo (solo en init)
    uint8_t sleep_mode = (old_mode & ~MODE1_AI) | MODE1_SLEEP;
    if (!write_byte(dev->i2c, dev->addr, MODE1, sleep_mode)) return false;

    uint8_t prescale = calc_prescale(freq_hz);
    if (!write_byte(dev->i2c, dev->addr, PRESCALE, prescale)) return false;

    if (!write_byte(dev->i2c, dev->addr, MODE1, old_mode)) return false;
    
    // Espera técnica de oscilador (5ms). 
    // SOLO PERMITIDA EN INIT, nunca en loop principal.
    sleep_ms(5);

    if (!write_byte(dev->i2c, dev->addr, MODE1, old_mode | MODE1_AI)) return false;

    dev->freq_hz = freq_hz;
    return true;
}

/**
 * @brief Escribe valores RAW de PWM en un canal.
 * @param dev     Dispositivo PCA9685.
 * @param channel Canal [0..15].
 * @param on      Cuenta de inicio (0–4095).
 * @param off     Cuenta de fin (0–4095).
 * @return true si la escritura I2C fue correcta, false en error.
 */
static bool set_pwm_raw(servo_pca_t *dev, uint8_t channel, uint16_t on, uint16_t off) {
    if (channel > 15) return false;
    
    // I2C Write de 5 bytes: ~100us a 400kHz. 
    // Completamente seguro para llamar dentro de un ciclo de Polling rápido.
    uint8_t reg = LED0_ON_L + 4 * channel;
    uint8_t buf[5] = {
        reg,
        (uint8_t)(on & 0xFF),
        (uint8_t)(on >> 8),
        (uint8_t)(off & 0xFF),
        (uint8_t)(off >> 8)
    };

    return i2c_write_blocking(dev->i2c, dev->addr, buf, 5, false) == 5;
}

/**
 * @brief Inicializa I2C y el PCA9685 para control de servos.
 * @param dev Dispositivo PCA9685 a configurar.
 * @return true en éxito, false en error o puntero nulo.
 */
bool servo_init(servo_pca_t *dev) {
    if (!dev) return false;

    // Inicialización I2C
    i2c_init(SERVO_I2C, 400000); // Aumentamos a 400kHz para menor latencia
    gpio_set_function(SERVO_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SERVO_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SERVO_SDA_PIN);
    gpio_pull_up(SERVO_SCL_PIN);

    dev->i2c = SERVO_I2C;
    dev->addr = PCA9685_ADDR;
    dev->freq_hz = 0.0f;

    // Reset software básico
    write_byte(dev->i2c, dev->addr, MODE1, 0x00);
    write_byte(dev->i2c, dev->addr, MODE2, MODE2_OUTDRV);
    
    // Configuración de frecuencia (incluye el único sleep_ms del driver)
    if (!set_freq(dev, SERVO_FREQ_HZ)) return false;

    return true;
}

/**
 * @brief Configura el pulso de un canal en microsegundos.
 * @param dev     Dispositivo PCA9685.
 * @param channel Canal [0..15].
 * @param us      Ancho de pulso en µs.
 * @return true en éxito, false en error.
 */
bool servo_set_us(servo_pca_t *dev, uint8_t channel, float us) {
    if (!dev || dev->freq_hz <= 0.0f) return false;

    // Clamping de seguridad
    if (us < 400.0f) us = 400.0f;
    if (us > 2600.0f) us = 2600.0f;

    float period_us = 1000000.0f / dev->freq_hz;
    float counts_f = (us / period_us) * 4096.0f;

    if (counts_f < 0.0f) counts_f = 0.0f;
    if (counts_f > 4095.0f) counts_f = 4095.0f;

    uint16_t off = (uint16_t)lroundf(counts_f);

    return set_pwm_raw(dev, channel, 0, off);
}
