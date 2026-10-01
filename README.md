# FirmwarePro (HIRI)

Firmware para estación de monitoreo técnico-científico basada en ESP32, con adquisición de sensores, GNSS, registro en SD, telemetría HTTP y gestor de archivos por WiFi AP.

## Estado

- Plataforma objetivo: **ESP32 Dev Module** (`esp32:esp32:esp32`)
- Versión de referencia documentada: **Pro V0.1.8**

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

## Carrusel de datos persistente

En **Opciones > Configuración > Carrusel**, BTN2 abre una pantalla de control
similar a RTC. Allí **BTN2 activa/desactiva** el carrusel y **BTN1 sale**.
El estado ON/OFF se guarda en flash y se conserva al apagar o reiniciar.
Por defecto está desactivado.

Desde Serial (115200 baud, nueva línea):

```text
set carousel on
set carousel off
config display
```

Los comandos `set` guardan automáticamente con el mismo `saveConfig()` que las
demás opciones. `config` muestra también el estado, `configsave` sigue disponible
y `configreset` restaura carrusel OFF. `help` incluye la opción y sus controles.

En las vistas de datos del inicio, rota cada **10 segundos**:
**PM2.5 > temperatura > humedad**. Usa las lecturas Plantower
actuales, conservando encabezado y controles. Los puntos inferiores indican
una de las tres páginas del carrusel. Los botones mantienen la navegación habitual.
Al navegar a Empezar/Detener muestreo u Opciones, muestra la opción elegida
durante 10 segundos; si no se selecciona, vuelve a PM2.5.
Se pausa dentro de submenús, confirmaciones, WiFi, modo Full y con la OLED apagada por
inactividad. La rotación no reinicia el plazo de apagado automático. Al volver
al inicio o navegar manualmente, espera otros 10 segundos antes de cambiar.

## Reinicio manual del equipo

**Configuración > Reiniciar** y el comando Serial `reboot` utilizan el mismo
reinicio. Primero muestran el aviso durante 750 ms, sin comenzar otra tarea
AT/HTTP. Se reinicia el ESP32 y el siguiente arranque **primero verifica AT**,
registro de datos y PDP. Con una sesion previa activa prueba una conexion TCP
al host de `API_BASE`, sin enviar mediciones. Si el modem responde, conserva
su encendido y recupera red/PDP si hace falta; un fallo TCP del servidor no
provoca apagado. Si no hay AT tras la espera y se trata de reinicio manual,
recuperacion automatica, panic o watchdog, solicita el ciclo del modem con
cuenta regresiva de 30 segundos. Conserva los 5 strikes.
Los mensajes `[BOOT]` y `[MODEM]` permiten seguir cada etapa por Serial.

El reinicio manual mantiene la configuración persistente, pero deja el muestreo
apagado. El reinicio automático de recuperación conserva su restauración de
muestreo. PWRKEY/CPOF solicitan el ciclo del módem; no cortan VBAT ni permiten
verificar el apagado físico sin una señal STATUS.

El LED pulsa azul durante la animacion y las esperas AT/red/recuperacion,
tambien dentro de TinyGSM, con la misma secuencia tras reinicio y encendido.
Respeta `set led off`. Al entrar al loop vuelve al color de PM2.5.
En un reset que conserva el modem se omite la descarga XTRA inicial para
no sumar otra espera de hasta 120 segundos. La prueba TCP confirma acceso
al servidor consultado; un fallo no distingue caida del servidor de falta
de ruta a internet.

## Configurar equipos y sensores de la API

En `HIRI_PR0_FINAL_V01.ino`, cambie solamente `DEVICE_ID_STR` para seleccionar
un equipo de `DEVICE_API_PROFILES`. La tabla permanece junto a `API_BASE`,
en el lugar de las listas originales, con todos los equipos visibles.
La URL utiliza los IDs de sensores, variables y valores del perfil elegido.
`"06"` y `"6"` seleccionan el mismo perfil; el identificador escrito se conserva
en pantalla, archivos y valor enviado del equipo.

Para agregar un equipo, copie una fila de la tabla y complete:

- ID del equipo.
- Lista de IDs de sensores asignados por el backend.
- Lista de variables: base, SHT31 o SDS198.
- Sensor extra: `EXTRA_NONE`, `EXTRA_SHT31` o `EXTRA_SDS198`.

El orden de las mediciones está comentado encima de la tabla. Las tres listas
(sensores, variables y valores) deben tener la misma cantidad de campos.
Si el ID no existe o las cantidades no coinciden, el firmware rechaza el
envío HTTP y deja un mensaje en Serial; los sensores y la pantalla continúan.
Los IDs del backend son explícitos: no se deducen del número de equipo.

Para un tipo de sensor nuevo, agregue su opción a `ExtraSensorApi`, su lista
de variables y los valores correspondientes en `buildMeasurementUrl()`.
La inicialización y lectura del hardware se agregan en `setup()`/`loop()`;
la tabla configura la telemetría y conserva los drivers actuales.

### Lista de dispositivos HIRI

Perfiles disponibles en `DEVICE_API_PROFILES`: **1–10 y 80–89**.
Los HIRI **82 y 83** ya estaban registrados y coinciden con los mapeos
proporcionados; se agregaron **84, 85, 86, 87, 88 y 89**. Cada uno utiliza
19 valores y el perfil `EXTRA_SHT31`. HIRI 86 tiene una sola entrada.

| HIRI / DEVICE_ID_STR | PMS, campos 1–5 | Módem/GPS, 6–10 | RTC, 11 | Batería, 12 | Equipo, 13–17 | SHT31, 18–19 |
|---|---|---|---|---|---|---|
| 82 | 939 | 940 | 941 | 942 | 943 | 944 |
| 83 | 945 | 946 | 947 | 948 | 949 | 950 |
| 84 | 951 | 952 | 953 | 954 | 955 | 956 |
| 85 | 957 | 958 | 959 | 960 | 961 | 962 |
| 86 | 963 | 964 | 965 | 966 | 967 | 968 |
| 87 | 969 | 970 | 971 | 972 | 973 | 974 |
| 88 | 975 | 976 | 977 | 978 | 979 | 980 |
| 89 | 981 | 982 | 983 | 984 | 985 | 986 |

Cada ID de la tabla se repite para todos los campos de su grupo. Por ejemplo,
HIRI 84 utiliza cinco veces 951 para PMS, cinco veces 952 para módem/GPS,
953 para RTC, 954 para batería, cinco veces 955 para equipo y dos veces 956
para SHT31. El firmware mantiene las listas completas y explícitas.

El orden de `idsVariables` es común a estos ocho equipos:

```text
3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,3,6
```

| Posición | Grupo | ID variable | Valor enviado | Unidad |
|---|---|---|---|---|
| 1 | PMS | 3 | Temperatura | °C |
| 2 | PMS | 6 | Humedad | % |
| 3 | PMS | 7 | PM1.0 | µg/m³ |
| 4 | PMS | 8 | PM2.5 | µg/m³ |
| 5 | PMS | 9 | PM10 | µg/m³ |
| 6 | Módem/GPS | 11 | Latitud | ° |
| 7 | Módem/GPS | 12 | Longitud | ° |
| 8 | Módem/GPS | 15 | Intensidad de señal telefónica | Adimensional |
| 9 | Módem/GPS | 45 | Velocidad | km/h |
| 10 | Módem/GPS | 46 | Satélites | Entero |
| 11 | RTC | 3 | Temperatura interna | °C |
| 12 | Batería | 4 | Voltaje | V |
| 13 | Equipo | 11 | Latitud | ° |
| 14 | Equipo | 12 | Longitud | ° |
| 15 | Equipo | 42 | Identificador DEVICE_ID_STR | String |
| 16 | Equipo | 43 | Número de envíos | Numeral |
| 17 | Equipo | 44 | Registro SD activo | Bool (0/1) |
| 18 | SHT31 | 3 | Temperatura | °C |
| 19 | SHT31 | 6 | Humedad | % |

Para seleccionar uno, cambie solamente el identificador, por ejemplo:

```cpp
const char *DEVICE_ID_STR = "89";
```

Los textos descriptivos de `valores` proporcionados representan el significado
de cada campo. El firmware los sustituye por las lecturas actuales al construir
la URL de `insertarMedicion`, usando el endpoint definido en `API_BASE`.

## Arranque y recuperación sin conexión

Debajo de `VERSION`, en `HIRI_PR0_FINAL_V01.ino`, configure:

```cpp
const uint8_t CONNECTION_MAX_STRIKES = 5;
```

Antes de pulsar PWRKEY se consulta AT durante hasta 15 segundos, porque un
reinicio del ESP32 puede dejar el módem encendido. Si no responde, se aplica
el pulso de la placa y se permiten 5 strikes de 15 segundos cada uno.
PWRKEY se vuelve a pulsar solo cada 3 strikes sin respuesta, dejando unos
45 segundos entre pulsos. Esto conserva los reintentos del original y permite
completar el arranque de la UART. La etapa AT puede tardar unos 90 segundos.

El registro celular permite 5 intentos de hasta 60 segundos, como la espera
por intento del firmware antiguo. PDP agrega los tiempos internos de TinyGSM
(hasta 60 s de NETCLOSE y 75 s de NETOPEN, además de otros comandos).
Por eso el arranque sin red puede tardar varios minutos. Una vez agotados los
intentos, se pasa al modo normal para leer y mostrar datos. Sin AT se omiten
GNSS y HTTP; con AT pero sin internet, GNSS se inicia y se omite XTRA.

En el loop se consulta PDP cada minuto. Tras **30 minutos continuos sin PDP**,
se guardan contadores, archivo CSV y estados HTTP/SD, y se reinicia el ESP32.
Ese arranque comprueba primero AT y PDP; conserva un módem que responde.
Solo si AT no responde tras esperar solicita apagarlo/encenderlo y espera
30 segundos para completar el apagado. Se restaura el muestreo que
estaba activo; SD se restaura solamente si se monta correctamente. La marca de
recuperación es de un solo uso y solo se aplica a ese reinicio por software.

Recuperar PDP reinicia el contador de desconexión. Un error HTTP del servidor
no provoca reinicio si PDP sigue activo. Durante WiFi SD no se reinicia el
equipo: al salir se dispone de otros 30 minutos. El plazo se ajusta mediante
`NETWORK_REBOOT_AFTER_MS`, junto a la versión.

HTTP hace un intento de reconexión PDP por llamada y espera al menos 60 segundos
tras un fallo. El watchdog es de 180 segundos para permitir que TinyGSM termine;
la llamada PDP sigue siendo bloqueante y puede pausar la UI mientras espera.

La polaridad GPIO4 HIGH/LOW se conserva de la placa actual. PWRKEY es una
solicitud de encendido/apagado, no un corte de alimentación. Sin STATUS ni
control de VBAT no se puede verificar un apagado físico. Referencia de tiempos:
[SIMCom SIM7600 Hardware Design, sección 3.2](https://media.digikey.com/pdf/Data%20Sheets/SIMCom%20PDFs/SIM7600_Series_Hardware_Design.pdf).

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
| SHT41 | I²C `0x44` | Detección, número de serie, temperatura y humedad en pantalla dedicada |
| Plantower/PMS | 9600 baud, RX 5, TX 18 | Trama, checksum, PM1, PM2.5, PM10 y T/H cuando el modelo las incluye |
| NeoPixel RGB | GPIO 12 | Secuencia rojo, verde, azul, blanco y apagado |
| Botón 1 | GPIO 19, activo en HIGH | Estado, antirrebote y contador de pulsaciones |
| Botón 2 | GPIO 23, activo en HIGH | Estado, antirrebote y contador de pulsaciones |
| Batería | ADC GPIO 35 | Promedio filtrado y voltaje calibrado |

Los botones utilizan las resistencias pull-down externas de la placa. Por eso se
configuran como `INPUT`: sueltos se leen en LOW y pulsados en HIGH.

### Información mostrada

La OLED utiliza el mismo constructor SSD1306, fuente y rotación `U8G2_R2` del
firmware principal. Al arrancar muestra un patrón de pantalla y luego alterna
cada cinco segundos entre el resumen general y una pantalla dedicada al SHT41.
El resumen es similar al siguiente:

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

El encabezado cambia a `TEST: TODO OK*` cuando OLED, RTC, SHT41, Plantower,
batería y ambos botones superan las verificaciones automáticas. Para la batería se acepta
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
