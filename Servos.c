#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "lib/servo/servo.h"  // ajusta el include si tu ruta difiere

#define NUM_FINGERS 5
#define VMAX 9

// Índice del "servo 5" (5º dedo) => CH4
#define INVERT_FINGER_INDEX 4

static int clampi(int x, int a, int b) {
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

static void print_help(void) {
    printf("\n=== RX trama enteros -> 5 servos ===\n");
    printf("Formato:\n");
    printf("  H,SEQ,V0,V1,V2,V3,V4\n");
    printf("Rangos:\n");
    printf("  SEQ: 0..255\n");
    printf("  Vi : 0..%d\n", VMAX);
    printf("Nota:\n");
    printf("  V4 (servo 5 / CH4) esta INVERTIDO.\n\n");
    printf("Ejemplos:\n");
    printf("  H,1,0,1,2,3,4\n");
    printf("  H,2,9,9,9,9,9\n");
    printf("  H,3,0,0,0,0,9  (CH4 ira al extremo contrario)\n\n");
    printf("Comandos:\n");
    printf("  help\n");
    printf("  demo   (abre/cierra CH0..CH4 con inversion en CH4)\n\n");
}

// Convierte un valor entero Vi (0..VMAX) a pulso de servo.
// Aplica inversion solo al dedo indicado.
static float value_to_us(int finger_index, int v) {
    v = clampi(v, 0, VMAX);

    float norm = (float)v / (float)VMAX;

    if (finger_index == INVERT_FINGER_INDEX) {
        norm = 1.0f - norm;  // inversion del sentido
    }

    float us = SERVO_US_MIN + norm * (SERVO_US_MAX - SERVO_US_MIN);
    return us;
}

static void apply_values(servo_pca_t *dev, const int v[NUM_FINGERS]) {
    for (int i = 0; i < NUM_FINGERS; i++) {
        float us = value_to_us(i, v[i]);
        servo_set_us(dev, (uint8_t)i, us);
    }
}

static void run_demo(servo_pca_t *dev) {
    printf("Demo: CH0..CH4 (CH4 invertido)\n");

    int v_open[NUM_FINGERS]   = {0, 0, 0, 0, 0};
    int v_mid[NUM_FINGERS]    = {VMAX/2, VMAX/2, VMAX/2, VMAX/2, VMAX/2};
    int v_close[NUM_FINGERS]  = {VMAX, VMAX, VMAX, VMAX, VMAX};

    apply_values(dev, v_open);
    sleep_ms(400);

    apply_values(dev, v_mid);
    sleep_ms(400);

    apply_values(dev, v_close);
    sleep_ms(400);

    apply_values(dev, v_open);
    sleep_ms(400);
}

// Parsea una línea "H,SEQ,V0,V1,V2,V3,V4"
// Retorna true si ok y llena seq y vals[5]
static bool parse_frame(char *line, int *seq_out, int vals[NUM_FINGERS]) {
    // Permitimos separadores coma o espacio
    const char *delims = ", ";

    char *tokens[16];
    int count = 0;

    char *tok = strtok(line, delims);
    while (tok && count < 16) {
        if (*tok != '\0') tokens[count++] = tok;
        tok = strtok(NULL, delims);
    }

    // Esperamos exactamente 7 tokens: H SEQ V0 V1 V2 V3 V4
    if (count != 7) return false;

    if (strcmp(tokens[0], "H") != 0) return false;

    // SEQ
    char *end = NULL;
    long seq = strtol(tokens[1], &end, 10);
    if (!end || *end != '\0') return false;
    if (seq < 0 || seq > 255) return false;

    for (int i = 0; i < NUM_FINGERS; i++) {
        end = NULL;
        long v = strtol(tokens[i + 2], &end, 10);
        if (!end || *end != '\0') return false;
        vals[i] = clampi((int)v, 0, VMAX);
    }

    *seq_out = (int)seq;
    return true;
}

int main() {
    stdio_init_all();
    sleep_ms(1200);

    printf("=== Pico H | PCA9685 | RX trama enteros ===\n");
    printf("I2C1 GP2=SDA GP3=SCL | Servos CH0..CH4\n");
    printf("Rango Vi: 0..%d | CH4 invertido\n", VMAX);

    servo_pca_t dev;
    if (!servo_init(&dev)) {
        while (true) {
            printf("ERROR: PCA9685 no responde por I2C.\n");
            sleep_ms(1000);
        }
    }

    // Posición segura inicial usando el mapeo por valores
    int v_init[NUM_FINGERS] = {0, 0, 0, 0, 0};
    apply_values(&dev, v_init);

    print_help();

    char line[128];

    while (true) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            sleep_ms(20);
            continue;
        }

        // limpiar \r\n
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0') continue;

        // comandos simples
        if (!strcmp(line, "help")) {
            print_help();
            continue;
        }
        if (!strcmp(line, "demo")) {
            run_demo(&dev);
            continue;
        }

        // Copia porque strtok modifica string
        char buf[128];
        strncpy(buf, line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        int seq = 0;
        int v[NUM_FINGERS] = {0};

        if (!parse_frame(buf, &seq, v)) {
            printf("Trama invalida. Usa: H,SEQ,V0,V1,V2,V3,V4\n");
            continue;
        }

        // Aplicar valores a CH0..CH4 con inversion en CH4
        apply_values(&dev, v);

        printf("OK SEQ=%d | V=[%d %d %d %d %d] (V4 invertido)\n",
               seq, v[0], v[1], v[2], v[3], v[4]);
    }
}
