# FirmwarePro (HIRI)

Firmware para estación de monitoreo técnico-científico basada en ESP32, con adquisición de sensores, GNSS, registro en SD, telemetría HTTP y gestor de archivos por WiFi AP.

## Estado

- Plataforma objetivo: **ESP32 Dev Module** (`esp32:esp32:esp32`)
- Versión de referencia documentada: **Pro V0.0.35**

## Funcionalidades principales

- Lectura de **PMS5003** (PM1/PM2.5/PM10 + T/H según modelo)
- Lectura de **SDS198** (PM100)
- Lectura opcional de **SHT31**
- GNSS por módem SIM7600 (posición, altitud, velocidad, satélites, HDOP)
- Registro en **CSV diario** en tarjeta SD
- Envío HTTP de mediciones a backend
- UI local con OLED + 2 botones
- Modo **WiFi SD** con web manager (upload/download/rename/delete)
- Captive portal DNS para mejor compatibilidad en Windows

## Documentación

- **Manual de Usuario:** `documentacion/MANUAL_USUARIO.md`
- **Manual Técnico:** `documentacion/MANUAL_TECNICO.md`
- **PCB, pinout y revisión de hardware:** [`PCB/README.md`](PCB/README.md)
- **Historial de cambios:** `documentacion/CAMBIOS.md`
- **Guía de desarrollo/versionado:** `documentacion/DEVELOPER_GUIDELINES.md`
- **Estructura de menú:** `documentacion/menu_structure.md`
- **Licencia:** `LICENSE`

## Test independiente de componentes

El repositorio incluye el sketch
[`test_componentes/test_componentes.ino`](test_componentes/test_componentes.ino),
diseñado para revisar el hardware básico antes de cargar el firmware completo.
Esta prueba funciona de manera independiente y no inicia el módem, GNSS, WiFi,
tarjeta SD, SDS198 ni los servicios de telemetría.

### Componentes revisados

| Componente | Conexión usada | Prueba realizada |
|---|---|---|
| OLED SSD1306 128×64 | I²C `0x3C`, SDA 21, SCL 22 | Detección I²C, patrón visual y pantalla de estado |
| RTC DS3231 | I²C `0x68` | Presencia, hora, temperatura interna y pérdida de alimentación |
| Plantower/PMS | 9600 baud, RX 5, TX 18 | Trama, checksum, PM1, PM2.5, PM10 y T/H cuando el modelo las incluye |
| NeoPixel RGB | GPIO 12 | Secuencia rojo, verde, azul, blanco y apagado |
| Botón 1 | GPIO 19, activo en HIGH | Estado, antirrebote y contador de pulsaciones |
| Botón 2 | GPIO 23, activo en HIGH | Estado, antirrebote y contador de pulsaciones |
| Batería | ADC GPIO 35 | Promedio filtrado y voltaje calibrado |

Los botones utilizan las resistencias pull-down externas de la placa. Por eso se
configuran como `INPUT`: sueltos se leen en LOW y pulsados en HIGH.

### Información mostrada

La OLED utiliza el mismo constructor SSD1306, fuente y rotación `U8G2_R2` del
firmware principal. Al arrancar muestra un patrón de pantalla y luego un resumen
similar al siguiente:

```text
TEST COMPONENTES HIRI
O:3C R:OK 12:34:56
PMS:1/25/10 5/12/18
PMS T/H:23.4C 45.2%
RGB:VERDE BAT:4.08V
B1:UP #0    B2:UP #0
```

- `PMS:ESPERANDO` indica que todavía no llegó una trama.
- `PMS:SIN DATOS/FAIL` indica ausencia de tramas válidas o pérdida de datos.
- `PMS T/H:NO DISP.` indica que el Plantower transmite partículas, pero su
  modelo no incluye temperatura y humedad válidas.
- `R:BAT!` indica que el RTC responde, pero registró una pérdida de alimentación
  y su hora podría ser incorrecta. El test nunca modifica la hora del RTC.
- `B1` y `B2` muestran `UP` al soltar, `DOWN` al pulsar y el total de
  pulsaciones válidas.

El monitor serie a **115200 baud** entrega además el escaneo I²C, cada trama
válida del Plantower, errores de checksum, hora y temperatura del RTC, cambios
del RGB, pulsaciones y voltaje de batería.

El encabezado cambia a `TEST: TODO OK*` cuando OLED, RTC, Plantower, batería y
ambos botones superan las verificaciones automáticas. Para la batería se acepta
como rango diagnóstico de una celda conectada entre 2,50 V y 4,50 V. El
asterisco recuerda que el RGB debe comprobarse visualmente: el ESP32 puede
ordenar un color, pero no medir si el LED realmente emitió luz.

### Controles del test

- **BTN1:** avanza manualmente al siguiente color del NeoPixel.
- **BTN2:** activa o desactiva el recorrido automático de colores.

### Uso del test

1. Abra `test_componentes/test_componentes.ino` en Arduino IDE.
2. Seleccione **ESP32 Dev Module**.
3. Use las dependencias incluidas en [`libraries`](libraries/README.md).
4. Cargue el sketch y abra el monitor serie a **115200 baud**.
5. Revise el patrón OLED, los colores y los valores mostrados.
6. Pulse ambos botones al menos una vez y confirme que sus estados y contadores
   cambien correctamente.

Con Arduino CLI, la ruta de librerías debe indicarse explícitamente:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries .\libraries .\test_componentes
```

## Compilación y carga (Arduino CLI)

Las dependencias externas están consolidadas en `libraries/`. Consulte
[`libraries/README.md`](libraries/README.md) para ver versiones y licencias.

### Compilar

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries ./libraries .
```

### Subir a dispositivo (ejemplo COM5)

```bash
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32 FirmwarePro.ino
```

## Disclaimer de responsabilidad

Este proyecto se entrega **"tal cual"** (as-is), sin garantías explícitas ni implícitas de funcionamiento, disponibilidad o aptitud para un propósito específico.

El despliegue en terreno, la seguridad del sistema, la validación de datos y el cumplimiento normativo son responsabilidad del usuario/integrador.

Los autores y colaboradores no se responsabilizan por pérdidas de datos, daños directos o indirectos ni por uso fuera de contexto técnico seguro.

## Licencia de documentación

La documentación de este repositorio se publica bajo:

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**  
https://creativecommons.org/licenses/by-nc/4.0/

- ✅ Se permite compartir y adaptar con atribución.
- ❌ No se permite uso comercial sin autorización adicional.
