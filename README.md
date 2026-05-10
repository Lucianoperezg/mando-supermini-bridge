# Mando SuperMini Bridge

Bridge HomeKit para control de ventilador de techo con luz, basado en **ESP32-S3 SuperMini** y transceptor RF **CC1101** a 433 MHz. Integra el ventilador en Apple Home mediante la librería [HomeSpan](https://github.com/HomeSpan/HomeSpan).

---

## Características

| Accesorio HomeKit | Controles |
|---|---|
| 💡 Luz del ventilador | Encendido/apagado, brillo (0–100 %), temperatura de color (en mireds) |
| 🌀 Ventilador | Encendido/apagado, velocidad (1–6) |
| 🔄 Giro | Modo verano (hacia arriba) / invierno (hacia abajo) |
| ⏹ Apagar todo | Switch momentáneo que apaga luz y ventilador |

El estado de cada accesorio se guarda en la flash (NVS) y se restaura automáticamente tras un reinicio.

---

## Hardware requerido

- **Placa:** ESP32-S3 SuperMini (o cualquier ESP32-S3 con acceso a los pines indicados)
- **Transceptor RF:** Módulo CC1101 (433.920 MHz, ASK/OOK, 38.4 kbps)
- Cable micro-USB / USB-C para programación

---

## Diagrama de conexiones

| CC1101 | ESP32-S3 SuperMini |
|---|---|
| CSN | GPIO 10 |
| SCK | GPIO 12 |
| MOSI | GPIO 11 |
| MISO | GPIO 13 |
| GDO0 | GPIO 9 |
| VCC | 3.3 V |
| GND | GND |

---

## Configuración de radio

| Parámetro | Valor |
|---|---|
| Frecuencia | 433.920 MHz |
| Modulación | ASK/OOK |
| Baud rate | 38 400 bps |
| Resolución RMT | 1 MHz (1 tick = 1 µs) |

IDs de ventilador incluidos en el firmware:

```
FAN_ID_ORIGINAL = 0x2BF6B
FAN_ID_NUEVO    = 0x0511E  ← emparejado por defecto
```

---

## Dependencias y entorno

El proyecto usa **PlatformIO** con Arduino framework sobre IDF 5.x.

| Dependencia | Versión |
|---|---|
| [HomeSpan](https://github.com/HomeSpan/HomeSpan) | ^2.1.1 |
| platform-espressif32 (pioarduino) | 55.03.37 |

---

## Instalación

### 1. Clonar el repositorio

```bash
git clone https://github.com/Lucianoperezg/mando-supermini-bridge.git
cd mando-supermini-bridge
```

### 2. Instalar PlatformIO

Si aún no lo tienes:

```bash
pip install platformio
```

O instala la extensión **PlatformIO IDE** en VS Code.

### 3. Compilar y flashear

```bash
pio run --target upload
```

### 4. Monitorizar la consola serie

```bash
pio device monitor
```

Velocidad: 115 200 bps.

---

## Emparejar con Apple Home

1. Abre la app **Casa** en iOS / macOS.
2. Toca **+** → **Añadir accesorio**.
3. Escanea el código QR que aparece en la consola serie, o introduce el código de emparejamiento de HomeSpan manualmente.
4. Los cuatro accesorios (luz, ventilador, giro y apagar todo) aparecerán en tu hogar.

---

## Estructura del proyecto

```
mando-supermini-bridge/
├── include/
│   └── fan_protocol.h    # Driver CC1101 + protocolo RF del ventilador
├── src/
│   └── main.cpp          # Lógica HomeKit (HomeSpan), accesorios y servicios
├── partitions_4mb.csv    # Tabla de particiones para flash de 4 MB
└── platformio.ini        # Configuración de PlatformIO
```

---

## Licencia

Este proyecto se distribuye bajo los términos de la licencia **MIT**. Consulta el archivo `LICENSE` para más información.
