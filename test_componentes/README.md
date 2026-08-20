# Test independiente de componentes HIRI

Este sketch prueba solamente los componentes básicos armados por separado:

- OLED SSD1306 128×64 en `0x3C`, con la misma configuración y rotación R2 del
  firmware principal
- RTC DS3231, I²C `0x68`
- SHT41, I²C `0x44`, temperatura y humedad con `Adafruit_SHT4x`
- NeoPixel RGB en GPIO 12
- botón 1 en GPIO 19
- botón 2 en GPIO 23
- voltaje de batería en ADC GPIO 35
- Plantower/PMS a 9600 baud, con la misma configuración del firmware principal
  (`SoftwareSerial RX=GPIO5, TX=GPIO18`)

No inicia el módem, GNSS, tarjeta SD, WiFi ni sensores de partículas.

## Uso

1. Abra `test_componentes/test_componentes.ino` en Arduino IDE.
2. Seleccione **ESP32 Dev Module**.
3. Instale las librerías **U8g2**, **RTClib**, **Adafruit NeoPixel**,
   **Adafruit SHT4x**, **Adafruit BusIO** y **Adafruit Unified Sensor** si faltan.
4. Compile, cargue el sketch y abra el monitor serie a **115200 baud**.
5. Compruebe que el OLED muestre el resumen y que el RGB recorra rojo, verde,
   azul, blanco y apagado.
6. Pulse BTN1: debe incrementar su contador y cambiar manualmente el color.
7. Pulse BTN2: debe incrementar su contador y activar/desactivar el ciclo RGB.

La OLED alterna automáticamente cada cinco segundos entre el resumen general y
una pantalla dedicada al SHT41. Esta segunda pantalla muestra temperatura,
humedad y número de serie; si el dispositivo no responde en `0x44`, muestra
`NO ENCONTRADO`. El sensor se lee cada dos segundos en alta precisión y con el
calentador desactivado.

La línea `PMS` muestra `ESPERANDO`, `OK` con PM1/PM2.5/PM10, o
`SIN DATOS/FAIL`. El encabezado cambia a `TEST: TODO OK*` cuando OLED, RTC,
SHT41, Plantower, batería y los dos botones respondieron, y el RTC no informa
pérdida de energía. `RTC:BAT!` significa que el DS3231 responde, pero perdió alimentación y
su hora podría ser incorrecta. El asterisco recuerda que el RGB debe comprobarse
visualmente: el microcontrolador puede enviarle colores, pero no confirmar que
el LED emitió luz.

La pantalla también muestra `BAT:x.xxV`. La lectura utiliza la misma fórmula,
promedio y calibración del firmware principal. Para el resultado general se
considera razonable un valor entre 2,50 V y 4,50 V; fuera de ese rango, el monitor
serie indica `REVISAR`.

La línea siguiente del PMS muestra también temperatura y humedad cuando el
modelo Plantower incluye esos campos. Si el sensor transmite partículas pero no
incluye T/H, aparece `PMS T/H:NO DISP.`.

El escáner I²C informa todas las direcciones encontradas. Un RTC con
`lostPower` sigue siendo detectado, pero muestra una advertencia y el test no
modifica su hora.

El monitor serie identifica `0x44` como el SHT41 esperado y entrega una lectura
con el formato `[SHT41] T=... C | HR=... %`. Una lectura ausente, fuera de rango
o con error impide que el encabezado cambie a `TEST: TODO OK*`.

Al arrancar, la OLED debe encender todos sus píxeles durante medio segundo y
luego mostrar `OLED ENCONTRADA`. Se usa el constructor SSD1306 y la rotación
`U8G2_R2` del firmware base para mantener la alineación de la pantalla original.

Los botones se leen como activos en HIGH usando las resistencias pull-down que
ya existen en la placa: sueltos muestran `UP` y pulsados muestran `DOWN`.

## Compilación con Arduino CLI

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries .\libraries .\test_componentes
```
