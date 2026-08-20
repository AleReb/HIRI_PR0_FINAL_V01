# Hardware PCB de HIRI Pro

Esta carpeta contiene la placa portadora (*carrier board*) correspondiente al
firmware principal [`HIRI_PR0_FINAL_V01.ino`](../HIRI_PR0_FINAL_V01.ino). El
diseño aloja el módulo ESP32/SIM7600 mediante dos hileras de 15 pines y distribuye
alimentación y señales hacia el sensor Plantower, pantalla, RTC, LED RGB y dos
botones.

> **Estado de la revisión (19-08-2026): no enviar todavía a fabricación.** El
> archivo de placa contiene cuatro conexiones sin rutear en `3V3`; además, deben
> resolverse las observaciones de compatibilidad con el firmware indicadas más
> abajo y ejecutar ERC/DRC en EAGLE.

## Archivos del diseño

| Archivo | Contenido |
|---|---|
| [`HiriPro/HirigrandeCHINA V2.1.sch`](HiriPro/HirigrandeCHINA%20V2.1.sch) | Esquemático eléctrico |
| [`HiriPro/HirigrandeCHINA V2.1.brd`](HiriPro/HirigrandeCHINA%20V2.1.brd) | Layout de la PCB |

Los dos archivos son XML de **Autodesk EAGLE 9.6.2**. El repositorio no incluye
Gerbers, archivos de perforado, *pick and place* ni BOM de fabricación; estos
deben exportarse únicamente después de cerrar la revisión eléctrica y el DRC.

## Bloques de hardware

- `JP1` y `JP2`: cabezales 1x15 del módulo ESP32/SIM7600.
- `XA1`: conector Molex 53048-0810 de ocho posiciones para Plantower/PMS.
- `STEPUP`: cabecera de tres pines para elevador `VBAT` a `5V`.
- `RTC`: conector JST-XH de cinco posiciones para DS3231.
- `SCREEN`: conector JST-SH para la pantalla OLED I²C.
- `I2C`: conector JST-SH de expansión I²C; permite conectar, por ejemplo, el
  SHT31 utilizado opcionalmente por el firmware.
- `RGB`: conector JST-SH para NeoPixel externo.
- `U$1` y `U$2`: pulsadores Cherry MX, con resistencias pull-down de 10 kΩ.
- `C1` a `C10`: posiciones SMC_D para condensadores polarizados. Sus valores no
  están definidos en el esquemático y deben completarse antes de generar la BOM.

La PCB es de dos capas: `Top` y `Bottom`. Las reglas guardadas especifican pista
mínima de 0,4 mm, perforación mínima de 0,35 mm y separación pista-pista de
0,3 mm. Estos valores describen el archivo, pero no sustituyen la comprobación
contra las capacidades del fabricante elegido.

## Pinout y relación con el firmware

### Señales principales del ESP32

| Función | GPIO efectivo | Red o conexión PCB | Definición en firmware | Revisión |
|---|---:|---|---|---|
| Módem RX | 26 | Pin del módulo en `JP1` | `MODEM_RX` | Coincide |
| Módem TX | 27 | Pin del módulo en `JP1` | `MODEM_TX` | Coincide |
| Módem PWRKEY | 4 | Pin del módulo en `JP1` | `MODEM_PWRKEY` | Coincide |
| Módem DTR | 32 | Pin del módulo en `JP1` | `MODEM_DTR` | Coincide |
| Módem FLIGHT | 25 | Pin del módulo en `JP1` | `MODEM_FLIGHT` | Coincide |
| PMS: recepción del ESP32 | 5 | `JP2.7` (`RX2`) ↔ `XA1.5` | Uso efectivo de `SoftwareSerial` | Coincide, pero los nombres de macros inducen a error |
| PMS: transmisión del ESP32 | 18 | `JP2.6` (`TX2`) ↔ `XA1.4` | Uso efectivo de `SoftwareSerial` | Coincide, pero los nombres de macros inducen a error |
| I²C SDA | 21 | `JP2.10`, `RTC.4`, `SCREEN.1`, `I2C.1` | `Wire` por defecto | Coincide |
| I²C SCL | 22 | `JP2.11`, `RTC.3`, `SCREEN.2`, `I2C.2` | `Wire` por defecto | Coincide |
| NeoPixel | 12 | `JP1.14` (`RGB`) ↔ `RGB.1` | `NEOPIX_PIN` | Coincide |
| Botón 1 | 19 | `JP2.9` (`BOT1`) | `BUTTON_PIN_1` | GPIO coincide; lógica eléctrica por corregir |
| Botón 2 | 23 | `JP2.8` (`BOT2`) | `BUTTON_PIN_2` | GPIO coincide; lógica eléctrica por corregir |
| SD SCLK | 14 | Pin del módulo en `JP1` | `SD_SCLK` | Coincide |
| SD MISO | 2 | Pin del módulo en `JP1` | `SD_MISO` | Coincide |
| SD MOSI | 15 | Pin del módulo en `JP1` | `SD_MOSI` | Coincide |
| SD CS | 13 | Pin del módulo en `JP1` | `SD_CS` | Coincide |
| Medición de batería | 35 | Sensado propio del módulo | `BAT_PIN` | No está implementado como una red separada en esta carrier |
| SDS198 RX | 39 | Conexión externa al módulo | `Serial2.begin(..., 39, -1)` | No existe conector dedicado en esta PCB |

En [`config.h`](../config.h), las macros del PMS se llaman `pms_RX=18` y
`pms_TX=5`, pero el objeto se construye en orden invertido:
`SoftwareSerial pms(pms_TX, pms_RX)`. Por eso la configuración efectiva del
ESP32 es **RX=GPIO5 y TX=GPIO18**, que es la que debe usarse al comprobar la
placa. Conviene renombrar esas macros en una futura revisión para eliminar la
ambigüedad.

`POWER_PIN` está definido como GPIO33, pero su uso se encuentra comentado en el
firmware principal. La red GPIO33 de la placa no controla actualmente una carga
desde el programa.

### Conectores periféricos

Las referencias siguientes usan el número de pad guardado en EAGLE. Antes de
armar cables, confirme también la orientación del conector y la marca de pin 1
en la vista de placa.

| Conector | Pad | Señal |
|---|---:|---|
| `XA1` Plantower | 8 | `5V`, salida del step-up |
|  | 7 | `GND` |
|  | 6 | `SET`, sin conexión al ESP32 |
|  | 5 | `RX2`; datos del sensor hacia RX del ESP32 (GPIO5) |
|  | 4 | `TX2`; datos del ESP32 (GPIO18) hacia RX del sensor |
|  | 1–3 | Sin conexión |
| `RTC` | 1 | `GND` |
|  | 2 | Sin conexión |
|  | 3 | `SCL` |
|  | 4 | `SDA` |
|  | 5 | `3V3` |
| `SCREEN` | 1 | `SDA` |
|  | 2 | `SCL` |
|  | 3 | `GND` |
|  | 4 | `VBAT` |
|  | 5–6 | Blindaje a `GND` |
| `I2C` | 1 | `SDA` |
|  | 2 | `SCL` |
|  | 3 | `GND` |
|  | 4 | `VBAT` |
|  | 5–6 | Blindaje a `GND` |
| `RGB` | 1 | Datos, GPIO12 |
|  | 2 | `GND` |
|  | 3 | `VBAT` |
|  | S1–S2 | Blindaje a `GND` |
| `STEPUP` | 1 | Entrada `VBAT` |
|  | 2 | `GND` |
|  | 3 | Salida `5V` hacia `XA1.8` |

**Atención:** `SCREEN`, `I2C` y `RGB` reciben `VBAT`, no `3V3` regulados. Antes
de conectar un módulo, compruebe su rango de alimentación y el voltaje máximo de
la batería. Las líneas lógicas del ESP32 siguen siendo de 3,3 V.

## Hallazgos pendientes

### 1. Cuatro conexiones `3V3` sin rutear

El `.brd` conserva cuatro segmentos en la capa EAGLE `19 Unrouted`, todos en la
red `3V3`:

| Desde (mm) | Hasta (mm) |
|---|---|
| `(48.625, 19.625)` | `(48.625, 19.6893)` |
| `(48.750, 10.175)` | `(48.750, 10.2500)` |
| `(34.450, 14.900)` | `(34.425, 14.9500)` |
| `(34.600, 19.625)` | `(34.500, 19.5250)` |

Aunque son segmentos muy cortos, EAGLE todavía los considera *airwires*. Se
deben rutear o resolver mediante el plano de cobre y luego ejecutar `RATSNEST`
hasta obtener **0 unrouted**.

### 2. Lógica de botones incompatible

En el esquemático, cada pulsador conecta su señal a `3V3` y tiene una resistencia
externa de 10 kΩ a `GND`: queda suelto en LOW y pulsado en HIGH. Sin embargo, el
firmware principal configura ambos GPIO como `INPUT_PULLUP` y atiende el flanco
`FALLING`. Esto puede registrar la liberación en vez de la pulsación y enfrenta
el pull-up interno con el pull-down externo.

Debe elegirse y probarse una solución antes de liberar hardware/firmware:

- conservar la PCB y usar entradas normales o pull-down, con interrupción
  `RISING`; o
- conservar la lógica activa en LOW del firmware y modificar el circuito para
  que el pulsador conecte a GND con pull-up.

El sketch [`test_componentes/test_componentes.ino`](../test_componentes/test_componentes.ino)
ya prueba la primera topología: entradas activas en HIGH con los pull-down de la
placa.

### 3. BOM incompleta

`C1` a `C10` usan encapsulado polarizado SMC_D, pero no tienen capacitancia ni
tensión nominal asignadas. También debe registrarse el modelo exacto del módulo
step-up. No sustituya componentes ni genere una orden de montaje hasta definir
estos datos y verificar polaridades.

### 4. Periféricos que no tienen conector dedicado

El firmware lee un SDS198 por GPIO39 y puede leer un SHT31 en `0x44`. La carrier
no ofrece un conector rotulado para SDS198; el SHT31 puede compartir `I2C`, pero
el conector lo alimenta desde `VBAT`. Estas conexiones deben quedar definidas en
el mazo de cables y en la matriz de versión del equipo.

## Procedimiento de validación recomendado

1. Abrir juntos el `.sch` y el `.brd` en EAGLE 9.6.2 o una versión compatible.
2. Completar valores y números de parte de la BOM.
3. Resolver las cuatro conexiones `3V3` y confirmar `RATSNEST: 0 unrouted`.
4. Ejecutar ERC sobre el esquemático y DRC sobre la placa usando las reglas del
   fabricante; documentar todas las excepciones.
5. Resolver la topología de los botones y sincronizarla con el firmware.
6. Verificar orientación y continuidad de cada conector con un multímetro, sin
   insertar todavía el ESP32 ni los sensores.
7. Alimentar con fuente limitada en corriente y comprobar `VBAT`, `3V3` y la
   salida `5V` del step-up.
8. Insertar el módulo y ejecutar el
   [`test_componentes`](../test_componentes/README.md) antes del firmware completo.
9. Validar OLED, RTC, PMS, RGB, botones y batería; después probar SD, módem,
   GNSS, SDS198 y telemetría con el firmware principal.
10. Solo entonces exportar Gerbers/Excellon y revisar visualmente cada capa,
    máscara, serigrafía, contorno y perforación.

## Trazabilidad

El nombre del diseño indica revisión de hardware **V2.1**. La versión del
firmware se mantiene por separado en la constante `VERSION`. Para cada lote se
recomienda registrar como mínimo: revisión de PCB, commit del repositorio,
versión de firmware, BOM aprobada, fabricante y fecha de montaje.

