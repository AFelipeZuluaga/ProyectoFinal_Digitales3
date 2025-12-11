# Mimic Hand – Mano Robótica (Servidor UDP – Raspberry Pi Pico W)

Este módulo implementa el **lado servidor** del proyecto Mimic Hand.  
Corresponde a la **mano robótica**, que recibe el estado de los dedos
desde el guante por **Wi-Fi (UDP)** y mueve los servomotores usando un
driver PCA9685 controlado por una Raspberry Pi Pico W.

---

## 1. Rol dentro del sistema

- Escuchar paquetes UDP en un puerto fijo (`4242`).
- Parsear tramas ASCII provenientes del guante:

  ```text
  H,v0,v1,v2,v3,v4
  ```

- Validar y almacenar los valores normalizados `0–9` de los dedos.
- Convertir estos valores a anchos de pulso en microsegundos.
- Actualizar el PCA9685 para mover los servos de la mano robótica.
- Utilizar **polling + interrupciones (IRQs)**:
  - IRQ para un heartbeat visual (LED).
  - Polling para Wi-Fi, recepción y actualización de servos.

---

## 2. Hardware asociado

- Raspberry Pi Pico W.
- Driver PCA9685 por I²C (por ejemplo, `i2c1`):
  - Pines SDA/SCL conectados a la Pico.
- Servomotores:
  - Conectados a los canales del PCA9685.
  - Alimentación separada para potencia de servos.
- Referencia de alimentación:
  - **Masa común** entre lógica (Pico + PCA9685) y servos.
- Conexión Wi-Fi:
  - Pico en modo **STA** conectado a un **hotspot de celular**.
  - Hotspot asigna IP por DHCP.
  - La IP del servidor se muestra por consola y se usa luego en el cliente.

---

## 3. Archivos relacionados

- `src/Pico_Server.c`  
  Programa principal de la mano:
  - Inicialización de Wi-Fi.
  - Inicialización del PCA9685 y servos.
  - Creación del servidor UDP.
  - Lógica de polling y actualización de servos.

- `lib/servo/servo.h`  
  - API de alto nivel para el PCA9685:
    - `servo_init(servo_pca_t *dev)`
    - `servo_set_us(dev, canal, ancho_us)`

- `lib/servo/servo.c`  
  Implementación:
  - Configuración de registros MODE1/MODE2 del PCA9685.
  - Cálculo de prescaler para la frecuencia PWM.
  - Conversión de microsegundos a cuentas de 12 bits (0–4095).

---

## 4. Flujo de ejecución del servidor

1. **Inicio y conexión Wi-Fi**
   - Inicializa `stdio` para depuración.
   - Configura la Pico W en modo **STA**.
   - Se conecta al hotspot usando SSID y contraseña definidos.
   - Obtiene una IP vía DHCP y la imprime por consola:
     - Esta IP será usada como `SERVER_IP` en el cliente.

2. **Inicialización de servos**
   - Llama a `servo_init(...)` para configurar el PCA9685:
     - Dirección I²C.
     - Frecuencia PWM.
     - Modos internos.
   - Sitúa todos los servos en una posición inicial segura (por ejemplo, posición neutra).

3. **Configuración del servidor UDP**
   - Crea un PCB UDP con `udp_new_ip_type`.
   - Hace `udp_bind` al puerto `4242` en todas las interfaces.
   - Registra un callback con `udp_recv` para manejar la llegada de paquetes.

4. **Callback de recepción UDP**
   - Cuando llega un paquete:
     - Copia los datos recibidos a un buffer local.
     - Asegura que haya terminación `' '`.
     - Parsear la trama con el formato `H,v0,v1,v2,v3,v4`.
     - Verifica que todos los valores estén dentro de `0–9`.
     - Actualiza un arreglo de valores de dedos y levanta una bandera de “nuevo dato”.
     - Imprime por consola:
       - La trama recibida.
       - El número de paquete (contador).

5. **Configuración de IRQ (heartbeat)**
   - Crea un `repeating_timer` que se dispara periódicamente.
   - En el callback:
     - Cambia el estado del LED de la Pico (“heartbeat” del sistema).
     - No se realiza lógica pesada en la interrupción.

6. **Bucle principal (polling + actualización de servos)**
   - En el `while(1)`:
     - Llama a `cyw43_arch_poll()` para mover la pila de red.
     - Revisa si la bandera de “nuevo dato” está activa:
       - Copia los valores de los dedos desde el buffer compartido.
       - Para cada dedo:
         - Aplica clamping (0–9).
         - Opcionalmente invierte el sentido si el montaje físico lo requiere.
         - Convierte el valor a un ancho de pulso (µs).
         - Llama a `servo_set_us(...)` para actualizar el canal apropiado.

---

## 5. Protocolo de comunicación (lado servidor)

- Transporte: **UDP/IPv4**.
- Puerto local del servidor: `4242`.

Tramas esperadas:

```text
H,v0,v1,v2,v3,v4
```

- `H`: cabecera para identificar el tipo de mensaje.
- `v0..v4`: valores enteros (`0–9`) que representan la flexión de cada dedo.

Si una trama es inválida (formato incorrecto, valores fuera de rango, etc.),
se puede descartar sin actualizar los servos.

---

## 6. Polling + IRQ en el servidor

- **IRQ**:
  - `repeating_timer` usado para generar un heartbeat con el LED.
  - Lógica mínima dentro de la interrupción.

- **Polling**:
  - El `while(1)` del `main`:
    - ejecuta `cyw43_arch_poll()` para gestionar la Wi-Fi.
    - revisa la bandera de “nuevo dato” para actualizar servos.
  - El callback de UDP solo mueve datos a un buffer y levanta la bandera;
    la actualización de servos se hace en el contexto del bucle principal.

---

## 7. Notas sobre problemas y decisiones

- Se detectaron congelamientos al usar la Pico W como **AP**;
  la solución fue utilizar un **hotspot externo** y trabajar siempre como STA.
- El protocolo no usa ACK ni reintentos:
  - Esto evita bloqueos por problemas de red.
  - El control es continuo: si se pierde un paquete, el siguiente corrige la posición.
- Algunas configuraciones (p. ej. inversión de un dedo, límites de µs)
  están pensadas para adaptarse al **montaje mecánico real** de la mano.
