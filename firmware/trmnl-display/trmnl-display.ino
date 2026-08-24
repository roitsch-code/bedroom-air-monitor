// Bedroom Air Monitor — display node (TRMNL 7.5" OG DIY kit; Seeed XIAO ESP32-S3 PLUS + 800×480 mono e-ink)
//
// On each wake it: connects to Wi-Fi, GETs the sensor's /now JSON, draws CO₂ + temperature +
// humidity and a plain-language airing hint, then deep-sleeps for REFRESH_MINUTES. No cloud, no
// TRMNL servers — it talks straight to the sensor on the LAN.
//
// Libraries (Arduino Library Manager): "GxEPD2", "Adafruit GFX", "ArduinoJson".
// Board: XIAO ESP32-S3.
//
// ⚠️ THE ONE HARDWARE-SPECIFIC LINE: the GxEPD2 panel class + pin map below must match your exact
//    panel. The panel is 800×480 1-bit mono; the placeholder uses the common GDEY075T7 driver.
//    If your unit ships a different controller, change ONLY the GxEPD2_750... class and the pins —
//    everything else (fetch, parse, layout, sleep) is panel-independent. See the TRMNL / Seeed
//    XIAO e-ink wiki for the exact class and pin numbers for your board revision.

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include "config.h"

// --- panel: EDIT THIS BLOCK FOR YOUR EXACT HARDWARE (see the note at the top) -----------------
#define EPD_CS   3
#define EPD_DC   5
#define EPD_RST  1
#define EPD_BUSY 2
GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT>
  display(GxEPD2_750_GDEY075T7(/*CS=*/EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RST, /*BUSY=*/EPD_BUSY));
// ----------------------------------------------------------------------------------------------

struct Air { bool ok = false; int co2 = -1, rh = -1; float temp = NAN, pm25 = NAN; };

Air fetchAir() {
  Air a;
  if (WiFi.status() != WL_CONNECTED) return a;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.begin(SENSOR_URL);
  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      a.ok   = true;
      a.co2  = doc["co2"]      | -1;
      a.rh   = doc["humidity"] | -1;
      a.temp = doc["temp"]     | NAN;
      a.pm25 = doc["pm25"]     | NAN;
    }
  }
  http.end();
  return a;
}

// The airing hint — the whole point. Hangs on the measured CO₂; never invents an alarm.
const char* airingHint(int co2) {
  if (co2 < 0)            return "no reading from the sensor";
  if (co2 >= CO2_AIR_PPM) return "Open the window before bed";
  if (co2 >= CO2_SOFT_PPM) return "A quick airing wouldn't hurt";
  return "Air in here is fine for the night";
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
}

void draw(const Air& a) {
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // Big CO₂ number — the headline.
    display.setFont(&FreeSansBold24pt7b);
    display.setCursor(40, 90);
    if (a.ok && a.co2 >= 0) { display.print(a.co2); display.print(" ppm CO2"); }
    else                    display.print("-- CO2");

    // The airing hint, large, directly under it.
    display.setFont(&FreeSans12pt7b);
    display.setCursor(40, 150);
    display.print(airingHint(a.ok ? a.co2 : -1));

    // Secondary line: temperature, humidity, dust.
    display.setCursor(40, 210);
    if (a.ok) {
      if (!isnan(a.temp)) { display.print(a.temp, 1); display.print(" C   "); }
      if (a.rh >= 0)      { display.print(a.rh); display.print(" %RH   "); }
      if (!isnan(a.pm25)) { display.print("PM2.5 "); display.print(a.pm25, 0); }
    } else {
      display.print("Could not reach the sensor.");
    }
  } while (display.nextPage());
  display.hibernate();
}

void sleep() {
  esp_sleep_enable_timer_wakeup((uint64_t)REFRESH_MINUTES * 60ULL * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  connectWiFi();
  Air a = fetchAir();
  display.init(115200, true, 2, false);
  draw(a);
  sleep();          // deep sleep until the next refresh; setup() runs again on wake
}

void loop() { /* never reached — the device deep-sleeps out of setup() */ }
