# Librerías incluidas en el repositorio

Esta carpeta conserva las dependencias externas del firmware HIRI y del sketch
`test_componentes`. Se incluyen los fuentes necesarios, metadatos y archivos de
licencia para que el proyecto no dependa de la versión más reciente del gestor
de librerías de Arduino.

## Versiones consolidadas

| Carpeta | Librería Arduino | Versión | Uso principal |
|---|---|---:|---|
| `U8g2` | U8g2 | 2.34.22 | OLED SSD1306 |
| `RTClib` | RTClib | 2.1.4 | RTC DS3231 |
| `Adafruit_NeoPixel` | Adafruit NeoPixel | 1.12.5 | LED RGB |
| `Adafruit_SHT31_Library` | Adafruit SHT31 Library | 2.2.2 | Sensor SHT31 |
| `Adafruit_BusIO` | Adafruit BusIO | 1.17.4 | Dependencia de RTClib y SHT31 |
| `EspSoftwareSerial` | EspSoftwareSerial | 8.1.0 | UART por software del Plantower |
| `TinyGSM` | TinyGSM | 0.12.0 | Módem SIM7600 |
| `OneButton` | OneButton | 2.5.0 | Interfaz de botones del firmware principal |

Cada subcarpeta mantiene el archivo de licencia distribuido por su autor. Estas
librerías no son código propio del proyecto HIRI y conservan sus licencias
originales.

## Arduino CLI

Desde la raíz del repositorio, indicar explícitamente esta carpeta mediante
`--libraries`. Para el test de componentes:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries .\libraries .\test_componentes
```

Para el firmware principal se usa el mismo argumento `--libraries` con la ruta
del sketch correspondiente.

## Arduino IDE

Arduino IDE no busca automáticamente librerías dentro de cualquier repositorio.
Se puede usar una de estas alternativas:

1. Configurar temporalmente la raíz de este repositorio como **Sketchbook
   location** en Preferencias. Arduino reconocerá su subcarpeta `libraries`.
2. Copiar las ocho carpetas individuales a la carpeta `libraries` del sketchbook
   habitual, normalmente `Documentos\Arduino\libraries`.

Si ya existe otra versión instalada globalmente, revise la salida detallada de
Arduino para confirmar qué ruta eligió. Para compilaciones reproducibles se
recomienda Arduino CLI con `--libraries`.

## Dependencias proporcionadas por ESP32

No se duplican `Arduino`, `Wire`, `SPI`, `FS`, `SD`, `WiFi`, `WebServer`,
`DNSServer`, `Preferences` ni `esp_task_wdt`, porque forman parte del paquete de
placa ESP32. El proyecto de referencia utiliza ESP32 Arduino Core 2.0.17.
