# Mimic Hand – Mano robótica teleoperada con Raspberry Pi Pico W

Proyecto de una mano robótica mimética controlada mediante un guante instrumentado, usando **dos Raspberry Pi Pico W** que se comunican por **Wi-Fi (UDP)**.

- **GUANTE (cliente)**: lee la flexión de los dedos con sensores Hall + ADC.  
- **MANO (servidor)**: recibe los datos por UDP y mueve servomotores vía PCA9685.  
- **Red**: ambos Picos conectados como **STA** a un **hotspot de celular**, no se usa el Pico como AP.

Este README está pensado para el repositorio Git del proyecto.

---

## 1. Resumen del funcionamiento

1. El **Pico de la mano**:
   - Se conecta al hotspot.
   - Imprime su IP en el monitor serie.
   - Inicia el driver PCA9685 (I²C) y coloca los servos en posición inicial.
   - Crea un servidor UDP en el puerto `4242`.
   - Cuando recibe tramas del guante, convierte los valores discretos de cada dedo a anchos de pulso y actualiza los servos.

2. El **Pico del guante**:
   - Se conecta al mismo hotspot.
   - Inicia ADC + multiplexor de sensores Hall.
   - Usa un **timer en IRQ** para marcar cada cuánto enviar datos.
   - En el bucle principal (polling), cuando toca enviar:
     - Lee los 5 dedos.
     - Normaliza cada uno a un rango discreto `0–9`.
     - Forma una trama `H,v0,v1,v2,v3,v4`.
     - La manda por UDP a la IP de la mano.

3. El sistema está optimizado para:
   - Movimiento continuo.  
   - Baja latencia.  
   - Tolerancia a pérdida de paquetes (no hay ACK ni reintentos).

---

## 2. Estructura del repositorio


```text
.
├─ Pico_Client/
│  ├─ lib/
│  │   └─ guante/
│  │      ├─ guante.h
│  │      └─ guante.c
│  ├─ Pico_Client.c        
│  ├─ lwipopts.h
│  ├─ CMakeList.txt
│  └─ README.md
│
├─ Pico_Server/
│  ├─ lib/   
|  │   └─ servo/
│  │      ├─ servo.h
│  │      └─ servo.c
│  ├─ Pico_server.c        
│  ├─ lwipopts.h
│  ├─ CMakeList.txt
│  └─ README.md
│  
└─ README.md
```

---

## 3. Arquitectura de hardware

### Guante (cliente)

- Raspberry Pi Pico W.  
- 5× sensores Hall (uno por dedo) + imanes.  
- Multiplexor analógico para seleccionar qué sensor leer.  
- ADC0 en GPIO26 para medir el voltaje del sensor seleccionado.  
- Alimentación compartida para Pico + sensores.

### Mano robótica (servidor)

- Raspberry Pi Pico W.  
- Driver PCA9685.  
- Servomotores conectados a los canales del PCA9685.  
- Fuente de servos independiente, con **tierra común** con la lógica.

### Red

- Hotspot de un teléfono.  
- Ambos Picos como **STA**:
  - El hotspot asigna IP por DHCP.
  - El servidor imprime su IP (`IP_SERVER`).
  - El cliente la usa como `SERVER_IP` para los paquetes UDP.

---

## 4. Arquitectura de software

### 4.1. `Pico_Server.c` (MANO – Servidor UDP)

Responsabilidades:

- Inicializa:
  - stdio/USB para logs,  
  - Wi-Fi en modo STA,  
  - conexión al hotspot (SSID y contraseña configurables).
- Imprime por serial la **IP del servidor** (para configurarla en el guante).
- Inicializa el PCA9685 vía `servo_init(...)` y sitúa los servos en posición segura.
- Crea un **servidor UDP**:
  - `udp_new_ip_type`, `udp_bind`, `udp_recv`.
- Callback de recepción UDP:
  - Recibe tramas de texto: `H,v0,v1,v2,v3,v4`.
  - Valida número de campos y rango (`0–9`).
  - Actualiza un búfer de valores de dedos + una bandera de “nuevo dato”.
  - Imprime la trama y el conteo de paquetes recibidos.
- Bucle principal (polling):
  - Llama regularmente a `cyw43_arch_poll()` (mover la pila lwIP).
  - Si hay nuevos datos: copia los valores y llama a la lógica que:
    - convierte `0–9` a un ancho de pulso en microsegundos,
    - llama a `servo_set_us()` para cada dedo.
- Timer en IRQ:
  - `repeating_timer` que solo parpadea el LED integrado como “heartbeat” como indicador de conexión.

### 4.2. `Pico_Client.c` (GUANTE – Cliente UDP)

Responsabilidades:

- Inicializa:
  - stdio/USB,  
  - Wi-Fi en modo STA,  
  - conexión al mismo hotspot.
- Inicializa el guante (`guante_init()`):
  - ADC,  
  - pines del multiplexor.
- Crea un **cliente UDP** conectado a `SERVER_IP:4242`.
- Configura un **timer en interrupción**:
  - Un `repeating_timer` que cada X ms levanta una bandera `flag_timer_send`.
- Bucle principal (polling):
  - Llama a `cyw43_arch_poll()`.
  - Cuando `flag_timer_send` está activa:
    - La limpia.
    - Llama a `guante_leer_dedos(...)` para obtener los 5 valores normalizados `0–9`.
    - Forma una trama ASCII `H,v0,v1,v2,v3,v4`.
    - Usa `udp_send` para transmitirla.
    - Imprime en consola la trama enviada y el número de paquete.

### 4.3. `lib/servo/servo.h` – `servo.c`

Responsabilidades:

- Encapsula el acceso al PCA9685:
  - Configura dirección I²C, frecuencia de PWM, modo Auto-Increment.
  - Calcula el prescaler adecuado para la frecuencia deseada.
- Proporciona funciones de alto nivel:
  - `servo_init(servo_pca_t *dev)` – setup completo del PCA9685.
  - `servo_set_us(dev, canal, ancho_us)` – asignar un pulso en microsegundos a un canal.
- Internamente:
  - Convierte µs → cuentas de 12 bits (0–4095).
  - Aplica límites de seguridad (`SERVO_US_MIN`, `SERVO_US_MAX`).

### 4.4. `lib/guante/guante.h` – `guante.c`

Responsabilidades:

- Configura ADC y pines del MUX (selección de dedo).
- Para cada dedo:
  - Selecciona el canal del MUX.
  - Lee el ADC (valor crudo).
  - Lo convierte a voltaje (para depuración).
  - Lo mapea a un rango discreto `0–9` usando umbrales globales `RAW_MIN` / `RAW_MAX`.
- Ofrece una API simple:
  - `guante_init()` – inicialización de hardware.
  - `guante_leer_dedos(uint8_t out[5])` – rellena el arreglo con valores `0–9`.
- Durante la fase de calibración:
  - Imprime por consola las tensiones de cada dedo en una sola línea para poder observar mínimos y máximos.

---

## 5. Protocolo de comunicación

- Transporte: **UDP** sobre IPv4.  
- Puerto: `4242`.  
- Servidor: IP asignada por el hotspot (p. ej. `172.20.10.2`).  
- Cliente: IP también asignada por el hotspot (p. ej. `172.20.10.3`.

Formato de trama (texto ASCII):

```text
H,v0,v1,v2,v3,v4
```

- `H` → identificador de cabecera.  
- `v0..v4` → enteros `0–9` (flexión de cada dedo ya normalizada).

Características del protocolo:

- No hay ACK, ni números de secuencia, ni retransmisión.  
- Si se pierde un paquete, simplemente se usa el siguiente estado.  
- Diseño intencional: priorizar movimiento fluido y baja latencia frente a fiabilidad absoluta.

---

## 6. Problemas importantes y soluciones

### 6.1. Pico como AP → congelamientos de la red

**Problema:**

- En las primeras versiones, la Pico de la mano funcionaba como **punto de acceso (AP)**.
- Tras cierto número de paquetes, el receptor:
  - Dejaba de entrar al callback de UDP.
  - Se quedaba con `packet_count` fijo.
  - El programa no se colgaba por completo, pero la recepción de datos sí.

**Causa probable:**

- Limitaciones / bugs del stack de red en modo AP del Pico W (buffers, manejo de colas, etc.).

**Solución:**

- Cambiar la topología:
  - Hotspot de celular como **AP externo**.
  - Ambos Picos como **STA**.
- Resultado:
  - Desaparecen los congelamientos de recepción.
  - La comunicación se vuelve estable a largo plazo.

---

### 6.2. Watchdog + conexión Wi-Fi bloqueante

**Problema:**

- Se habilitó el watchdog con timeout de ~8 s.
- La función de conexión Wi-Fi (`cyw43_arch_wifi_connect_timeout_ms`) puede bloquear hasta ~15 s.
- Al no llamar a `watchdog_update()` durante ese tiempo, la Pico se reiniciaba.
- En el monitor serie se veía que intentaba conectar y el puerto se cerraba una y otra vez.

**Solución:**

- Desactivar el watchdog durante la fase de conexión Wi-Fi.
- (Opcional futuro) volver a activarlo, pero solo después de estar conectados, refrescándolo en el bucle principal.

---

### 6.3. Lógica de sensores invertida (7 abierto, 0 cerrado)

**Problema:**

- El hardware entregaba valores normalizados con “signo” opuesto:
  - Mano abierta → valor alto.
  - Mano cerrada → valor bajo.
- Para la mano robótica era más natural:
  - `0` → dedo abierto.
  - valor máximo → dedo cerrado.

**Solución:**

- Mantener la normalización cruda `0–9` y luego invertir donde corresponde:

  ```c
  nivel_invertido = MAX - nivel;
  ```

- Esta inversión se hace en la lógica de la mano para el dedo que lo requiera, de forma transparente para el resto del código.

---

### 6.4. Calibración de sensores Hall

**Problema:**

- Cada sensor Hall presenta un rango de tensiones distinto entre:
  - mano totalmente abierta,
  - mano totalmente cerrada.
- Sin calibración, el mapeo `raw → 0–9` podría:
  - saturarse antes del final del recorrido,
  - no usar toda la resolución.

**Solución:**

- Añadir impresión de voltajes por dedo durante las pruebas:
  - Ejemplo:  
    `Vdedos: 0.842, 0.910, 1.005, 0.732, 0.695`
- Con esos datos se pueden seleccionar `RAW_MIN` / `RAW_MAX` conservadores por dedo y refinar el mapeo.
- La librería ya está estructurada para poder reemplazar los umbrales globales por arrays por dedo si se desea.

---

## 7. Polling + IRQ en el diseño

El proyecto combina explícitamente **polling** + **interrupciones**:

- **En el GUANTE (cliente):**
  - IRQ:
    - Timer (`repeating_timer`) que marca los instantes de muestreo/envío levantando una bandera.
  - Polling:
    - Bucle principal revisa esa bandera, lee sensores, arma trama y envía UDP.
    - `cyw43_arch_poll()` mueve la pila de red.

- **En la MANO (servidor):**
  - IRQ:
    - Timer (`repeating_timer`) usado como “heartbeat” para el LED.
  - Polling:
    - Bucle principal llama a `cyw43_arch_poll()`.
    - Comprueba si hay nuevos datos desde el callback UDP y, si los hay, actualiza servos.

Esta separación deja la lógica pesada (ADC, Wi-Fi, UDP, I²C, servos) fuera de las IRQ y cumple el requisito académico de usar ambos mecanismos.

---
