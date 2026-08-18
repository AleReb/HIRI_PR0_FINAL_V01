/*
 * HIRI - Prueba independiente de componentes
 *
 * Hardware tomado del firmware principal:
 *   - ESP32 Dev Module
 *   - OLED SSD1306 128x64 I2C (SDA 21, SCL 22, direccion 0x3C)
 *   - RTC DS3231 I2C (direccion 0x68)
 *   - NeoPixel RGB, 1 LED, GPIO 12
 *   - Boton 1 GPIO 19 y boton 2 GPIO 23 (resistencia externa, activos en HIGH)
 *   - Plantower/PMS por SoftwareSerial a 9600 baud
 *   - Divisor de voltaje de bateria en ADC GPIO 35
 *
 * Librerias necesarias:
 *   U8g2, RTClib y Adafruit NeoPixel
 *
 * Esta prueba no inicializa modem, GPS, SD ni sensores de particulas.
 */

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>

// -------------------- Pines --------------------
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t RTC_ADDRESS = 0x68;
constexpr uint8_t RGB_PIN = 12;
constexpr uint8_t RGB_COUNT = 1;
constexpr uint8_t BUTTON_1_PIN = 19;
constexpr uint8_t BUTTON_2_PIN = 23;
constexpr uint8_t BUTTON_PRESSED_LEVEL = HIGH;
constexpr uint8_t BATTERY_PIN = 35;
// Se conserva exactamente el orden usado por el firmware principal:
// SoftwareSerial pms(pms_TX, pms_RX), donde pms_TX=5 y pms_RX=18.
constexpr uint8_t PMS_SOFT_RX_PIN = 5;
constexpr uint8_t PMS_SOFT_TX_PIN = 18;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t RGB_STEP_MS = 1000;
constexpr uint32_t DISPLAY_REFRESH_MS = 200;
constexpr uint32_t PMS_FIRST_FRAME_TIMEOUT_MS = 15000;
constexpr uint32_t PMS_STALE_MS = 10000;
// Prefijo propio para evitar colisionar con RGB_BRIGHTNESS, macro definida por
// el core ESP32 2.0.17 en esp32-hal-rgb-led.h.
constexpr uint8_t TEST_RGB_BRIGHTNESS = 40;
constexpr uint8_t BATTERY_SAMPLES = 30;
constexpr uint32_t BATTERY_SAMPLE_MS = 5;
constexpr float BATTERY_FILTER_ALPHA = 0.8f;

// Mismo constructor usado por el firmware principal.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
RTC_DS3231 rtc;
Adafruit_NeoPixel rgb(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
SoftwareSerial pms(PMS_SOFT_RX_PIN, PMS_SOFT_TX_PIN);

struct ButtonState {
  uint8_t pin;
  bool stablePressed;
  bool lastRawPressed;
  uint32_t lastRawChangeMs;
  uint32_t pressCount;
};

ButtonState button1 = {BUTTON_1_PIN, false, false, 0, 0};
ButtonState button2 = {BUTTON_2_PIN, false, false, 0, 0};

bool oledFound = false;
uint8_t oledAddress = 0;
bool rtcFound = false;
bool rtcLostPower = false;
bool rgbAutomatic = true;
uint8_t colorIndex = 0;
uint32_t lastRgbStepMs = 0;
uint32_t lastDisplayMs = 0;
uint8_t pmsBuffer[64];
size_t pmsBufferLength = 0;
uint16_t pm1 = 0;
uint16_t pm25 = 0;
uint16_t pm10 = 0;
float pmsTemperature = NAN;
float pmsHumidity = NAN;
bool pmsFrameOk = false;
uint32_t lastPmsFrameMs = 0;
uint32_t pmsValidFrames = 0;
uint32_t pmsChecksumErrors = 0;
uint32_t lastBatterySampleMs = 0;
uint32_t lastBatteryPrintMs = 0;
uint32_t batterySampleSum = 0;
uint8_t batterySampleCount = 0;
float batteryVoltage = 0.0f;
bool batteryReadingReady = false;

const char *COLOR_NAMES[] = {"ROJO", "VERDE", "AZUL", "BLANCO", "APAGADO"};
constexpr uint8_t COLOR_COUNT = sizeof(COLOR_NAMES) / sizeof(COLOR_NAMES[0]);

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void scanI2cBus() {
  Serial.println("\n[I2C] Escaneando bus...");
  uint8_t devices = 0;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("[I2C] Dispositivo encontrado en 0x%02X", address);
      if (address == OLED_ADDRESS) Serial.print(" (OLED esperado)");
      if (address == RTC_ADDRESS) Serial.print(" (RTC esperado)");
      Serial.println();
      ++devices;
    }
  }

  if (devices == 0) Serial.println("[I2C] ERROR: no se encontraron dispositivos");
  oledAddress = i2cDevicePresent(OLED_ADDRESS) ? OLED_ADDRESS : 0;
  oledFound = oledAddress != 0;
  rtcFound = i2cDevicePresent(RTC_ADDRESS);
}

void showOledStartupPattern() {
  if (!oledFound) return;

  // Pantalla completamente encendida durante un instante. Si no se ve este
  // rectangulo, revisar alimentacion, SDA/SCL o el modelo del controlador.
  oled.clearBuffer();
  oled.drawBox(0, 0, 128, 64);
  oled.sendBuffer();
  delay(500);

  oled.clearBuffer();
  oled.drawFrame(0, 0, 128, 64);
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(12, 20, "OLED ENCONTRADA");
  oled.setFont(u8g2_font_5x7_tf);
  oled.setCursor(28, 36);
  oled.printf("I2C 0x%02X", oledAddress);
  oled.drawStr(22, 52, "PANTALLA: OK");
  oled.sendBuffer();
  delay(1500);
}

void setRgbColor(uint8_t index) {
  uint32_t color = 0;
  switch (index % COLOR_COUNT) {
    case 0: color = rgb.Color(255, 0, 0); break;
    case 1: color = rgb.Color(0, 255, 0); break;
    case 2: color = rgb.Color(0, 0, 255); break;
    case 3: color = rgb.Color(255, 255, 255); break;
    default: color = rgb.Color(0, 0, 0); break;
  }
  rgb.setPixelColor(0, color);
  rgb.show();
  Serial.printf("[RGB] Color: %s%s\n", COLOR_NAMES[index],
                rgbAutomatic ? " (automatico)" : " (manual)");
}

// Devuelve true solamente en el flanco de pulsacion ya estabilizado.
bool updateButton(ButtonState &button) {
  const bool rawPressed = digitalRead(button.pin) == BUTTON_PRESSED_LEVEL;
  const uint32_t now = millis();

  if (rawPressed != button.lastRawPressed) {
    button.lastRawPressed = rawPressed;
    button.lastRawChangeMs = now;
  }

  if ((now - button.lastRawChangeMs >= DEBOUNCE_MS) &&
      (rawPressed != button.stablePressed)) {
    button.stablePressed = rawPressed;
    if (button.stablePressed) {
      ++button.pressCount;
      return true;
    }
  }
  return false;
}

void printRtc() {
  if (!rtcFound) return;
  const DateTime now = rtc.now();
  Serial.printf("[RTC] %04d-%02d-%02d %02d:%02d:%02d | %.2f C%s\n",
                now.year(), now.month(), now.day(), now.hour(), now.minute(),
                now.second(), rtc.getTemperature(),
                rtcLostPower ? " | ADVERTENCIA: lostPower" : "");
}

bool pmsIsReceiving() {
  return pmsFrameOk && (millis() - lastPmsFrameMs <= PMS_STALE_MS);
}

bool batteryVoltageValid() {
  // Rango amplio para una celda Li-ion conectada al divisor de la placa.
  return batteryReadingReady && batteryVoltage >= 2.5f &&
         batteryVoltage <= 4.5f;
}

void sampleBattery() {
  const uint32_t now = millis();
  if (now - lastBatterySampleMs < BATTERY_SAMPLE_MS) return;
  lastBatterySampleMs = now;

  batterySampleSum += analogRead(BATTERY_PIN);
  ++batterySampleCount;
  if (batterySampleCount < BATTERY_SAMPLES) return;

  // Misma formula del firmware principal: ADC de 12 bits, referencia 3.3 V,
  // divisor 1:2 y factor de calibracion 1.15.
  const float rawVoltage =
      (batterySampleSum / float(BATTERY_SAMPLES) / 4095.0f) * 3.3f * 2.0f *
      1.15f;
  if (!batteryReadingReady) {
    batteryVoltage = rawVoltage;
    batteryReadingReady = true;
  } else {
    batteryVoltage = BATTERY_FILTER_ALPHA * rawVoltage +
                     (1.0f - BATTERY_FILTER_ALPHA) * batteryVoltage;
  }

  batterySampleSum = 0;
  batterySampleCount = 0;

  if (now - lastBatteryPrintMs >= 5000) {
    lastBatteryPrintMs = now;
    Serial.printf("[BATERIA] ADC GPIO35: %.2f V - %s\n", batteryVoltage,
                  batteryVoltageValid() ? "RANGO OK" : "REVISAR");
  }
}

void parsePms() {
  while (pms.available() > 0) {
    const int value = pms.read();
    if (value < 0) break;

    if (pmsBufferLength < sizeof(pmsBuffer)) {
      pmsBuffer[pmsBufferLength++] = static_cast<uint8_t>(value);
    } else {
      memmove(pmsBuffer, pmsBuffer + 1, sizeof(pmsBuffer) - 1);
      pmsBuffer[sizeof(pmsBuffer) - 1] = static_cast<uint8_t>(value);
    }
  }

  size_t offset = 0;
  while (pmsBufferLength - offset >= 32) {
    uint8_t *frame = pmsBuffer + offset;
    if (frame[0] != 0x42 || frame[1] != 0x4D) {
      ++offset;
      continue;
    }

    // Una trama Plantower de datos completa declara 28 bytes posteriores
    // al campo de longitud y ocupa 32 bytes en total.
    const uint16_t frameLength = (uint16_t(frame[2]) << 8) | frame[3];
    if (frameLength != 28) {
      ++offset;
      continue;
    }

    uint16_t calculatedChecksum = 0;
    for (uint8_t i = 0; i < 30; ++i) calculatedChecksum += frame[i];
    const uint16_t receivedChecksum =
        (uint16_t(frame[30]) << 8) | frame[31];

    if (calculatedChecksum != receivedChecksum) {
      ++pmsChecksumErrors;
      Serial.printf("[PLANTOWER] Checksum incorrecto (#%lu)\n",
                    (unsigned long)pmsChecksumErrors);
      ++offset;
      continue;
    }

    // Concentraciones atmosfericas, iguales a las usadas en pms.ino.
    pm1 = (uint16_t(frame[10]) << 8) | frame[11];
    pm25 = (uint16_t(frame[12]) << 8) | frame[13];
    pm10 = (uint16_t(frame[14]) << 8) | frame[15];

    const uint16_t temperature10 = (uint16_t(frame[24]) << 8) | frame[25];
    const uint16_t humidity10 = (uint16_t(frame[26]) << 8) | frame[27];
    const float candidateTemperature = temperature10 / 10.0f;
    const float candidateHumidity = humidity10 / 10.0f;
    if ((temperature10 != 0 || humidity10 != 0) &&
        candidateTemperature > -40.0f && candidateTemperature < 85.0f &&
        candidateHumidity >= 0.0f && candidateHumidity <= 100.0f) {
      pmsTemperature = candidateTemperature;
      pmsHumidity = candidateHumidity;
    } else {
      pmsTemperature = NAN; // Normal en modelos PMS sin sensor T/H.
      pmsHumidity = NAN;
    }

    pmsFrameOk = true;
    lastPmsFrameMs = millis();
    ++pmsValidFrames;
    Serial.printf("[PLANTOWER] OK trama=%lu PM1=%u PM2.5=%u PM10=%u",
                  (unsigned long)pmsValidFrames, pm1, pm25, pm10);
    if (!isnan(pmsTemperature) && !isnan(pmsHumidity)) {
      Serial.printf(" T=%.1fC HR=%.1f%%", pmsTemperature, pmsHumidity);
    }
    Serial.println();

    offset += 32;
  }

  if (offset > 0) {
    const size_t remaining = pmsBufferLength - offset;
    memmove(pmsBuffer, pmsBuffer + offset, remaining);
    pmsBufferLength = remaining;
  }
}

void drawStatus() {
  if (!oledFound) return;

  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tf);
  const bool requiredTestsOk = oledFound && rtcFound && !rtcLostPower &&
                               pmsIsReceiving() && batteryVoltageValid() &&
                               button1.pressCount > 0 && button2.pressCount > 0;
  oled.drawStr(0, 7, requiredTestsOk ? "TEST: TODO OK*" : "TEST COMPONENTES HIRI");
  oled.drawHLine(0, 10, 128);

  oled.setCursor(0, 20);
  oled.print("O:");
  oled.print(oledAddress, HEX);
  oled.print(" R:");
  oled.print(!rtcFound ? "FAIL " : (rtcLostPower ? "BAT! " : "OK "));
  if (rtcFound) {
    const DateTime now = rtc.now();
    char timeText[9];
    snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", now.hour(),
             now.minute(), now.second());
    oled.print(timeText);
  }

  oled.setCursor(0, 30);
  oled.print("PMS:");
  if (pmsIsReceiving()) {
    oled.print("1/25/10 ");
    oled.print(pm1);
    oled.print("/");
    oled.print(pm25);
    oled.print("/");
    oled.print(pm10);
  } else if (!pmsFrameOk && millis() < PMS_FIRST_FRAME_TIMEOUT_MS) {
    oled.print("ESPERANDO...");
  } else {
    oled.print("SIN DATOS/FAIL");
  }

  oled.setCursor(0, 40);
  oled.print("PMS T/H:");
  if (pmsIsReceiving() && !isnan(pmsTemperature) && !isnan(pmsHumidity)) {
    oled.print(pmsTemperature, 1);
    oled.print("C ");
    oled.print(pmsHumidity, 1);
    oled.print("%");
  } else if (pmsIsReceiving()) {
    oled.print("NO DISP.");
  } else {
    oled.print("---");
  }

  oled.setCursor(0, 50);
  oled.print("RGB:");
  oled.print(COLOR_NAMES[colorIndex]);
  oled.print(" BAT:");
  if (batteryReadingReady) {
    oled.print(batteryVoltage, 2);
    oled.print("V");
  } else {
    oled.print("...");
  }

  oled.setCursor(0, 62);
  oled.print("B1:");
  oled.print(button1.stablePressed ? "DOWN" : "UP");
  oled.print(" #");
  oled.print(button1.pressCount);

  oled.setCursor(65, 62);
  oled.print("B2:");
  oled.print(button2.stablePressed ? "DOWN" : "UP");
  oled.print(" #");
  oled.print(button2.pressCount);

  oled.sendBuffer();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);
  Serial.println("\n================================");
  Serial.println(" HIRI - TEST DE COMPONENTES");
  Serial.println("================================");

  // La placa ya tiene resistencias pull-down externas.
  pinMode(BUTTON_1_PIN, INPUT);
  pinMode(BUTTON_2_PIN, INPUT);
  pinMode(BATTERY_PIN, INPUT);

  rgb.begin();
  rgb.setBrightness(TEST_RGB_BRIGHTNESS);
  rgb.clear();
  rgb.show();

  pms.begin(9600);
  Serial.println("[PLANTOWER] Esperando tramas a 9600 baud (RX GPIO5, TX GPIO18)");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  scanI2cBus();

  if (oledFound) {
    // Misma inicializacion y configuracion del firmware principal.
    oled.begin();
    oled.setDisplayRotation(U8G2_R2);
    oled.setFont(u8g2_font_5x7_tf);
    Serial.printf("[OLED] Dispositivo I2C detectado en 0x%02X\n", oledAddress);
    Serial.println("[OLED] Configuracion base: SSD1306 con rotacion R2");
    Serial.println("[OLED] Mostrando patron blanco y texto de prueba");
    showOledStartupPattern();
  } else {
    Serial.println("[OLED] FAIL: no responde en 0x3C");
  }

  if (rtcFound && rtc.begin()) {
    rtcLostPower = rtc.lostPower();
    Serial.println("[RTC] OK: DS3231 detectado en 0x68");
    if (rtcLostPower) {
      Serial.println("[RTC] ADVERTENCIA: perdio energia; no se cambia la hora en este test");
    }
    printRtc();
  } else {
    rtcFound = false;
    Serial.println("[RTC] FAIL: DS3231 no encontrado");
  }

  Serial.println("[BOTONES] B1=GPIO19, B2=GPIO23, resistencia externa, activos en HIGH");
  Serial.println("[BATERIA] Midiendo divisor de voltaje en GPIO35");
  Serial.println("[AYUDA] B1 avanza el color; B2 activa/desactiva ciclo automatico");
  Serial.println("[AYUDA] Verifique visualmente RGB y OLED; el software no puede medir su luz");

  setRgbColor(colorIndex);
  drawStatus();
}

void loop() {
  const uint32_t now = millis();

  parsePms();
  sampleBattery();

  if (updateButton(button1)) {
    rgbAutomatic = false;
    colorIndex = (colorIndex + 1) % COLOR_COUNT;
    Serial.printf("[BOTON 1] OK - pulsaciones: %lu\n",
                  (unsigned long)button1.pressCount);
    setRgbColor(colorIndex);
  }

  if (updateButton(button2)) {
    rgbAutomatic = !rgbAutomatic;
    lastRgbStepMs = now;
    Serial.printf("[BOTON 2] OK - pulsaciones: %lu - RGB automatico: %s\n",
                  (unsigned long)button2.pressCount,
                  rgbAutomatic ? "SI" : "NO");
  }

  if (rgbAutomatic && now - lastRgbStepMs >= RGB_STEP_MS) {
    lastRgbStepMs = now;
    colorIndex = (colorIndex + 1) % COLOR_COUNT;
    setRgbColor(colorIndex);
  }

  if (now - lastDisplayMs >= DISPLAY_REFRESH_MS) {
    lastDisplayMs = now;
    drawStatus();
  }

  static uint32_t lastRtcPrintMs = 0;
  if (rtcFound && now - lastRtcPrintMs >= 5000) {
    lastRtcPrintMs = now;
    printRtc();
  }

  delay(5);
}
