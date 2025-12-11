# Mimic Hand – Guante (Cliente UDP – Raspberry Pi Pico W)

Este módulo implementa el **lado cliente** del proyecto Mimic Hand.  
Corresponde al **guante instrumentado**, que lee la flexión de los dedos
mediante sensores Hall y envía esa información a la mano robótica a
través de **Wi-Fi (UDP)** usando una Raspberry Pi Pico W.

---

## 1. Rol dentro del sistema

- Leer el estado de los **5 dedos** usando:
  - Sensores Hall.
  - Un multiplexor analógico.
  - El ADC de la Pico W.
- Normalizar cada dedo a un valor discreto en el rango `0–9`.
- Formar una trama ASCII de la forma:

  ```text
  H,v0,v1,v2,v3,v4
  ```

- Enviar continuamente esta trama por **UDP** al servidor (mano robótica).
- Trabajar con **polling + interrupciones (IRQs)**:
  - IRQ para marcar el instante de muestreo/envío.
  - Polling en el bucle principal para Wi-Fi y lógica de aplicación.

---

## 2. Hardware asociado

- Raspberry Pi Pico W.
- 5× sensores Hall (uno por dedo) con su respectivo imán.
- Multiplexor analógico para seleccionar el sensor a leer.
- ADC0 (GPIO26) como entrada analógica.
- Conexión Wi-Fi:
  - El Pico se conecta como **STA** a un **hotspot de celular**.
  - El hotspot asigna IP por DHCP.
  - La IP del servidor (mano) se configura como `SERVER_IP` en el código.

---

## 3. Archivos relacionados

- `src/Pico_Client.c`  
  Programa principal del guante:
  - Inicialización de Wi-Fi.
  - Inicialización del guante.
  - Envío de tramas por UDP.
  - Lógica de polling + IRQ.

- `lib/guante/guante.h`  
  - API de alto nivel para el guante:
    - `guante_init()`
    - `guante_leer_dedos(uint8_t out[5])`

- `lib/guante/guante.c`  
  Implementación:
  - Configuración de ADC.
  - Manejo de pines del multiplexor.
  - Lectura de cada dedo.
  - Normalización al rango `0–9`.
  - (Modo calibración) impresión de voltajes de cada dedo.

---

## 4. Flujo de ejecución del cliente

1. **Inicio y conexión Wi-Fi**
   - Inicializa `stdio` para depuración por USB.
   - Configura la Pico W en modo **STA**.
   - Se conecta al hotspot usando SSID y contraseña definidos en el código.
   - Una vez conectado, muestra por consola la IP asignada.

2. **Inicialización del guante**
   - Llama a `guante_init()`:
     - Configura el ADC.
     - Configura los pines del MUX.
   - El guante queda listo para leer los 5 dedos.

3. **Configuración UDP**
   - Crea un PCB UDP y lo conecta a `SERVER_IP:4242`, donde está escuchando
     el servidor (mano robótica).

4. **Configuración de IRQ (timer)**
   - Crea un `repeating_timer` que se dispara cada cierto periodo (ej. 250 ms).
   - En el callback del timer:
     - Solo se levanta una bandera `flag_timer_send`.
     - No se hace lógica pesada en la interrupción.

5. **Bucle principal (polling + envío)**
   - Llama periódicamente a `cyw43_arch_poll()` para avanzar la pila de red.
   - Revisa si `flag_timer_send` está activa:
     - Si lo está:
       - La baja.
       - Llama a `guante_leer_dedos(...)` para obtener los valores `0–9` de los 5 dedos.
       - Forma la trama ASCII `H,v0,v1,v2,v3,v4`.
       - Envía la trama usando `udp_send`.
       - Imprime por consola la trama enviada y el número de paquete.

---

## 5. Protocolo de datos (lado cliente)

- Transporte: **UDP/IPv4**.
- Puerto destino: `4242`.
- Dirección destino: `SERVER_IP` (IP del servidor, configurada en el código).

Formato de la trama:

```text
H,v0,v1,v2,v3,v4
```

- `H`: carácter de cabecera.
- `v0..v4`: valores enteros (`0–9`) de la flexión de cada dedo, ya normalizados.

---

## 6. Polling + IRQ en el cliente

- **IRQ**:
  - Un timer `repeating_timer` genera interrupciones periódicas.
  - El callback solo levanta `flag_timer_send`.

- **Polling**:
  - El `while(1)` del `main` hace:
    - `cyw43_arch_poll()` para la Wi-Fi.
    - Polling de `flag_timer_send` para decidir cuándo leer sensores y enviar datos.

Este diseño cumple con el requisito de usar interrupciones para marcar eventos
de tiempo y polling para el resto de la lógica (Wi-Fi, construcción y envío de tramas).

---

## 7. Notas sobre problemas y decisiones

- Se abandonó el uso de la Pico W como **AP** por problemas de
  congelamiento en la recepción.
- La configuración final usa un **hotspot externo** (celular), con ambos
  Picos en modo STA, lo que resultó ser mucho más estable.
- Se decidió usar un protocolo simple sin ACK ni números de secuencia:
  - Si se pierde un paquete, el siguiente estado corrige la posición de la mano.
  - Esto simplifica la lógica y reduce la latencia.
