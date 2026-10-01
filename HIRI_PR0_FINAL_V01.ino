/*
 * FirmwarePro.ino
 * Merged Firmware: GPSDebug Backend + Menu UI
 *
 * Features:
 * - GPS/GNSS (Sim7600)
 * - PMS5003 + SHT31 + SDS198 Sensors
 * - SD Card Logging
 * - HTTP Streaming
 * - OLED UI with Menus (U8g2)
 * - Navigation: OneButton (BTN1=Cycle, BTN2=Select/Hold)
 * - WiFi AP for File Management
 */

// Modem definition moved to config.h

#include "Adafruit_SHT31.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
// #include <Adafruit_NeoPixel.h> // Moved to config.h
// #include <Arduino.h> // Included in config.h
#include <Preferences.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
// #include <TinyGsmClient.h> // Moved to config.h
#include <U8g2lib.h>
#include <WebServer.h>
#include <DNSServer.h> // Added for Captive Portal
#include <WiFi.h>
#include <Wire.h>
#include <esp_task_wdt.h>

// -------------------- DEFINES & GLOBALS --------------------
#include "config.h"

// Modem modem
// #define TINY_GSM_RX_BUFFER 4096 // Moved to config.h
#define SerialAT Serial1
TinyGsm modem(SerialAT);

// --- Missing global constants/state restored for broken build ---
#define RTC_PROBE_PERIOD_MS 60000
#define RTC_SYNC_THRESHOLD 30
#define MIN_VALID_EPOCH 1672531200 // 2023-01-01

// SDS198 protocol constants
const byte HEADER = 0xAA;
const byte CMD = 0xB4;
const byte TAIL = 0xAB;

// Firmware version
String VERSION = "Pro V0.1.8";
const uint8_t CONNECTION_MAX_STRIKES = 5; // EDITAR AQUI: intentos AT y de red.
static_assert(CONNECTION_MAX_STRIKES > 0, "Debe haber al menos un strike");
// Cada strike AT permite iniciar el modem; no es una consulta instantanea.
const uint32_t MODEM_STRIKE_WINDOW_MS = 15000;
const uint8_t MODEM_PWRKEY_RETRY_EVERY = 3; // Pulso cada 3 ventanas sin AT.
const uint32_t NETWORK_ATTACH_TIMEOUT_MS = 60000; // Igual que el arranque antiguo.
const uint32_t NETWORK_REBOOT_AFTER_MS = 30UL * 60UL * 1000UL;
const uint32_t NETWORK_HEALTH_PERIOD_MS = 60000;

// Global states
bool rtcOK = false;
bool SHT31OK = false;
bool SDOK = false;
bool wifiModeActive = false;
bool hasRed = false;
bool modemReady = false; // Sin respuesta AT: operar con sensores locales.
bool offlineTracking = false;
uint32_t offlineSinceMs = 0;
uint32_t lastNetworkHealthMs = 0;
bool resumeAfterNetworkRecovery = false;
bool modemRecoveryOnBoot = false; // Habilita recuperacion si falla AT; no fuerza ciclo.
bool bootLedActive = false;
uint32_t bootLedLastMs = 0;
bool bootLedOn = false;
bool systemRestartPending = false;
uint32_t systemRestartRequestedMs = 0;
bool recoveryStreaming = false;
bool recoveryLogging = false;

// Config Instance
SystemConfig config;

// Objects
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIX_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_SHT31 sht31 = Adafruit_SHT31();
RTC_DS3231 rtc;
Preferences prefs;
SPIClass spiSD(HSPI);
WebServer server(80); // Used in wifi.ino
DNSServer dnsServer;  // Captive Portal DNS

// PMS
SoftwareSerial pms(pms_TX, pms_RX);

// Button flags (edge + debounce)
// Button flags (edge + debounce)
volatile bool btn1ClickFlag = false;
volatile bool btn2ClickFlag = false;
volatile bool btn2HoldFlag = false;

// Internal Flags for ISR State tracking
// (Removed complex hold state tracking)

// Debounce Tracking
volatile uint32_t lastDebounceTime1 = 0;
volatile uint32_t lastDebounceTime2 = 0;
const uint32_t BTN1_DEBOUNCE_MS = 200; // Increased for Button 1 stability
const uint32_t BTN2_DEBOUNCE_MS = 200;  // Increased for Button 2 stability (was 50)

// Data Variables
uint16_t PM1 = 0, PM25 = 0, PM10 = 0;
float pmsTempC = NAN, pmsHum = NAN;
int SDS198PM100 = 0;
float tempsht31 = NAN, humsht31 = NAN;
float rtcTempC = NAN;
float batV = 0.0f;
int csq = 0;
bool networkError = false;

// GPS Data
String gpsLat = "NaN", gpsLon = "NaN";
String gpsTime = "N/A", gpsDate = "N/A";
String satellitesStr = "0", hdopStr = "N/A", gpsAlt = "N/A";
String gpsStatus = "NoFix";
String gpsSpeedKmh = "0.0";

// Internal Logic Variables
bool loggingEnabled = false;
bool streaming = false;

String csvFileName = "";
String logFilePath = "";
String failedTxPath = "";
String currentNote = "3"; // Global note for one-shot logging
// Variables moved to main for centralization
String lastSavedCSVLine = ""; // Used in sd_card.ino for OLED display
File uploadFile;              // Used in wifi.ino for file uploads

String deviceID = "/HIRIP";
// CAMBIAR SOLO ESTE ID para seleccionar el equipo y su URL de mediciones.
// Se aceptan ceros iniciales: "06" selecciona el mismo equipo que "6".
const char *DEVICE_ID_STR = "81";
String AP_SSID_STR = "";
const char *AP_PASSWORD = "12345678";
String apIpStr = "0.0.0.0";
// -------------------- Measurements API (real endpoint) --------------------
const char *API_BASE = "http://api-sensores.cmasccp.cl/insertarMedicion";
// ================= TABLA DE EQUIPOS / SENSORES API =================
// Mantener aqui los IDs asignados por el backend (no se calculan por formula).
// Orden de los 17 valores base, alineado con idsSensores e idsVariables:
//  1..5  PMS: temperatura, humedad, PM1, PM2.5, PM10
//  6..10 Modem/GPS: latitud, longitud, senal, velocidad, satelites
// 11..12 RTC: temperatura; bateria: voltaje
// 13..17 Equipo: latitud, longitud, DEVICE_ID_STR, contador, estado SD
// Extras al final: SHT31 = temperatura/humedad; SDS198 = PM100.
const char *IDS_VARIABLES = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44";
const char *IDS_VARIABLESSHT31 = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,3,6";
const char *IDS_VARIABLES06 = "3,6,7,8,9,11,12,15,45,46,3,4,11,12,42,43,44,51";

enum ExtraSensorApi { EXTRA_NONE, EXTRA_SHT31, EXTRA_SDS198 };
struct DeviceApiProfile {
  uint16_t deviceId;
  const char *sensorIds;
  const char *variableIds;
  ExtraSensorApi extraSensor;
};

// Columnas: ID equipo | IDs sensores backend | IDs variables | sensor extra.
// NUEVO EQUIPO: copiar una fila y reemplazar ID e IDs del backend.
// No cambiar las filas existentes para agregar otro equipo.
const DeviceApiProfile DEVICE_API_PROFILES[] = {
  { 1, "401,401,401,401,401,402,402,402,402,402,403,404,405,405,405,405,405", IDS_VARIABLES, EXTRA_NONE}, // Equipo 1: 17 valores
  { 2, "406,406,406,406,406,407,407,407,407,407,408,409,410,410,410,410,410", IDS_VARIABLES, EXTRA_NONE}, // Equipo 2: 17 valores
  { 3, "415,415,415,415,415,416,416,416,416,416,417,418,419,419,419,419,419,420,420", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 3: 19 valores
  { 4, "448,448,448,448,448,449,449,449,449,449,450,451,452,452,452,452,452,453,453", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 4: 19 valores; nota original: no actualizado en DICTUC
  { 5, "454,454,454,454,454,455,455,455,455,455,456,457,458,458,458,458,458,459,459", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 5: 19 valores
  { 6, "460,460,460,460,460,461,461,461,461,461,462,463,464,464,464,464,464,467", IDS_VARIABLES06, EXTRA_SDS198}, // Equipo 6: 18 valores
  { 7, "468,468,468,468,468,469,469,469,469,469,470,471,472,472,472,472,472", IDS_VARIABLES, EXTRA_NONE}, // Equipo 7: 17 valores
  { 8, "473,473,473,473,473,474,474,474,474,474,475,476,477,477,477,477,477", IDS_VARIABLES, EXTRA_NONE}, // Equipo 8: 17 valores
  { 9, "478,478,478,478,478,479,479,479,479,479,480,481,482,482,482,482,482,483,483", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 9: 19 valores
  {10, "484,484,484,484,484,485,485,485,485,485,486,487,488,488,488,488,488,489,489", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 10: 19 valores
  {80, "927,927,927,927,927,928,928,928,928,928,929,930,931,931,931,931,931,932,932", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 80: 19 valores
  {81, "933,933,933,933,933,934,934,934,934,934,935,936,937,937,937,937,937,938,938", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 81: 19 valores
  {82, "939,939,939,939,939,940,940,940,940,940,941,942,943,943,943,943,943,944,944", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 82: 19 valores
  {83, "945,945,945,945,945,946,946,946,946,946,947,948,949,949,949,949,949,950,950", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 83: 19 valores
  {84, "951,951,951,951,951,952,952,952,952,952,953,954,955,955,955,955,955,956,956", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 84: 19 valores
  {85, "957,957,957,957,957,958,958,958,958,958,959,960,961,961,961,961,961,962,962", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 85: 19 valores
  {86, "963,963,963,963,963,964,964,964,964,964,965,966,967,967,967,967,967,968,968", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 86: 19 valores
  {87, "969,969,969,969,969,970,970,970,970,970,971,972,973,973,973,973,973,974,974", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 87: 19 valores
  {88, "975,975,975,975,975,976,976,976,976,976,977,978,979,979,979,979,979,980,980", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 88: 19 valores
  {89, "981,981,981,981,981,982,982,982,982,982,983,984,985,985,985,985,985,986,986", IDS_VARIABLESSHT31, EXTRA_SHT31}, // Equipo 89: 19 valores
};
// NUEVO TIPO DE SENSOR: agregar su enum, lista de variables y lectura de valores
// en buildMeasurementUrl(); inicializar/leer su hardware en setup()/loop().
// Esta tabla configura telemetria; no reemplaza los drivers de los sensores.
const DeviceApiProfile *activeApiProfile = nullptr;

String valores;
String url;

// Cuenta campos CSV simples en constantes tipo "1,2,3".
// Se usa para mantener alineados idsSensores/idsVariables/valores.
uint8_t countCsvFields(const char *csv) {
  if (csv == nullptr || csv[0] == '\0')
    return 0;

  uint8_t count = 1;
  for (const char *p = csv; *p != '\0'; ++p) {
    if (*p == ',')
      count++;
  }
  return count;
}

// APN
const char apn[] = "gigsky-02";
const char gprsUser[] = "";
const char gprsPass[] = "";

// Counters
uint32_t sendCounter = 0;
uint32_t sdSaveCounter = 0;
// Separamos guardado SD y transmisión HTTP con timers independientes.
uint32_t lastHttpSend = 0;
uint32_t lastSdSave = 0;
// Estado de actividad para UI (header U/S).
uint32_t lastHttpActivityMs = 0;
uint32_t lastSdActivityMs = 0;
bool lastHttpOk = false;
bool lastSdOk = false;
uint8_t lastDayLogged = 0;
bool wasStreamingBeforeBoot = false;

// Animation Variables
int logoXOffset = -25;
int hiriXOffset = 128;
int proYOffset = 64;
const int LOGO_FINAL_X = 4;
const int HIRI_FINAL_X = 48;
const int PRO_FINAL_X = 106;
const int HIRI_FINAL_Y = 44;
const int PRO_FINAL_Y = 52;

// Watchdog
// TinyGSM puede esperar 60 s en NETCLOSE + 75 s en NETOPEN.
// Un reset antes de que retorne gprsConnect no permite terminar el intento.
#define WDT_TIMEOUT 180
String rebootReason = "Unknown";
String networkOperator = "N/A";
String networkTech = "N/A";
String signalQuality = "0";
String registrationStatus = "N/A";

// XTRA
uint32_t lastXtraDownload = 0;
bool xtraSupported = false;
bool xtraLastOk = false;
const uint32_t XTRA_REFRESH_MS = 3UL * 24UL * 60UL * 60UL * 1000UL;

// Display State
// Display State
volatile DisplayState displayState = DISP_NORMAL;
volatile uint32_t displayStateStartTime = 0;
uint32_t lastOledActivity = 0;

// Modem Sync
uint8_t rtcModemSyncCount = 0;
uint32_t lastModemSyncAttempt = 0;
const uint32_t MODEM_SYNC_INTERVAL_MS = 600000;
const uint8_t MAX_MODEM_SYNC_COUNT = 3;
bool rtcNetSyncPending = false;
uint32_t rtcNextProbeMs = 0;

// AT Command Struct
struct AtSession {
  bool active = false;
  String expect1;
  String expect2;
  String resp;
  uint32_t deadline = 0;
} at;

// Buffer for PMS
static uint8_t pmsBuf[64];
static size_t pmsHead = 0;
static uint32_t lastPmsSeen = 0;

// Battery Averaging
const float alpha = 0.8;
float batteryVoltageAverage = 0;
static uint32_t lastBatSample = 0;
static float batSampleSum = 0;
static int batSampleCount = 0;
const int NUM_SAMPLES = 30;
const uint32_t BAT_SAMPLE_INTERVAL_MS = 5;

// Variables needed for GPS diag
uint32_t lastNmeaSeenMs = 0;
uint32_t gnssFixFirstMs = 0;
bool gnssFixReported = false;
uint8_t gsaFixType = 0;
uint8_t gsaSatsUsed = 0;
float gsaPdop = NAN, gsaHdop2 = NAN, gsaVdop = NAN;
uint16_t gsvSatsInView = 0;
float gsvSnrAvg = 0, gsvSnrMax = 0;
uint32_t gsvLastMs = 0;
float gsvSnrAcc = 0;
int gsvSnrCnt = 0;
uint32_t lastNmeaMs = 0;
uint32_t lastGgaMs = 0;
uint32_t lastFixMs = 0;
uint16_t nmeaCount1s = 0;
uint16_t nmeaRate = 0;
uint32_t nmeaRefMs = 0;
uint8_t fixQLast = 0;
bool ttffPrinted = false;
uint32_t gnssStartMs = 0;
bool haveFix = false;

// GnssDbgState moved to gps.ino

// SD Definition constants
const int SD_SCLK = 14, SD_MISO = 2, SD_MOSI = 15, SD_CS = 13;

// Extern function declarations (if needed explicitly, though linking usually
// handles it)
void loadConfig();
void applyLEDConfig();
void writeErrorLogHeader();
String generateCSVFileName();
void writeCSVHeader();
void drawAnimation(); // From animacion.ino
void startWifiApServer();
void stopWifiApServer();
void renderDisplay(); // From ui.ino
void showMessage(const char *msg);
bool saveCSVData();
void checkRebootReason();
void logError(const String &type, const String &ctx, const String &msg);
void readPMS();
bool readFrameSDS198(byte *buf);
void updatePmLed(float pm25);
void gnssBringUp();
void gnssDiagTick();
void gnssDebugPollAsync();
void gnssWatchdog();
bool atTick(bool &done, bool &ok);
bool atRun(const String &cmd, const String &expect1 = "OK",
           const String &expect2 = "ERROR", uint32_t timeout_ms = 8000);
bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms = 4000);
bool httpGet_webhook(const String &url);
bool detectAndEnableXtra();
bool downloadXtraOnce();
void downloadXtraIfDue();
void parseNMEA(const String &line);
void saveFailedTransmission(const String &url, const String &errorType);
bool sendCurrentMeasurement();
void handleButtonLogic(); // Renamed from dispatchButtonFlags

// ISR Function Prototypes
void IRAM_ATTR isr_btn1();
void IRAM_ATTR isr_btn2();

// UI Event Handlers
extern void ui_btn1_click();
extern void ui_btn2_click();

// Oled Status Helper (used by wifi/main)
// Renderiza un estado rápido en OLED con hasta 4 líneas de texto.
// Se usa para feedback de arranque, red, módem y operaciones críticas.
void oledStatus(const String &l1, const String &l2 = "", const String &l3 = "",
                const String &l4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 12);
  u8g2.print(l1);
  u8g2.setCursor(0, 26);
  u8g2.print(l2);
  u8g2.setCursor(0, 40);
  u8g2.print(l3);
  u8g2.setCursor(0, 54);
  u8g2.print(l4);
  u8g2.sendBuffer();
}

// AT Helper (needed in main)
// Inicia una sesión AT no bloqueante, guarda expectativas y timeout.
// La respuesta se procesa luego con atTick() para no congelar el loop.
void atBegin(const String &cmd, const String &expect1, const String &expect2,
             uint32_t timeout_ms) {
  modem.stream.print("AT");
  modem.stream.println(cmd);
  at.active = true;
  at.expect1 = expect1;
  at.expect2 = expect2;
  at.resp = "";
  at.deadline = millis() + timeout_ms;
}

// Avanza la máquina de estados AT leyendo serial y detectando fin/timeout.
// También enruta tramas NMEA entrantes al parser GNSS cuando aparecen.
bool atTick(bool &done, bool &ok) {
  bootLedTick();
  while (SerialAT.available()) {
    String line = SerialAT.readStringUntil('\n');
    line.trim();
    if (line.isEmpty())
      continue;

    if (line.charAt(0) == '$') {
      parseNMEA(line);
      continue;
    }

    if (!at.active)
      continue;
    at.resp += line;
    at.resp += "\n";
    if (at.expect1.length() && line.indexOf(at.expect1) >= 0) {
      done = true;
      ok = true;
      at.active = false;
      return true;
    }
    if (at.expect2.length() && line.indexOf(at.expect2) >= 0) {
      done = true;
      ok = (at.expect2 == "OK");
      at.active = false;
      return true;
    }
  }
  if (at.active && millis() > at.deadline) {
    done = true;
    ok = false;
    at.active = false;
    return true;
  }
  done = false;
  ok = false;
  return false;
}

// Ejecuta un comando AT de forma bloqueante hasta éxito, error o timeout.
// Es un helper práctico para setup y tareas puntuales de configuración.
bool atRun(const String &cmd, const String &expect1, const String &expect2,
           uint32_t timeout_ms) {
  atBegin(cmd, expect1, expect2, timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok))
      break;
    delay(1);
  }
  return ok;
}

// Envía AT y devuelve la respuesta completa en un String para diagnóstico.
// Útil cuando se necesita parsear contenido (no solo OK/ERROR).
bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms) {
  atBegin(cmd, "OK", "ERROR", timeout_ms);
  bool done = false, ok = false;
  while (!done) {
    if (atTick(done, ok))
      break;
    delay(1);
  }
  resp = at.resp;
  return ok;
}

// Consulta operador, tecnología y registro de red desde el módem.
// Actualiza variables globales usadas en UI, logs y comandos seriales.
void updateNetworkInfo() {
  String resp;
  if (sendAtSync("+COPS?", resp, 3000)) {
    int idx = resp.indexOf("+COPS:");
    if (idx >= 0) {
      int start = resp.indexOf('"', idx);
      int end = resp.indexOf('"', start + 1);
      if (start >= 0 && end > start) {
        networkOperator = resp.substring(start + 1, end);
      }
    }
  }
  if (sendAtSync("+COPS?", resp, 3000)) {
    if (resp.indexOf(",7") >= 0)
      networkTech = "LTE";
    else if (resp.indexOf(",2") >= 0)
      networkTech = "3G";
    else if (resp.indexOf(",0") >= 0)
      networkTech = "2G";
    else
      networkTech = "Unknown";
  }
  signalQuality = String(csq);
  if (sendAtSync("+CREG?", resp, 2000)) {
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0)
      registrationStatus = "Registered";
    else if (resp.indexOf(",2") >= 0)
      registrationStatus = "Searching";
    else
      registrationStatus = "NotRegistered";
  }
}

// -------------------- Telemetry Tx --------------------
// NOTA DE INTEGRACION:
// - "streaming" controla transmisión HTTP.
// - "loggingEnabled" controla guardado en SD.
// - Ambos están separados a propósito para evitar acoplar guardar/transmitir.
// Construye payload/URL de medición según hardware activo y envía por HTTP.
// Persiste contadores en flash y registra fallos en SD cuando corresponde.
// Seleccion explicita: un ID desconocido deshabilita HTTP, nunca usa otro equipo.
bool selectDeviceApiProfile() {
  activeApiProfile = nullptr;
  if (!DEVICE_ID_STR || !DEVICE_ID_STR[0]) return false;
  uint32_t id = 0;
  for (const char *c = DEVICE_ID_STR; *c; ++c) {
    if (*c < '0' || *c > '9') return false;
    id = id * 10 + (*c - '0');
    if (id > 65535) return false;
  }
  for (const auto &profile : DEVICE_API_PROFILES) {
    if (profile.deviceId == id) {
      activeApiProfile = &profile;
      Serial.printf("[API] Equipo %s: %s\n", DEVICE_ID_STR, profile.sensorIds);
      return true;
    }
  }
  Serial.printf("[API][ERR] Equipo %s sin perfil; agregar fila a la tabla\n",
                DEVICE_ID_STR);
  return false;
}

// Construye URL sin enviar: facilita revisar el mapeo por Serial y probarlo.
bool buildMeasurementUrl(String &measurementUrl) {
  measurementUrl = "";
  if (!activeApiProfile) {
    Serial.println("[API][ERR] No hay perfil valido para este equipo");
    return false;
  }
  String v1 = isnan(pmsTempC) ? "0" : safeFloatStr(pmsTempC);
  String v2 = isnan(pmsHum) ? "0" : safeFloatStr(pmsHum);
  String v3 = safeUIntStr(PM1);
  String v4 = safeUIntStr(PM25);
  String v5 = safeUIntStr(PM10);
  String v6 = safeGpsStr(gpsLat);
  String v7 = safeGpsStr(gpsLon);
  String v8 = safeIntStr(csq);
  String v9 = (gpsSpeedKmh.length() ? gpsSpeedKmh : "0");
  String v10 = safeSatsStr(satellitesStr);
  String v11 = safeFloatStr(rtcTempC);
  String v12 = safeFloatStr(batV);
  String v13 = v6; // Lat
  String v14 = v7; // Lon
  String v15 = String(DEVICE_ID_STR);
  String v16 = safeUIntStr(sendCounter + 1);
  String v17 = loggingEnabled ? "1" : "0";
  const String baseVal = v1 + "," + v2 + "," + v3 + "," + v4 + "," + v5 +
                         "," + v6 + "," + v7 + "," + v8 + "," + v9 + "," +
                         v10 + "," + v11 + "," + v12 + "," + v13 + "," +
                         v14 + "," + v15 + "," + v16 + "," + v17;

  String val = baseVal;
  switch (activeApiProfile->extraSensor) {
  case EXTRA_NONE:
    break;
  case EXTRA_SHT31:
    val += "," + (isnan(tempsht31) ? String("0") : safeFloatStr(tempsht31));
    val += "," + (isnan(humsht31) ? String("0") : safeFloatStr(humsht31));
    break;
  case EXTRA_SDS198:
    val += "," + safeUIntStr(SDS198PM100);
    break;
  default:
    return false;
  }

  const uint8_t sensorCount = countCsvFields(activeApiProfile->sensorIds);
  const uint8_t variableCount = countCsvFields(activeApiProfile->variableIds);
  const uint8_t valueCount = countCsvFields(val.c_str());
  if (sensorCount != variableCount || sensorCount != valueCount) {
    Serial.printf("[API][ERR] Campos desalineados: sensores=%u variables=%u valores=%u\n",
                  sensorCount, variableCount, valueCount);
    return false;
  }
  measurementUrl = String(API_BASE) + "?idsSensores=" + activeApiProfile->sensorIds +
                   "&idsVariables=" + activeApiProfile->variableIds + "&valores=" + val;
  return true;
}

bool sendCurrentMeasurement() {
  String url;
  if (!buildMeasurementUrl(url)) return false;
  Serial.println("[HTTP] GET " + url);
  if (httpGet_webhook(url)) {
    sendCounter++;
    prefs.begin("system", false);
    prefs.putUInt("sendCnt", sendCounter);
    prefs.putUInt("sdCnt", sdSaveCounter);
    prefs.end();
    Serial.println("[HTTP] OK");
    return true;
  }

  saveFailedTransmission(url, "HTTP_FAIL");
  Serial.println("[HTTP] FAIL");
  return false;
}
                                
// Poll de botones por flags con debounce por software.
// BTN activo en LOW (INPUT_PULLUP): al soltar genera click flag.
// -------------------- INTERRUPT SERVICE ROUTINES --------------------

// Button 1: Simply trigger Click on FALLING edge (Press)
// This makes it feel instant. Debounce ensures single trigger.
void IRAM_ATTR isr_btn1() {
  uint32_t now = millis();
  if (now - lastDebounceTime1 > BTN1_DEBOUNCE_MS) {
    lastDebounceTime1 = now;
    btn1ClickFlag = true;
  }
}

// Button 2: Simple Click (FALLING)
// Logic simplified: No more Hold detection. Just Click.
void IRAM_ATTR isr_btn2() {
  uint32_t now = millis();
  
  // Debounce check
  if (now - lastDebounceTime2 > BTN2_DEBOUNCE_MS) {
    lastDebounceTime2 = now;
    btn2ClickFlag = true;
  }
}

// Checks logic (Simplified)
void handleButtonLogic() {
  // Dispatch Actions
  if (btn1ClickFlag) {
    btn1ClickFlag = false;
    ui_btn1_click();
    if (systemRestartPending) return;
  }
  if (btn2ClickFlag) {
    btn2ClickFlag = false;
    ui_btn2_click();
  }
}

// Reinicio manual comun al menu y Serial. El aviso se dibuja ANTES de esperar.
// No iniciar mas operaciones AT/HTTP mientras el reinicio esta pendiente.
void requestSystemRestart() {
  if (systemRestartPending) return;
  prefs.begin("system", false);
  prefs.remove("netRecovery"); // Reinicio manual: no reanudar muestreo antiguo.
  prefs.putBool("streaming", false);
  prefs.putUInt("sendCnt", sendCounter);
  prefs.putUInt("sdCnt", sdSaveCounter);
  prefs.putString("csvFile", csvFileName);
  prefs.putBool("modemCycle", true); // Comprobar salud antes de recuperar; un uso.
  prefs.end();
  systemRestartPending = true;
  systemRestartRequestedMs = millis();
  showMessage("REINICIO + REVISION");
  renderDisplay();
  Serial.println("[SYSTEM] Reinicio ESP32; comprobar modem antes de solicitar ciclo");
}

bool serviceSystemRestart() {
  if (!systemRestartPending) return false;
  if (millis() - systemRestartRequestedMs >= 750) {
    Serial.flush();
    ESP.restart();
  }
  return true;
}

// Pulso azul comun a encendido y retorno del reset. Solo activo durante setup.
void bootLedTick() {
  if (!bootLedActive || !config.ledEnabled) return;
  const uint32_t now = millis();
  if (now - bootLedLastMs < 300) return;
  bootLedLastMs = now;
  bootLedOn = !bootLedOn;
  pixels.setPixelColor(0, pixels.Color(0, 0, bootLedOn ? 80 : 0));
  pixels.show();
}

void bootWait(uint32_t durationMs) {
  const uint32_t start = millis();
  while (millis() - start < durationMs) {
    bootLedTick();
    delay(10);
  }
}

// Solo consultas: no cerrar PDP ni enviar mediciones para comprobar conectividad.
bool probeExistingModemData() {
  String response;
  const bool attached = sendAtSync("+CGATT?", response, 2000) &&
                        response.indexOf("+CGATT: 1") >= 0;
  const bool pdpReady = attached && modem.isGprsConnected();
  Serial.printf("[BOOT][NET] Sesion previa: attach=%u PDP=%u\n", attached, pdpReady);
  return pdpReady;
}

// TCP al host de API_BASE: verifica ruta/DNS/servidor sin insertar una medicion.
// Un fallo TCP puede ser del servidor; no implica que el modem deba apagarse.
bool probeBootInternet() {
  String host = String(API_BASE);
  uint16_t port = 80;
  if (host.startsWith("http://")) host.remove(0, 7);
  else if (host.startsWith("https://")) { host.remove(0, 8); port = 443; }
  else return false;
  const int slash = host.indexOf('/');
  if (slash >= 0) host = host.substring(0, slash);
  const int colon = host.indexOf(':');
  if (colon >= 0) {
    port = host.substring(colon + 1).toInt();
    host = host.substring(0, colon);
  }
  if (!host.length() || !port) return false;
  oledStatus("NET", "Comprobando acceso", host);
  // Instancia persistente: TinyGSM conserva un puntero al cliente del socket.
  static TinyGsmClient probeClient(modem, 0);
  const bool reachable = probeClient.connect(host.c_str(), port, 5);
  probeClient.stop(1000);
  Serial.printf("[BOOT][NET] TCP %s:%u = %s (sin enviar mediciones)\n",
                host.c_str(), port, reachable ? "OK" : "NO DISPONIBLE");
  return reachable;
}

// Pulso que ya utilizaba esta placa (GPIO4 HIGH 300 ms, luego LOW).
// PWRKEY solicita encendido; no equivale a cortar la alimentacion del modem.
void pulseModemPowerKey() {
  Serial.println("[MODEM] Pulso PWRKEY; esperar UART antes de reintentar");
  digitalWrite(MODEM_PWRKEY, HIGH);
  bootWait(300);
  digitalWrite(MODEM_PWRKEY, LOW);
}

// Recuperacion solo tras comprobar que no hay respuesta AT.
// Con AT se usa apagado ordenado; sin AT se usa PWRKEY largo (3 s).
// No hay STATUS ni corte de VBAT conectado: esto no prueba que se apago.
void cycleModemForNetworkRecovery() {
  oledStatus("MODEM", "Recuperacion de red", "Apagando...");
  if (modem.testAT(1000)) {
    if (!atRun("+CPOF", "OK", "ERROR", 3000)) {
      digitalWrite(MODEM_PWRKEY, HIGH);
      bootWait(3000);
      digitalWrite(MODEM_PWRKEY, LOW);
    }
  } else {
    digitalWrite(MODEM_PWRKEY, HIGH);
    bootWait(3000);
    digitalWrite(MODEM_PWRKEY, LOW);
  }
  // Hacer visible la espera: SIM7600 tarda unos 26 s en completar el apagado.
  for (uint8_t seconds = 30; seconds > 0; --seconds) {
    oledStatus("MODEM", "Apagado solicitado",
               String("Esperar ") + String(seconds) + "s");
    if (seconds % 5 == 0)
      Serial.printf("[MODEM] Espera de apagado: %u s\n", seconds);
    bootWait(1000);
  }
  pulseModemPowerKey();
  oledStatus("MODEM", "Encendiendo...", "Esperando UART");
}

bool waitForModemAtWindow(uint32_t windowMs) {
  const uint32_t started = millis();
  while (millis() - started < windowMs) {
    bootLedTick();
    if (modem.testAT(1000)) return true;
    bootWait(200);
  }
  return false;
}

// Un unico estado de conexion para el arranque, HTTP y la vigilancia periodica.
// El timer se borra solo al recuperar PDP; un HTTP 500 no es perdida celular.
void updateNetworkConnection(bool connected) {
  hasRed = connected;
  networkError = !connected;
  if (connected) {
    offlineTracking = false;
  } else if (!offlineTracking) {
    offlineSinceMs = millis();
    offlineTracking = true;
  }
}

void networkRecoveryTick() {
  const uint32_t now = millis();
  // Consulta corta, sin gprsConnect ni pulsos PWRKEY dentro del loop normal.
  // No interferir con una sesion AT ya iniciada por GNSS.
  if (modemReady && !at.active &&
      now - lastNetworkHealthMs >= NETWORK_HEALTH_PERIOD_MS) {
    lastNetworkHealthMs = now;
    String response;
    const bool pdpOnline = sendAtSync("+NETOPEN?", response, 2000) &&
                           response.indexOf("+NETOPEN: 1") >= 0;
    updateNetworkConnection(pdpOnline);
  }
  if (!offlineTracking ||
      millis() - offlineSinceMs < NETWORK_REBOOT_AFTER_MS) return;

  // Los escritores SD cierran cada archivo. Guardar estado antes del reinicio
  // para no perder un muestreo que el usuario dejo activo.
  logError("NET_RECOVERY", "30min", "Reinicio tras 30 min sin PDP");
  prefs.begin("system", false);
  prefs.putBool("recoverTx", streaming);
  prefs.putBool("recoverLog", loggingEnabled);
  prefs.putUInt("sendCnt", sendCounter);
  prefs.putUInt("sdCnt", sdSaveCounter);
  prefs.putString("csvFile", csvFileName);
  prefs.putBool("netRecovery", true); // Marca escrita al final.
  prefs.end();
  oledStatus("NET", "30 min sin conexion", "Reiniciando...");
  Serial.println("[NET] Recuperacion programada: reiniciar ESP32");
  Serial.flush();
  ESP.restart();
}

// -------------------- SETUP --------------------
// Inicializa hardware, configuración persistente y servicios base del firmware.
// Define estado de arranque seguro y prepara módem/GNSS/SD/UI para operación.
void setup() {
  delay(300);
  Serial.begin(115200);
  //pinMode(POWER_PIN, OUTPUT);//esto no se usa ahora
  //digitalWrite(POWER_PIN, HIGH);//para ver con mini madre

  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 50, 100)); // Blue startup
  pixels.show();

  Serial.println("\n[BOOT] FirmwarePro " + VERSION);
  checkRebootReason();
  Serial.println("[BOOT] Motivo: " + rebootReason);

  prefs.begin("system", false);
  sendCounter = prefs.getUInt("sendCnt", 0);
  sdSaveCounter = prefs.getUInt("sdCnt", 0);
  csvFileName = prefs.getString("csvFile", "");
  wasStreamingBeforeBoot = prefs.getBool("streaming", false);
  // Marca de un solo uso: solo reanudar un reinicio de recuperacion de red.
  const bool modemCycleMarker = prefs.getBool("modemCycle", false);
  modemRecoveryOnBoot = (modemCycleMarker && rebootReason == "Software") ||
      rebootReason == "Panic" || rebootReason == "IntWatchdog" ||
      rebootReason == "TaskWatchdog" || rebootReason == "OtherWatchdog";
  if (modemCycleMarker) prefs.remove("modemCycle");
  const bool networkRecoveryMarker = prefs.getBool("netRecovery", false);
  resumeAfterNetworkRecovery = networkRecoveryMarker && rebootReason == "Software";
  recoveryStreaming = prefs.getBool("recoverTx", false);
  recoveryLogging = prefs.getBool("recoverLog", false);
  if (networkRecoveryMarker) prefs.remove("netRecovery");
  prefs.end();

  // Estado runtime por defecto.
  streaming = false;
  loggingEnabled = false;

  selectDeviceApiProfile();
  loadConfig();
  applyLEDConfig();
  bootLedActive = true;
  bootLedLastMs = millis() - 300;
  bootLedTick();

  // Create Log Paths
  logFilePath = String("/errors_h") + String(DEVICE_ID_STR) + String(".csv");
  failedTxPath = String("/failed_h") + String(DEVICE_ID_STR) + String(".csv");

  // SSID
  AP_SSID_STR = "HIRIPRO_" + String(DEVICE_ID_STR);

  // Display Init
  u8g2.begin();
  u8g2.setDisplayRotation(U8G2_R2);
  u8g2.setFont(u8g2_font_5x7_tf);
  lastOledActivity = millis();

  // Animation
  while (logoXOffset < LOGO_FINAL_X || hiriXOffset > HIRI_FINAL_X ||
         proYOffset > PRO_FINAL_Y) {
    if (logoXOffset < LOGO_FINAL_X)
      logoXOffset += 4;
    if (hiriXOffset > HIRI_FINAL_X)
      hiriXOffset -= 4;
    if (proYOffset > PRO_FINAL_Y)
      proYOffset -= 1;
    drawAnimation();
    bootWait(20);
  }

  // Show Version
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(58, 9, VERSION.c_str());
  u8g2.setCursor(0, 55);
  u8g2.print("ID:" + String(DEVICE_ID_STR));
  u8g2.sendBuffer();

  // PMS & SDS198
  pms.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, 39, -1); // SDS198

  // RTC
  if (!rtc.begin()) {
    Serial.println("[RTC] FAIL");
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:FAIL");
  } else {
    rtcOK = true;
    u8g2.setCursor(0, 64);
    u8g2.print("RTC:OK");
  }
  u8g2.sendBuffer();

  // SHT31
  if (!sht31.begin(0x44)) {
    Serial.println("[SHT31] FAIL");
    SHT31OK = false;
  } else {
    Serial.println("[SHT31] OK");
    SHT31OK = true;
    u8g2.print(" SHT31:OK");
  }
  u8g2.sendBuffer();
  bootWait(1000);

  // BUTTONS (Interrupts)
  pinMode(BUTTON_PIN_1, INPUT_PULLUP);
  pinMode(BUTTON_PIN_2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_1), isr_btn1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN_2), isr_btn2, FALLING);

  // MODEM: configurar control antes de consultar AT.
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW); // Reposo usado por esta placa.
  pinMode(MODEM_FLIGHT, OUTPUT);
  digitalWrite(MODEM_FLIGHT, HIGH);
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);

  // ESP.restart/watchdog no corta VBAT: comprobar salud ANTES de CPOF/PWRKEY.
  Serial.println("[BOOT] Verificacion AT antes de cualquier ciclo del modem");
  oledStatus("MODEM", "Comprobando AT...");
  modemReady = waitForModemAtWindow(MODEM_STRIKE_WINDOW_MS);
  bool internetProbed = false;
  bool internetReachable = false;
  bool modemWasCycled = false;
  if (modemReady) {
    hasRed = probeExistingModemData();
    if (hasRed) {
      internetReachable = probeBootInternet();
      internetProbed = true;
      oledStatus("MODEM", "AT/PDP OK", internetReachable ? "Acceso API OK" : "API no disponible",
                 "Conservar modem");
    } else {
      Serial.println("[BOOT] AT responde; recuperar red/PDP sin apagar modem");
    }
  } else if (resumeAfterNetworkRecovery || modemRecoveryOnBoot) {
    Serial.println("[BOOT] Sin AT tras espera: solicitar ciclo de recuperacion");
    cycleModemForNetworkRecovery();
    modemWasCycled = true;
    modemReady = waitForModemAtWindow(MODEM_STRIKE_WINDOW_MS);
  }
  if (!modemReady) {
    pulseModemPowerKey();
    for (uint16_t strike = 1; strike <= CONNECTION_MAX_STRIKES; ++strike) {
      oledStatus("MODEM", "Esperando arranque",
                 String("Intento ") + String(strike) + "/" +
                     String(CONNECTION_MAX_STRIKES));
      if (waitForModemAtWindow(MODEM_STRIKE_WINDOW_MS)) {
        modemReady = true;
        break;
      }
      Serial.printf("[MODEM] Strike %u/%u: sin AT tras %lu ms\n",
                    strike, CONNECTION_MAX_STRIKES, MODEM_STRIKE_WINDOW_MS);
      // Conservar reintentos PWRKEY del original, pero separados por 45 s.
      // No pulsar mientras el modem responde ni despues del ultimo strike.
      if (strike < CONNECTION_MAX_STRIKES &&
          strike % MODEM_PWRKEY_RETRY_EVERY == 0) {
        pulseModemPowerKey();
      }
    }
  }

  if (modemReady) {
    bootLedTick();
    oledStatus("MODEM", "AT OK");
    atRun("+CEDRXS=0", "OK", "ERROR", 1500);
    atRun("+CPSMS=0", "OK", "ERROR", 1500);

    for (uint16_t strike = 0; !hasRed && strike < CONNECTION_MAX_STRIKES; ++strike) {
      oledStatus("NET", "Attach/PDP...",
                 String("Intento ") + String(strike + 1) + "/" +
                     String(CONNECTION_MAX_STRIKES));
      // No cerrar/reabrir un PDP que ya sobrevivio al reinicio del ESP32.
      if (modem.waitForNetwork(NETWORK_ATTACH_TIMEOUT_MS) &&
          (modem.isGprsConnected() || modem.gprsConnect(apn, gprsUser, gprsPass))) {
        hasRed = true;
        break;
      }
      Serial.printf("[NET] Strike %u/%u\n", strike + 1,
                    CONNECTION_MAX_STRIKES);
    }
    networkError = !hasRed;
    oledStatus("NET", hasRed ? "PDP OK" : "Sin conexion");

    if (hasRed && !internetProbed) internetReachable = probeBootInternet();

    // XTRA requiere internet; GNSS puede funcionar sin red celular.
    // En reset con modem conservado, evitar otra descarga XTRA de hasta 120 s.
    const bool warmReset = rebootReason == "Software" || modemRecoveryOnBoot;
    if (hasRed && internetReachable && (!warmReset || modemWasCycled)) {
      xtraSupported = detectAndEnableXtra();
      if (xtraSupported) {
        oledStatus("XTRA", "Downloading...");
        xtraLastOk = downloadXtraOnce();
        lastXtraDownload = millis();
      }
    }
    gnssBringUp();
  } else {
    networkError = true;
    csq = 99;
    oledStatus("MODEM", "Sin respuesta", "Modo sin conexion");
  }
  if (!hasRed) {
    Serial.println("[BOOT] Sin conexion: continuar al modo normal");
  }
  displayState = DISP_NORMAL;

  // Watchdog
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  // SD Auto Mount
  // Política actual:
  // - Verificar SD al inicio.
  // - Si antes estaba activo y el reinicio fue "solo" (no SW manual),
  //   reanudar streaming+logging.
  if (config.sdAutoMount || wasStreamingBeforeBoot ||
      (resumeAfterNetworkRecovery && recoveryLogging)) {
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);
    if (SDOK) {
      if (!csvFileName.length() || !SD.exists(csvFileName.c_str())) {
        csvFileName = generateCSVFileName();
        writeCSVHeader();
      }

      prefs.begin("system", false);
      prefs.putString("csvFile", csvFileName);
      prefs.end();

      // Autoresume SOLO en reinicios claramente inesperados por watchdog/panic.
      // Evita retomar streaming tras reinicios manuales, power-on o estados
      // ambiguos.
      bool rebootWasUnexpected =
          (rebootReason == "Panic" || rebootReason == "IntWatchdog" ||
           rebootReason == "TaskWatchdog" || rebootReason == "OtherWatchdog");

      if (wasStreamingBeforeBoot && rebootWasUnexpected) {
        streaming = true;
        loggingEnabled = true;
        writeErrorLogHeader();
        Serial.println(
            "[BOOT] Auto-resume enabled (previous state + unexpected reboot)");
      } else {
        Serial.println("[BOOT] SD detected. Streaming/logging remain OFF");
      }
    }
  }

  if (resumeAfterNetworkRecovery) {
    streaming = recoveryStreaming;
    loggingEnabled = recoveryLogging && SDOK;
    if (loggingEnabled) writeErrorLogHeader();
    Serial.printf("[BOOT] Recuperacion de red: HTTP=%u SD=%u\n",
                  streaming, loggingEnabled);
  }
  updateNetworkConnection(hasRed);
  lastNetworkHealthMs = millis();
  bootLedActive = false;
  updatePmLed((float)PM25);
  Serial.println("[READY] Loop starting");
}

// -------------------- LOOP --------------------
bool FirstLoop = true;
// Bucle principal no bloqueante: sensores, UI, watchdog y scheduler de tareas.
// Ejecuta guardado SD y transmisión HTTP en timers separados por configuración.
void loop() {
  esp_task_wdt_reset();
  if (serviceSystemRestart()) return;

  // Button flags
  // Button Logic (State Check & Dispatch)
  handleButtonLogic();
  if (serviceSystemRestart()) return;

  if (wifiModeActive) {
    // No reiniciar durante gestion de archivos; dar 30 min al salir de WiFi.
    if (offlineTracking) offlineSinceMs = millis();
    // Modo WiFi Exclusivo:
    // 1. Procesa DNS (Portal Cautivo)
    // 2. Procesa WebServer
    // 3. Mantiene refresco mínimo de pantalla (para no congelar UI)
    // 4. Mantiene lectura mínima de GPS si hay FIX (para no perderlo/saturar buffer), pero sin logica pesada.
    
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Mantener GPS vivo (vaciar buffer) si ya teníamos FIX, para no perderlo al salir.
    // No procesamos la data completa para ahorrar CPU, solo lectura básica si es necesario 
    // o dejamos que el buffer maneje lo suyo. 
    // En este caso, simplemente NO lo apagamos. El módulo sigue encendido.
    // Si queremos mantener el buffer limpio:
    if (haveFix) {
       // Opcional: leer y descartar o procesar mínimo. 
       // Por ahora, confiamos en que el módulo sigue con energía.
       // Solo llamamos al watchdog del GNSS para que no crea que se colgó si implementamos timeout.
       gnssWatchdog(); 
    }

    static uint32_t lastWifiDisp = 0;
    if (millis() - lastWifiDisp > 500) {
       lastWifiDisp = millis();
       renderDisplay();
    }
    return; 
  }

  networkRecoveryTick();

  // Sensors & GNSS
  if (modemReady) {
    gnssWatchdog();
    gnssDiagTick();
    gnssDebugPollAsync();
  }

  // RTC temperature refresh
  static uint32_t lastRtcTempMs = 0;
  if (rtcOK && (millis() - lastRtcTempMs >= 1000)) {
    lastRtcTempMs = millis();
    rtcTempC = rtc.getTemperature();
  }

  // First Loop Logic
  if (FirstLoop) {
    if (modemReady) {
      csq = modem.getSignalQuality();
      updateNetworkInfo();
    }
    FirstLoop = false;
  }

  // PMS & SDS
  readPMS();
  // Mantener RGB actualizado con PM2.5 aun cuando no haya transmisión HTTP.
  static uint32_t lastLedUpdateMs = 0;
  if (millis() - lastLedUpdateMs >= 300) {
    lastLedUpdateMs = millis();
    updatePmLed((float)PM25);
  }
  byte frame[10];
  if (readFrameSDS198(frame)) {
    SDS198PM100 = (uint16_t)((frame[5] << 8) | frame[4]);
    Serial.print("PM100: ");
    Serial.println(SDS198PM100);
  }

  // AT Tick
  static uint32_t lastAtTick = 0;
  if (millis() - lastAtTick >= 50) {
    lastAtTick = millis();
    bool d, o;
    (void)atTick(d, o);
  }

  // Battery Sampling
  if (millis() - lastBatSample >= BAT_SAMPLE_INTERVAL_MS) {
    lastBatSample = millis();
    batSampleSum += analogRead(BAT_PIN);
    batSampleCount++;
    if (batSampleCount >= NUM_SAMPLES) {
      float rawV = (batSampleSum / NUM_SAMPLES / 4095.0f) * 3.3f * 2.0f * 1.15f;
      if (batteryVoltageAverage == 0)
        batteryVoltageAverage = rawV;
      batteryVoltageAverage =
          (alpha * rawV) + ((1.0 - alpha) * batteryVoltageAverage);
      batV = batteryVoltageAverage;
      batSampleSum = 0;
      batSampleCount = 0;
    }
  }

  // Display Update
  static uint32_t lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 60) {
    lastDisplayUpdate = millis();
    renderDisplay();
  }

  // Auto Off
  if (config.oledAutoOff &&
      (millis() - lastOledActivity > config.oledTimeout)) {
    u8g2.setPowerSave(1);
  }

  // Guardado SD (separado de transmisión HTTP)
  // Nota: por diseño de esta etapa, loggingEnabled inicia en false.
  if (loggingEnabled && (millis() - lastSdSave >= config.sdSavePeriod)) {
    lastSdSave = millis();
    bool sdSaved = saveCSVData();
    lastSdActivityMs = millis();
    lastSdOk = sdSaved;
    // Sin pantalla emergente de "guardado": se usan indicadores del header/RGB.
  }

  // Transmisión HTTP (separada de guardado SD)
  if (streaming && (millis() - lastHttpSend >= config.httpSendPeriod)) {
    lastHttpSend = millis();
    bool txOk = sendCurrentMeasurement();
    lastHttpActivityMs = millis();
    lastHttpOk = txOk;
  }

  // Serial Commands
  processSerialCommand();
}
