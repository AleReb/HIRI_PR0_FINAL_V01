# Registro de Cambios - FirmwarePro

## [V0.1.8] - 2026-10-01
### Perfiles HIRI 84 a 89 y lista de dispositivos en README
- Se agregan equipos 84, 85, 86, 87, 88 y 89 a DEVICE_API_PROFILES, usando
  exactamente los IDs proporcionados y el perfil SHT31 de 19 valores.
- Se verifican equipos existentes 82 y 83; 86 se registra una sola vez.
- README incluye dispositivos disponibles, mapeos 82-89 y orden/unidades de
  los 19 campos de valores e IDs de variables.
- Se conserva DEVICE_ID_STR actual, endpoint API_BASE, 5 strikes y carrusel 10 s.
- Validacion: 20 perfiles unicos; HIRI 82-89 con 19 campos alineados y mapeos
  coincidentes con README. Compilacion ESP32 correcta (78% flash, 16% RAM).
- No se realizan solicitudes al endpoint de insercion para verificar mapeos.

## [V0.1.7] - 2026-10-01
### Comprobar modem antes de recuperarlo y pulso LED durante el arranque
- AT antes de CPOF/PWRKEY, tambien en reset manual, recuperacion y watchdog/panic.
- Consulta attach/PDP y prueba TCP al host de API_BASE sin enviar mediciones.
- Si AT responde, conservar modem; recuperar red/PDP sin apagarlo. Un fallo TCP
  del servidor no fuerza ciclo. Solo falta de AT permite ciclo en recuperacion.
- Pulso azul comun a encendido/reset, animacion y esperas TinyGSM mediante hook
  TINY_GSM_YIELD; tambien en AT y cuenta regresiva de apagado.
- Respeta ledEnabled y restaura LED por PM2.5 al entrar al loop.
- En reset que conserva modem se omite XTRA inicial para evitar hasta 120 s extra.
- Se conservan los 5 strikes y carrusel de tres datos cada 10 segundos.
- Validacion: compilacion ESP32 correcta (78% flash, 16% RAM); pruebas C++
  de orden AT/PDP/TCP, reutilizacion, servidor caido, falta de cobertura,
  recuperacion sin AT, timeout y pulso LED respetando OFF.
- TCP solo verifica el servidor consultado; sin STATUS no se verifica apagado
  fisico. Pendiente validar placa y reinicios watchdog reales.

## [V0.1.6] - 2026-10-01
### Reinicio ESP32/modem y carrusel de tres vistas cada 10 segundos
- Carrusel limitado a PM2.5, temperatura y humedad, cada 10 s.
- Navegacion manual conserva opciones; vuelve a PM2.5 tras 10 s sin seleccionar
  Empezar/Detener muestreo u Opciones. Submenus/confirmaciones permanecen abiertos.
- Icono propio con dos flechas curvas, separado del icono de reiniciar.
- Menu Reiniciar y Serial reboot comparten reinicio programado con aviso visible;
  el loop no inicia otras operaciones mientras espera los 750 ms del aviso.
- Marca de un solo uso solicita ciclo SIM7600 al reiniciar por software, tambien
  desde reinicio manual. Cuenta regresiva visible de 30 s y diagnosticos Serial.
- Se vuelve a ejecutar verificacion AT/red; se conservan 5 strikes y configuracion.
- Reinicio manual deja muestreo OFF; recuperacion automatica conserva reanudacion.
- Validacion: compilacion ESP32 correcta (77% flash, 15% RAM), icono inspeccionado
  y pruebas C++ con tiempo/Preferences simulados: 3 datos/10 s, retorno desde
  opciones, pausas, rollover, OFF persistente y marca de reinicio de un solo uso.
- Pendiente validar en placa: PWRKEY no corta VBAT y no hay STATUS para verificar.

## [V0.1.5] - 2026-10-01
### Carrusel persistente de datos y control desde Serial/menu
- Carrusel cada 5 s: PM2.5, PM1, PM10, temperatura y humedad del Plantower.
- Configuracion carouselEnabled guardada en config/carousel mediante loadConfig
  y saveConfig; OFF por defecto y con configreset.
- Serial: set carousel on/off, config display/config y ayuda help actualizada.
- Menu Configuracion: Carrusel ON/OFF, pantalla tipo RTC con BTN2 alternar y
  guardar, BTN1 salir; icono de rotacion.
- Pausa en menus, mensajes, confirmaciones, WiFi, Full y OLED inactiva.
- Navegacion manual reinicia la espera; no mantiene despierta la OLED.
- Se conservan los 5 strikes elegidos por el usuario.
- Validacion: compilacion ESP32 correcta (77% flash, 15% RAM); pruebas C++
  con Preferences/tiempo simulados para persistencia, defaults, rotacion,
  navegacion, pausas, auto-off y rollover de millis.
- Pendiente prueba visual y persistencia en hardware.

## [V0.1.4] - 2026-10-01
### Arranque paciente y recuperacion de red a los 30 minutos
- CONNECTION_MAX_STRIKES = 10 debajo de VERSION, con ventanas AT de 15 s.
- AT antes de PWRKEY y reintentos de pulso cada 3 strikes, conservando polaridad.
- Registro de red con timeout de 60 s por intento, como el original.
- Reinicio ESP32 tras 30 min continuos sin PDP; sin reinicio durante WiFi SD.
- Recuperacion solicita apagado CPOF/PWRKEY, espera 30 s y solicita encendido.
- Marca persistente de un solo uso para restaurar HTTP/SD y contadores.
- Reconexion HTTP: un intento PDP por llamada y backoff de 60 s tras fallo.
- Watchdog 180 s: TinyGSM puede bloquear 60 s en NETCLOSE y 75 s en NETOPEN.
- Validacion: compilacion ESP32 correcta (77% flash, 15% RAM) y pruebas
  C++ con modem/tiempo simulados: UART lenta, timeout, timer de 30 min,
  rollover de millis, estados de recuperacion, PWRKEY y backoff HTTP.
- Riesgos: arranque mas largo; PDP sigue bloqueante; sin STATUS/VBAT no se
  verifica apagado fisico. Pendiente validacion en hardware conocido.

## [V0.1.3] - 2026-10-01
### Perfiles de equipos y URL seleccionada por DEVICE_ID_STR
- Tabla visible en el lugar de las listas originales para los equipos 1 a 10
  y 80 a 83, conservando sus IDs del backend.
- DEVICE_ID_STR selecciona sensores, variables y extras de la URL.
- Extras SHT31/SDS198 seleccionados explicitamente por perfil.
- Constructor de URL separado del envio; valida cantidades de campos.
- IDs desconocidos o invalidos no usan el perfil de otro equipo.
- Comentarios y README explican como agregar equipos y tipos de sensores.
- Pendiente validar en hardware y contra el backend; no cambia drivers ni CSV.

## [V0.1.2] - 2026-10-01
### Arranque con strikes y funcionamiento sin red
- Espera AT limitada a 3 strikes configurables, eliminando el bucle infinito.
- Registro/PDP limitado a 3 intentos; al agotarlos pasa al modo normal.
- Sin respuesta AT se omiten GNSS, consultas del loop y apertura HTTP.
- Sin internet se omite XTRA; GNSS se inicia si hay respuesta AT.
- PDP conserva los timeouts de TinyGSM. Pendiente prueba en hardware con
  ausencia de respuesta AT, sin cobertura y con red disponible.

## [V0.1.0] - 2026-02-14
### Primera versión formal (baseline de producto)
- Se establece versión de firmware en código a **Pro V0.1.0** (`FirmwarePro.ino`).
- Ajustes finales de UI WiFi:
  - Pantalla WiFi dedicada mostrando **SSID / PASS / IP**.
  - Control por botones según estado WiFi:
    - Con WiFi activo: pantalla bloqueada (sin salida accidental).
    - Toggle ON/OFF por BTN2.
- Se repone feedback visual de arranque de módem:
  - Parpadeo de LED RGB durante `MODEM Starting...` para indicar actividad y evitar falsa percepción de bloqueo.
- Documentación ordenada en carpeta `documentacion/` con `README.md` en raíz y `LICENSE` en root del repo.

## [V0.0.35] - 2026-02-14
### WiFi SD, documentación y control de release
- Se consolidó versión de firmware a **Pro V0.0.35**.
- Se implementó flujo de **WiFi SD** con mejoras para uso desde Windows (captive portal DNS).
- Se creó documentación formal en Markdown:
  - `README.md`
  - `MANUAL_USUARIO.md`
  - `MANUAL_TECNICO.md`
  - `LICENSE` (CC BY-NC 4.0)
- Se realizó recuperación/validación de estabilidad tras pruebas (bootloop por SW reset reportado en terreno).
- **Commit con funcionamiento confirmado en equipo (booteo + flasheo exitoso):**
  - `6c66dbb` (reaplicación de cambios WiFi exclusivo)
  - Flasheado exitosamente en COM5 con verificación hash y reset OK.

## [V0.0.33] - 2026-02-14
### Notas operativas en CSV + menú Mensajes
- Se agregó nueva columna `notas` al CSV al final del header:
  - `...,pm100,notas`
- Se agregó opción **"Otros"** al submenú **Mensajes**.
- Lógica de nota one-shot:
  - Al seleccionar `Camion`, `Humo`, `Construccion` u `Otros`, se guarda esa nota en la próxima fila CSV.
  - Luego se limpia automáticamente para evitar repetición.

## [V0.0.26] - 2026-02-13
### Mejoras en UI y Lógica de Muestreo
- **Nuevo Prompt de Confirmación:** Se reemplazó el aviso simple por un cuadro de diálogo claro: "¿CONFIRMAR ACCIÓN? INICIAR/DETENER MUESTREO".
- **Lógica de Botones en Prompt:** 
  - **BTN1:** Cancela la acción y vuelve al menú.
  - **BTN2:** Confirma e inicia/detiene el proceso.
- **Muestreo Inmediato:** Al confirmar el inicio, se fuerza una ejecución inmediata de guardado en SD y envío HTTP para feedback visual instantáneo (LED/Header).
- **Depuración:** Se añadieron mensajes por puerto serial para rastrear clics de botones físicamente.

## [V0.0.25] - 2026-02-13
### Reestructuración de Navegación
- **Modo FULL en Menú:** Se eliminó el acceso por pulsación larga (Hold) y se añadió como la 5ª opción del Menú Principal.
- **Salida de Modo FULL:** Ahora se sale del modo bloqueado haciendo clic en el **BTN1 (izquierdo)**, devolviendo al usuario al menú principal.
- **Hold Desactivado:** Se deshabilitó la función de mantener presionado el botón derecho para evitar saltos accidentales de pantalla.

## [V0.0.24] - 2026-02-13
### Seguridad de Operación
- **Implementación de Full Lock:** Bloqueo de navegación de menús mientras el dispositivo está en modo de visualización de datos (Modo FULL).
- **Control de Muestreo:** Sincronización de BTN2 para alternar inicio/parada tanto en menú como en vista bloqueada.

## [V0.0.23] - 2026-02-13
### Versión Inicial Fusionada
- Integración de Backend GPSDebug con UI de Menús HIRI_PR0.
- Soporte para sensores PMS5003, SHT31, SDS198.
- Registro en SD y transmisión HTTP concurrente.
