// Bedroom Air Monitor — display node (TRMNL 7.5" OG DIY kit; Seeed XIAO ESP32-S3 PLUS + 800×480
// mono e-ink, UC8179 controller).
//
// On each wake it: connects to Wi-Fi, gets the sensor's /now JSON (CO₂, indoor temp, humidity),
// gets the outdoor temperature from Open-Meteo, works out an airing verdict, draws it, then
// deep-sleeps for REFRESH_MINUTES. No cloud, no TRMNL servers, no HealthSync server round-trip —
// it talks straight to the sensor and to Open-Meteo.
//
// Layout matches the approved preview exactly: header (room name + clock) on top, the verdict in
// the middle, four factors (CO₂ / INNEN / DRAUSSEN / FEUCHTE) evenly split across the bottom, one
// font throughout, no divider line.
//
// Libraries (Arduino Library Manager): "Seeed_GFX", "ArduinoJson".
// Board: "XIAO_ESP32S3_PLUS" (Tools → Board → Seeed XIAO Boards).
// driver.h in this same sketch folder selects the exact panel — see that file, don't touch it.
//
// ⚠️ NOT test-flashed by me — I don't have the hardware. If it doesn't compile, paste the exact
// error and I'll fix only that line, not guess-rewrite the file.

#include "driver.h"
#include <Seeed_GFX.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"

EPaper epaper = EPaper();

// Europe/Berlin, DST-aware (CET winter / CEST summer).
static const char* TZ_BERLIN = "CET-1CEST,M3.5.0,M10.5.0/3";

// ── What we know after one wake cycle ────────────────────────────────────────
struct Reading {
  bool sensorOk = false;
  int co2 = -1, humidity = -1;
  float indoorTemp = NAN;
  bool outdoorOk = false;
  float outdoorTemp = NAN;
  bool timeOk = false;
  char clock[6] = "--:--";
};

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Wi-Fi connecting");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected." : "Wi-Fi failed — drawing with what we have.");
}

// ── Sensor: GET /now from the bedroom sensor node ────────────────────────────
void fetchSensor(Reading& r) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.begin(SENSOR_URL);
  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      r.sensorOk    = true;
      r.co2         = doc["co2"]      | -1;
      r.humidity    = doc["humidity"] | -1;
      r.indoorTemp  = doc["temp"]     | NAN;
    }
  } else {
    Serial.print("Sensor GET failed, code "); Serial.println(code);
  }
  http.end();
}

// ── Outdoor temperature: Open-Meteo, keyless ─────────────────────────────────
void fetchOutdoor(Reading& r) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();   // no cert pinning — Open-Meteo needs no key, this is a read-only GET
  HTTPClient http;
  http.setConnectTimeout(8000);
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + WEATHER_LAT +
               "&longitude=" + WEATHER_LON + "&current=temperature_2m";
  http.begin(client, url);
  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      if (doc["current"]["temperature_2m"].is<float>()) {
        r.outdoorTemp = doc["current"]["temperature_2m"].as<float>();
        r.outdoorOk = true;
      }
    }
  } else {
    Serial.print("Open-Meteo GET failed, code "); Serial.println(code);
  }
  http.end();
}

// ── Clock: NTP, Europe/Berlin. If it hasn't synced yet, we show "--:--" rather
//    than invent a time. ────────────────────────────────────────────────────
void fetchTime(Reading& r) {
  if (WiFi.status() != WL_CONNECTED) return;
  configTzTime(TZ_BERLIN, "pool.ntp.org", "time.google.com");
  struct tm t;
  if (getLocalTime(&t, 6000)) {
    snprintf(r.clock, sizeof(r.clock), "%02d:%02d", t.tm_hour, t.tm_min);
    r.timeOk = true;
  }
}

// ── The airing verdict ────────────────────────────────────────────────────────
// PLACEHOLDER (see config.h): Markus is designing the real rules himself — this only combines
// CO₂ + the indoor/outdoor temperature difference + humidity into something sensible until then.
// Swap the numbers in config.h, or rewrite the body of this one function — nothing else needs to
// change when the real rules land.
struct Verdict { const char* lead; String sub; };

Verdict decideAiring(const Reading& r) {
  if (!r.sensorOk || r.co2 < 0) {
    return { "Keine Daten", "Sensor gerade nicht erreichbar." };
  }

  bool outdoorCooler = r.outdoorOk && (r.outdoorTemp < r.indoorTemp - 1.0f);
  bool humid = r.humidity >= 0 && r.humidity >= HUMIDITY_HIGH;

  if (r.co2 >= CO2_AIR_PPM) {
    if (r.outdoorOk && !outdoorCooler && r.outdoorTemp > r.indoorTemp) {
      String s = "CO2 hoch, aber draussen waermer (" + String(r.outdoorTemp, 0) +
                 String((char)176) + ") als drinnen. Noch nicht lueften.";
      return { "Noch warten", s };
    }
    String s = String("CO2 hoch") + (outdoorCooler ? " - und draussen ist es kuehler. Das lueftet und kuehlt zugleich." : ".");
    if (humid) s += " Feuchte ebenfalls hoch.";
    return { "Fenster auf", s };
  }
  if (r.co2 >= CO2_SOFT_PPM || humid) {
    String s = humid ? "Luftfeuchtigkeit ueber " + String(HUMIDITY_HIGH) + "% - kurz stosslueften."
                      : "CO2 leicht erhoeht - eine kurze Lueftung wuerde nicht schaden.";
    return { "Bald lueften", s };
  }
  return { "Alles gut", "Luft im Zimmer ist fuer die Nacht in Ordnung." };
}

// ── Drawing helpers ──────────────────────────────────────────────────────────

// Draws `numPart` centered on centerX, and — if hasDegree — a small hollow circle right after it,
// so the ° never depends on a font actually containing that glyph.
void drawCenteredWithDegree(const String& numPart, bool hasDegree, int centerX, int y, uint8_t font) {
  epaper.setTextFont(font);
  int w = epaper.textWidth(numPart);
  int degR = font >= 4 ? 5 : 3;
  int degGap = 6;
  int totalW = w + (hasDegree ? (degGap + degR * 2) : 0);
  int x = centerX - totalW / 2;
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString(numPart, x, y);
  if (hasDegree) {
    int fontH = epaper.fontHeight(font);
    int cx = x + w + degGap + degR;
    int cy = y + degR + 1;                 // sits near the top of the digits, like a real degree sign
    (void)fontH;
    epaper.drawCircle(cx, cy, degR, TFT_BLACK);
  }
}

// Very small word-wrap: greedily fills lines up to maxW, returns up to maxLines lines.
int wrapText(const String& text, int maxW, uint8_t font, String outLines[], int maxLines) {
  epaper.setTextFont(font);
  int lineCount = 0;
  String line, word;
  int i = 0, n = text.length();
  while (i <= n && lineCount < maxLines) {
    char c = (i < n) ? text[i] : ' ';
    if (c == ' ' || i == n) {
      String candidate = line.length() ? (line + " " + word) : word;
      if (epaper.textWidth(candidate) > maxW && line.length()) {
        outLines[lineCount++] = line;
        line = word;
      } else {
        line = candidate;
      }
      word = "";
      if (i == n) { if (lineCount < maxLines && line.length()) outLines[lineCount++] = line; break; }
    } else {
      word += c;
    }
    i++;
  }
  return lineCount;
}

void draw(const Reading& r, const Verdict& v) {
  const int W = 800, H = 480;
  const int MARGIN = 44;
  const int CONTENT_W = W - 2 * MARGIN;

  epaper.fillScreen(TFT_WHITE);

  // Header: room name left, clock right. One line, no divider.
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
  epaper.setTextFont(4);
  epaper.setTextDatum(TL_DATUM);
  epaper.drawString("SCHLAFZIMMER", MARGIN, 40);
  epaper.setTextDatum(TR_DATUM);
  epaper.drawString(r.timeOk ? String(r.clock) : "--:--", W - MARGIN, 40);

  // Verdict: black box, white text, left-aligned inside it — same as the approved preview.
  const int boxX = MARGIN, boxY = 112, boxW = CONTENT_W, boxH = 208;
  epaper.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
  epaper.setTextColor(TFT_WHITE, TFT_BLACK);
  epaper.setTextDatum(TL_DATUM);
  epaper.setTextFont(4);
  epaper.setTextSize(2);
  epaper.drawString(v.lead, boxX + 32, boxY + 30);
  epaper.setTextSize(1);

  epaper.setTextFont(2);
  String subLines[2];
  int nLines = wrapText(v.sub, boxW - 64, 2, subLines, 2);
  for (int i = 0; i < nLines; i++) {
    epaper.drawString(subLines[i], boxX + 32, boxY + 128 + i * 26);
  }

  // Footer: four equal columns, each centered, one font, no line above it.
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
  const int footY = boxY + boxH + 40;      // labels
  const int valY  = footY + 26;            // values
  const int colW = CONTENT_W / 4;

  const char* labels[4] = { "CO2", "INNEN", "DRAUSSEN", "FEUCHTE" };
  String values[4] = {
    r.sensorOk && r.co2 >= 0 ? String(r.co2) : "--",
    r.sensorOk && !isnan(r.indoorTemp) ? String(r.indoorTemp, 1) : "--",
    r.outdoorOk ? String(r.outdoorTemp, 0) : "--",
    r.sensorOk && r.humidity >= 0 ? String(r.humidity) : "--",
  };
  bool hasDegree[4] = { false, true, true, false };
  const char* suffix[4] = { "", "", "", "%" };

  for (int i = 0; i < 4; i++) {
    int centerX = MARGIN + colW * i + colW / 2;

    epaper.setTextFont(2);
    epaper.setTextDatum(TC_DATUM);
    epaper.drawString(labels[i], centerX, footY);

    String valueText = values[i] + suffix[i];
    drawCenteredWithDegree(valueText, hasDegree[i], centerX, valY, 4);
  }

  epaper.update();
}

void sleep() {
  esp_sleep_enable_timer_wakeup((uint64_t)REFRESH_MINUTES * 60ULL * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  connectWiFi();

  Reading r;
  fetchSensor(r);
  fetchOutdoor(r);
  fetchTime(r);

  Verdict v = decideAiring(r);

  epaper.begin();
  epaper.setRotation(0);
  draw(r, v);

  sleep();   // deep sleep until the next refresh; setup() runs again on wake
}

void loop() { /* never reached — the device deep-sleeps out of setup() */ }
