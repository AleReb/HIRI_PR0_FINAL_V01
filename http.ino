// -------------------- HTTP helpers --------------------
#include "config.h"
// -------------------- External Variables --------------------
extern TinyGsm modem;
extern struct AtSession at;
extern Adafruit_NeoPixel pixels;
extern const char apn[];
extern const char gprsUser[];
extern const char gprsPass[];
extern bool hasRed;
extern bool modemReady;
extern void updateNetworkConnection(bool connected);
extern float batV;
extern uint16_t PM25;
extern SystemConfig config;
extern void updatePmLed(float pm25);
extern void logError(const String &type, const String &ctx, const String &msg);
extern bool atTick(bool &done, bool &ok);
extern bool atRun(const String &cmd, const String &expect1,
                  const String &expect2, uint32_t timeout_ms);
extern bool sendAtSync(const String &cmd, String &resp, uint32_t timeout_ms);

// Un intento PDP por llamada, con pausa entre fallos.
// TinyGSM es bloqueante (NETCLOSE hasta 60 s, NETOPEN hasta 75 s).
// El watchdog de 180 s permite que ese intento retorne sin reset prematuro.
static bool pdpReconnectBackoff = false;
static uint32_t lastPdpReconnectFailure = 0;
const uint32_t PDP_BACKOFF_MS = 60000;

bool ensurePdpAndNet() {
  if (!modemReady) {
    updateNetworkConnection(false);
    return false;
  }
  if (pdpReconnectBackoff &&
      millis() - lastPdpReconnectFailure < PDP_BACKOFF_MS) return false;

  esp_task_wdt_reset();
  if (modem.isGprsConnected()) {
    pdpReconnectBackoff = false;
    updateNetworkConnection(true);
    return true;
  }
  updateNetworkConnection(false);
  Serial.println("[NET] PDP down: un intento de reconexion");
  // Usar el APN configurado; no sustituirlo por un literal de otra SIM.
  const bool connected = modem.gprsConnect(apn, gprsUser, gprsPass);
  esp_task_wdt_reset();
  updateNetworkConnection(connected);
  if (!connected) {
    pdpReconnectBackoff = true;
    lastPdpReconnectFailure = millis();
    Serial.println("[NET] PDP FAIL: siguiente intento en al menos 60 s");
    return false;
  }
  pdpReconnectBackoff = false;
  Serial.println("[NET] PDP reconnected OK");
  return true;
}

// Helper: parsear +HTTPACTION: 0,200,123
// Parsea la URC +HTTPACTION para extraer código HTTP y tamaño de respuesta.
// Centraliza parsing defensivo del módem SIM7600.
void parseHttpActionResponse(const String &resp, int &code, int &dataLen) {
  code = -1;
  dataLen = -1;
  int p = resp.indexOf("+HTTPACTION:");
  int c1 = resp.indexOf(',', p);
  int c2 = resp.indexOf(',', c1 + 1);
  if (p >= 0 && c1 > 0 && c2 > c1) {
    code = resp.substring(c1 + 1, c2).toInt();
    dataLen = resp.substring(c2 + 1).toInt();
  }
}

// -------------------- HTTP GET (blocking, stable) --------------------
// Ejecuta ciclo HTTP completo (INIT/PARA/ACTION/READ/TERM) con timeout y watchdog.
// Devuelve true solo para respuestas 2xx y registra errores detallados.
bool httpGet_webhook(const String &fullUrl) {
  Serial.printf("[HTTP][SYNC] URL length = %d\n", fullUrl.length());
  if (fullUrl.length() > 512) {
    Serial.println("[HTTP][WARN] URL >512 chars; SIM7600 +HTTPPARA may fail.");
  }

  if (!ensurePdpAndNet()) {
    logError("HTTP_PDP_FAIL", "ensurePdpAndNet", "PDP/NET setup failed");
    return false;
  }

  bool done = false, ok = false;
  (void)atTick(done, ok);

  (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
  if (!atRun("+HTTPINIT", "OK", "ERROR", 5000)) {
    Serial.println("[HTTP][ERR] HTTPINIT FAIL");
    logError("HTTP_INIT_FAIL", "HTTPINIT", at.resp);
    return false;
  }
  if (!atRun("+HTTPPARA=\"CID\",1", "OK", "ERROR", 2000)) {
    Serial.println("[HTTP][ERR] HTTPPARA CID FAIL");
    logError("HTTP_CID_FAIL", "HTTPPARA_CID", at.resp);
    (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
    return false;
  }
  {
    String cmd = "+HTTPPARA=\"URL\",\"" + fullUrl + "\"";
    if (!atRun(cmd, "OK", "ERROR", 6000)) {
      Serial.println("[HTTP][ERR] Set URL FAIL");
      logError("HTTP_URL_FAIL", "HTTPPARA_URL", at.resp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      return false;
    }
  }

  // Apagar LED para ahorrar energía durante transmisión
  pixels.setPixelColor(0, pixels.Color(0, 0, 0));
  pixels.show();

  atBegin("+HTTPACTION=0", "+HTTPACTION:", "ERROR", 90000);
  int httpCode = -1, dataLen = -1;
  {
    bool actionDone = false, actionOk = false;
    uint32_t startTime = millis();
    // Timeout HTTP configurable (config.httpTimeout en segundos)
    // fallback de seguridad: 15s si la config viene en 0.
    const uint32_t MAX_HTTP_WAIT_MS =
        (config.httpTimeout > 0 ? (uint32_t)config.httpTimeout * 1000UL : 15000UL);

    while (!actionDone) {
      esp_task_wdt_reset(); // Reset watchdog para evitar timeout durante HTTP

      if (atTick(actionDone, actionOk))
        break;

      // Check timeout
      if (millis() - startTime > MAX_HTTP_WAIT_MS) {
        uint32_t elapsed = millis() - startTime;
        Serial.printf("[HTTP][TIMEOUT] After %lu ms (bat=%.2fV)\n", elapsed,
                      batV);
        logError("HTTP_TIMEOUT", "HTTPACTION",
                 "Timeout=" + String(elapsed) + "ms bat=" + String(batV, 2) +
                     "V");
        (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
        updatePmLed((float)PM25);
        return false;
      }

      delay(1);
    }
    String actionResp = at.resp;
    if (!actionOk) {
      Serial.println("[HTTP][ERR] +HTTPACTION did not complete");
      logError("HTTP_ACTION_FAIL", "HTTPACTION", actionResp);
      (void)atRun("+HTTPTERM", "OK", "ERROR", 1500);
      // Restaurar LED antes de salir
      updatePmLed((float)PM25);
      return false;
    }
    parseHttpActionResponse(actionResp, httpCode, dataLen);
    Serial.printf("[HTTP] code=%d len=%d\n", httpCode, dataLen);
    if (httpCode == -1) {
      Serial.println("[HTTP][ERR] Could not parse +HTTPACTION");
      logError("HTTP_PARSE_FAIL", "HTTPACTION", actionResp);
    }
  }

  if (dataLen > 0) {
    String cmd = "+HTTPREAD=0," + String(dataLen);
    String readResp;
    if (sendAtSync(cmd, readResp, 8000)) {
      Serial.println("[HTTP][READ] ----------------");
      Serial.println(readResp);
      Serial.println("[HTTP][READ] ----------------");
    } else {
      Serial.println("[HTTP][WARN] HTTPREAD failed");
    }
  }

  (void)atRun("+HTTPTERM", "OK", "ERROR", 2000);

  // Restaurar LED según nivel de PM2.5
  updatePmLed((float)PM25);

  return (httpCode >= 200 && httpCode < 300);
}

