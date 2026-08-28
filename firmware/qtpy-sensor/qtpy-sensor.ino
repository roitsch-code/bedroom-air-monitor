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

// SEN54 fan state. The gas (VOC) sensor is ALWAYS running — either in full Measurement mode
// (fan on, sampling dust) or in RHT/Gas-Only mode (fan off, but VOC/NOx/RHT keep measuring). We
// only duty-cycle the *fan*, never the whole sensor. This is Sensirion's own reduced-power design
// ("Reduced Power Operation for SEN5x") and the reason VOC works: the earlier code dropped the
// sensor to Idle between bursts, which powers the gas sensor OFF, so the VOC index — which adapts
// over hours — could never leave 0. Fan off = quiet in the bedroom; VOC alive = actually useful.
bool sen54FanOn = false;
uint32_t sen54FanStart = 0;
uint32_t lastPostMs = 0;

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
  // Start with a full-measurement fan burst either way, so PM is available quickly. In intermittent
  // mode serviceSen54() drops the fan to gas-only after warmup; in continuous mode it stays on.
  sen5x.startMeasurement();
  sen54FanOn = true; sen54FanStart = millis();
}

void readScd4x() {
  uint16_t co2; float t, rh; bool ready = false;
  if (scd4x.getDataReadyStatus(ready) != 0 || !ready) return;
  if (scd4x.readMeasurement(co2, t, rh) != 0) return;
  if (co2 == 0) return;                             // SCD-41 reports 0 for an invalid sample
  latest.co2 = co2; latest.tempC = t; latest.rh = rh; latest.co2Age = millis();
}

// Read the SEN54. VOC is always accepted (it runs continuously, even in gas-only mode). PM is only
// accepted when acceptPm is true — i.e. once per fan burst, after the fan has warmed up — so the
// unstable readings during fan spin-up never reach the display. In gas-only mode pm25 comes back
// NAN anyway (fan off), so it's never overwritten there.
void readSen5x(bool acceptPm) {
  float pm1, pm25, pm4, pm10, humidity, temp, voc, nox;
  if (sen5x.readMeasuredValues(pm1, pm25, pm4, pm10, humidity, temp, voc, nox) != 0) return;
  if (acceptPm && !isnan(pm25)) { latest.pm25 = pm25; latest.pmAge = millis(); }
  if (!isnan(voc)) latest.voc = voc;
}

// Intermittent DUST sampling — the fan (and only the fan) is duty-cycled. Between bursts the sensor
// sits in RHT/Gas-Only mode: fan off (quiet), but the VOC gas sensor keeps running so its index
// stays alive and keeps adapting. Every SEN54_BURST_MINUTES we spin the fan up for a fresh PM.
void serviceSen54() {
#if SEN54_INTERMITTENT
  uint32_t now = millis();
  if (sen54FanOn) {
    // Fan running (full Measurement). Once it's warmed up, grab one PM reading, then drop the fan
    // back down to gas-only — WITHOUT stopping the gas sensor (that's what keeps VOC alive).
    if (now - sen54FanStart >= (uint32_t)SEN54_WARMUP_SECONDS * 1000UL) {
      readSen5x(true);                   // capture the settled PM value (and VOC)
      sen5x.startMeasurementWithoutPm(); // fan OFF, VOC/RHT keep running -> quiet + index stays alive
      sen54FanOn = false;
      Serial.println("SEN54: PM sampled, fan off (gas-only continues, VOC keeps running)");
    }
  } else {
    // Gas-only mode (VOC live). Time for a fresh dust reading? Spin the fan up.
    if (now - latest.pmAge >= (uint32_t)SEN54_BURST_MINUTES * 60000UL || latest.pmAge == 0) {
      sen5x.startMeasurement();          // fan ON
      sen54FanOn = true; sen54FanStart = now;
      Serial.println("SEN54: fan burst started");
    }
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

  // Read the SEN54 every 5 s. In intermittent mode VOC runs continuously (gas-only between bursts),
  // so we always poll it for VOC — but only accept PM during a warmed-up fan burst (serviceSen54
  // owns the fan timing). In continuous mode the fan is always on, so PM is accepted too.
  static uint32_t lastPm = 0;
  if (millis() - lastPm >= 5000) {
    lastPm = millis();
#if SEN54_INTERMITTENT
    readSen5x(false);   // VOC every cycle; PM captured per-burst inside serviceSen54()
#else
    readSen5x(true);
#endif
  }
  serviceSen54();
  maybePost();
}
