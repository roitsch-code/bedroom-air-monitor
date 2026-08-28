// Bedroom Air Monitor — sensor node (Adafruit QT Py ESP32-S2)
//
// Reads an Adafruit SCD-41 (CO₂ / temp / humidity) and a Sensirion SEN54 (fine dust / VOC) over
// the shared STEMMA QT I²C bus, serves the current reading as JSON on the LAN (GET /now), and
// optionally POSTs it to a logging endpoint every few minutes.
//
// The SCD-41 runs continuously (it's silent). The SEN54's fan is audible, so by default it only
// spins up for a short burst every SEN54_BURST_MINUTES to sample dust — see config.h.
//
// Libraries (Arduino Library Manager): "Sensirion I2C SCD4x", "Sensirion I2C SEN5X", "ArduinoJson".
// Board: Adafruit QT Py ESP32-S2.

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SensirionI2cScd4x.h>
#include <SensirionI2CSen5x.h>
#include "config.h"

SensirionI2cScd4x scd4x;
SensirionI2CSen5x sen5x;
WebServer server(80);

// Latest readings. NAN = "not measured", so we never serve a fabricated number.
struct Reading {
  float co2 = NAN, tempC = NAN, rh = NAN;   // SCD-41
  float pm25 = NAN, voc = NAN;              // SEN54
  uint32_t co2Age = 0, pmAge = 0;           // millis() of last successful read
} latest;

bool sen54Running = false;
uint32_t sen54BurstStart = 0;
uint32_t lastPostMs = 0;

// The SEN54's on-chip VOC index algorithm resets to its initial ("still learning", reports 0)
// state every time measurement is stopped and restarted — confirmed in Sensirion's own library
// docs: "stopping and restarting the measure mode will reset the state to initial values." In
// intermittent mode we stop after every single burst, so without saving/restoring this state the
// index would never leave 0. Carried in RAM across bursts (not across a reboot — that's fine, it
// just re-learns from scratch after a power cycle, same as day one).
uint8_t vocAlgState[8] = {0};
bool vocAlgStateValid = false;

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
void connectWiFi() {
  WiFi.mode(WIFI_STA);
#ifdef STATIC_IP
  IPAddress ip(STATIC_IP), gw(GATEWAY_IP), sn(SUBNET_IP);
  WiFi.config(ip, gw, sn);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Wi-Fi connecting");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. Sensor is at http://"); Serial.print(WiFi.localIP()); Serial.println("/now");
  } else {
    Serial.println("Wi-Fi failed — will keep retrying in the loop.");
  }
}

// ── JSON of the current reading (shared by /now and the POST) ────────────────
String readingJson() {
  StaticJsonDocument<256> doc;
  if (!isnan(latest.co2))   doc["co2"]      = round(latest.co2);
  if (!isnan(latest.tempC)) doc["temp"]     = round(latest.tempC * 10) / 10.0;
  if (!isnan(latest.rh))    doc["humidity"] = round(latest.rh);
  if (!isnan(latest.pm25))  doc["pm25"]     = round(latest.pm25 * 10) / 10.0;
  if (!isnan(latest.voc))   doc["voc"]      = round(latest.voc);
  doc["placement"] = PLACEMENT;
  doc["uptime_s"]  = millis() / 1000;
  String out; serializeJson(doc, out); return out;
}

void handleNow() {
  server.sendHeader("Access-Control-Allow-Origin", "*");   // let a browser/other LAN device read it
  server.send(200, "application/json", readingJson());
}

// ── Sensors ──────────────────────────────────────────────────────────────────
void startSensors() {
  // On the QT Py ESP32-S2 the STEMMA QT connector is Wire1 (SDA1=41, SCL1=40), NOT the default
  // Wire (which is the castellated side pads). Using Wire here leaves the sensors powered but
  // silent — a green LED with no I2C. The pins must be set explicitly on this board.
  Wire1.setPins(SDA1, SCL1);
  Wire1.begin();

  scd4x.begin(Wire1, SCD41_I2C_ADDR_62);
  scd4x.stopPeriodicMeasurement();                 // clean state after a reset
  delay(500);                                       // the SCD4x needs ~500 ms before it will start
  scd4x.setAutomaticSelfCalibrationEnabled(CO2_AUTO_CALIBRATION);
  scd4x.setTemperatureOffset(CO2_TEMP_OFFSET_C);
  scd4x.startPeriodicMeasurement();                // a reading roughly every 5 s, silent

  sen5x.begin(Wire1);
  sen5x.deviceReset();
  sen5x.setTemperatureOffsetSimple(0.0f);          // we take temp/RH from the SCD-41, not this one
#if !SEN54_INTERMITTENT
  sen5x.startMeasurement();                         // continuous: fan always on
  sen54Running = true;
#endif
}

void readScd4x() {
  uint16_t co2; float t, rh; bool ready = false;
  if (scd4x.getDataReadyStatus(ready) != 0 || !ready) return;
  if (scd4x.readMeasurement(co2, t, rh) != 0) return;
  if (co2 == 0) return;                             // SCD-41 reports 0 for an invalid sample
  latest.co2 = co2; latest.tempC = t; latest.rh = rh; latest.co2Age = millis();
}

void readSen5x() {
  float pm1, pm25, pm4, pm10, humidity, temp, voc, nox;
  if (sen5x.readMeasuredValues(pm1, pm25, pm4, pm10, humidity, temp, voc, nox) != 0) return;
  if (!isnan(pm25)) { latest.pm25 = pm25; latest.pmAge = millis(); }
  if (!isnan(voc))  latest.voc = voc;
}

// Intermittent dust sampling: spin the fan up, let it stabilise, read once, spin it down.
void serviceSen54Burst() {
#if SEN54_INTERMITTENT
  uint32_t now = millis();
  if (!sen54Running) {
    if (now - latest.pmAge >= (uint32_t)SEN54_BURST_MINUTES * 60000UL || latest.pmAge == 0) {
      // Restore the learned VOC state BEFORE starting — setVocAlgorithmState only takes effect in
      // idle mode and is applied once when the next measurement starts (Sensirion's own docs).
      // Logged, not silently ignored (§9) — a failed restore would look identical to "still
      // learning" from the outside, and that's exactly the bug we're chasing.
      if (vocAlgStateValid) {
        uint16_t err = sen5x.setVocAlgorithmState(vocAlgState, 8);
        Serial.print("SEN54: VOC state restore -> "); Serial.println(err == 0 ? "ok" : String("ERROR " + String(err)));
      }
      sen5x.startMeasurement(); sen54Running = true; sen54BurstStart = now;
      Serial.println("SEN54: fan burst started");
    }
  } else if (now - sen54BurstStart >= (uint32_t)SEN54_WARMUP_SECONDS * 1000UL) {
    readSen5x();
    // Save the state BEFORE stopping, so the next burst can resume instead of relearning from 0.
    uint16_t err = sen5x.getVocAlgorithmState(vocAlgState, 8);
    if (err == 0) vocAlgStateValid = true;
    Serial.print("SEN54: VOC state save -> "); Serial.println(err == 0 ? "ok" : String("ERROR " + String(err)));
    sen5x.stopMeasurement(); sen54Running = false;
    Serial.println("SEN54: sampled, fan stopped");
  }
#endif
}

// ── Optional logging POST ────────────────────────────────────────────────────
void maybePost() {
  if (strlen(POST_URL) == 0) return;
  uint32_t now = millis();
  if (now - lastPostMs < (uint32_t)POST_MINUTES * 60000UL) return;
  lastPostMs = now;
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(POST_URL);
  http.addHeader("Content-Type", "application/json");
  if (strlen(POST_SECRET) > 0) http.addHeader("x-ingest-secret", POST_SECRET);
  int code = http.POST(readingJson());
  Serial.print("POST -> "); Serial.println(code);
  http.end();
}

// ── Arduino entry points ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  connectWiFi();
  startSensors();
  server.on("/now", handleNow);
  server.begin();
  Serial.println("HTTP server up on /now");
}

void loop() {
  server.handleClient();
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  static uint32_t lastScd = 0;
  if (millis() - lastScd >= 5000) { lastScd = millis(); readScd4x(); }
#if !SEN54_INTERMITTENT
  static uint32_t lastPm = 0;
  if (millis() - lastPm >= 5000) { lastPm = millis(); readSen5x(); }
#endif
  serviceSen54Burst();
  maybePost();
}
