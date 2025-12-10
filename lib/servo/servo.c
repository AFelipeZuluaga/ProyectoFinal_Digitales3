#include "servo.h"
#include "pico/stdlib.h"
#include <math.h>

/* --- Registros PCA9685 --- */
#define MODE1       0x00
#define MODE2       0x01
#define PRESCALE    0xFE
#define LED0_ON_L   0x06

/* Bits */
#define MODE1_SLEEP (1u << 4)
#define MODE1_AI    (1u << 5)
#define MODE2_OUTDRV (1u << 2)

/* Oscilador interno típico */
#define PCA_OSC_HZ  25000000.0f

/* --- I2C helpers --- */
static bool write_byte(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg, data };
    return i2c_write_blocking(i2c, addr, buf, 2, false) == 2;
}

static bool read_byte(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *out) {
    if (i2c_write_blocking(i2c, addr, &reg, 1, true) != 1) return false;
    return i2c_read_blocking(i2c, addr, out, 1, false) == 1;
}

static uint8_t calc_prescale(float freq_hz) {
    float prescale_f = (PCA_OSC_HZ / (4096.0f * freq_hz)) - 1.0f;
    int prescale = (int)lroundf(prescale_f);
    if (prescale < 3) prescale = 3;
    if (prescale > 255) prescale = 255;
    return (uint8_t)prescale;
}

static bool set_freq(servo_pca_t *dev, float freq_hz) {
    uint8_t old_mode = 0;
    if (!read_byte(dev->i2c, dev->addr, MODE1, &old_mode)) return false;

    // Entrar en sleep
    uint8_t sleep_mode = (old_mode & ~MODE1_AI) | MODE1_SLEEP;
    if (!write_byte(dev->i2c, dev->addr, MODE1, sleep_mode)) return false;

    uint8_t prescale = calc_prescale(freq_hz);
    if (!write_byte(dev->i2c, dev->addr, PRESCALE, prescale)) return false;

    // Salir de sleep
    if (!write_byte(dev->i2c, dev->addr, MODE1, old_mode)) return false;
    sleep_ms(5);

    // Auto increment
    if (!write_byte(dev->i2c, dev->addr, MODE1, old_mode | MODE1_AI)) return false;

    dev->freq_hz = freq_hz;
    return true;
}

static bool set_pwm_raw(servo_pca_t *dev, uint8_t channel, uint16_t on, uint16_t off) {
    if (channel > 15) return false;
    if (on > 4095 || off > 4095) return false;

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

bool servo_init(servo_pca_t *dev) {
    if (!dev) return false;

    // I2C1 en GP2/GP3
    i2c_init(SERVO_I2C, 100000);
    gpio_set_function(SERVO_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SERVO_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SERVO_SDA_PIN);
    gpio_pull_up(SERVO_SCL_PIN);

    dev->i2c = SERVO_I2C;
    dev->addr = PCA9685_ADDR;
    dev->freq_hz = 0.0f;

    // Config base
    if (!write_byte(dev->i2c, dev->addr, MODE1, 0x00)) return false;
    if (!write_byte(dev->i2c, dev->addr, MODE2, MODE2_OUTDRV)) return false;

    sleep_ms(10);

    // Frecuencia de servo
    if (!set_freq(dev, SERVO_FREQ_HZ)) return false;

    return true;
}

bool servo_set_us(servo_pca_t *dev, uint8_t channel, float us) {
    if (!dev || dev->freq_hz <= 0.0f) return false;

    // Saturar a un rango razonable para test
    if (us < 500.0f) us = 500.0f;
    if (us > 2500.0f) us = 2500.0f;

    float period_us = 1000000.0f / dev->freq_hz;     // 20ms en 50Hz
    float counts_f = (us / period_us) * 4096.0f;

    if (counts_f < 0.0f) counts_f = 0.0f;
    if (counts_f > 4095.0f) counts_f = 4095.0f;

    uint16_t off = (uint16_t)lroundf(counts_f);

    // on=0 para simpleza
    return set_pwm_raw(dev, channel, 0, off);
}
