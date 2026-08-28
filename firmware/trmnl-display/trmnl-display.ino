// Bedroom Air Monitor — display node (TRMNL 7.5" OG DIY kit; Seeed XIAO ESP32-S3 PLUS + 800×480
// mono e-ink, UC8179 controller).
//
// On each wake it: connects to Wi-Fi, gets the sensor's /now JSON (CO₂, indoor temp, humidity),
// gets the outdoor temperature from Open-Meteo, works out an airing verdict, draws it, then
// deep-sleeps for REFRESH_MINUTES. No cloud, no TRMNL servers, no HealthSync server round-trip —
// it talks straight to the sensor and to Open-Meteo.
//
// Layout matches the approved preview: header (room name + clock) on top, the verdict in the
// middle, six factors (CO₂ / INNEN / DRAUSSEN / FEUCHTE / FEINSTAUB / VOC) evenly split across the
// bottom, one font throughout, no divider line. FEINSTAUB (PM2.5) and VOC come from the SEN54 —
// both show "--" until its intermittent fan burst has sampled at least once (see the sensor's own
// config.h, SEN54_BURST_MINUTES).
//
// Libraries: "Seeed_GFX" (Sketch → Include Library → Add .ZIP Library, from
// github.com/Seeed-Studio/Seeed_GFX), "ArduinoJson" (Library Manager).
// Board: "XIAO_ESP32S3_PLUS" (Tools → Board → ESP32 Arduino).
// driver.h in this same sketch folder selects the exact panel — see that file, don't touch it.
// Seeed_GFX's own header is still called TFT_eSPI.h (it's a fork that kept the original name) —
// that's not a typo below. EPaper is only declared when driver.h's setup actually enables it.
//
// ⚠️ NOT test-flashed by me — I don't have the hardware. If it doesn't compile, paste the exact
// error and I'll fix only that line, not guess-rewrite the file.

#include "driver.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"

#ifdef EPAPER_ENABLE
EPaper epaper;
#endif

// Europe/Berlin, DST-aware (CET winter / CEST summer).
static const char* TZ_BERLIN = "CET-1CEST,M3.5.0,M10.5.0/3";

// ── What we know after one wake cycle ────────────────────────────────────────
struct Reading {
  bool sensorOk = false;
  int co2 = -1, humidity = -1;
  float indoorTemp = NAN;
  float pm25 = NAN;   // µg/m³, from the SEN54 — NAN when the fan hasn't sampled yet (intermittent mode)
  float voc = NAN;    // Sensirion VOC index (unitless, ~100 = normal)
  bool outdoorOk = false;
  float outdoorTemp = NAN;
  float outdoorHumidity = NAN;   // %RH — needed so "air it out" isn't suggested when it's raining
  bool timeOk = false;
  char clock[6] = "--:--";
};

// The airing verdict. Defined here, right next to Reading, on purpose: the Arduino IDE
// auto-generates function prototypes near the top of the file, before this struct would be
// defined if it stayed down near decideAiring() — and a prototype returning an unknown type fails
// to compile ("'Verdict' does not name a type"). Any type used in a function signature has to be
// defined up here, above every function.
struct Verdict { const char* lead; String sub; };

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
      r.pm25        = doc["pm25"]     | NAN;   // absent while the SEN54 fan hasn't sampled yet
      r.voc         = doc["voc"]      | NAN;
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
               "&longitude=" + WEATHER_LON + "&current=temperature_2m,relative_humidity_2m";
  http.begin(client, url);
  int code = http.GET();
  if (code == 200) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      if (doc["current"]["temperature_2m"].is<float>()) {
        r.outdoorTemp = doc["current"]["temperature_2m"].as<float>();
        r.outdoorOk = true;
      }
      if (doc["current"]["relative_humidity_2m"].is<float>()) {
        r.outdoorHumidity = doc["current"]["relative_humidity_2m"].as<float>();
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

// ── Physics: absolute humidity ───────────────────────────────────────────────
// Relative humidity alone can't say whether opening a window will dry the room out — warm air
// holds far more water than cold air, so "90% outside" at 3°C can hold LESS real water than
// "60% inside" at 22°C. Converting both to absolute humidity (grams of water per m³, Magnus-Tetens
// approximation) makes the comparison physically correct: it gets a rainy mild day right (outside
// really is wetter, don't air it out) AND a cold damp winter day right (the cold air is drier in
// absolute terms, and dries out further once it warms up inside) — a %RH comparison alone cannot
// get both of those right at the same time.
float absoluteHumidity(float tempC, float rh) {
  if (isnan(tempC) || isnan(rh) || rh < 0) return NAN;
  float satVaporPressure = 6.112f * exp((17.62f * tempC) / (243.12f + tempC));  // hPa
  return 216.7f * ((rh / 100.0f) * satVaporPressure) / (273.15f + tempC);       // g/m³
}

// ── The airing verdict ────────────────────────────────────────────────────────
// PLACEHOLDER (see config.h): Markus is designing the real rules himself — this combines CO₂ + VOC
// + the indoor/outdoor temperature difference + absolute humidity into something sensible until
// then. Swap the numbers in config.h, or rewrite the body of this one function — nothing else needs
// to change when the real rules land. (Verdict itself is defined up top, next to Reading.)

Verdict decideAiring(const Reading& r) {
  if (!r.sensorOk || r.co2 < 0) {
    return { "Keine Daten", "Sensor gerade nicht erreichbar." };
  }

  bool co2High = r.co2 >= CO2_AIR_PPM;
  bool co2Soft = r.co2 >= CO2_SOFT_PPM;
  bool vocKnown = !isnan(r.voc);
  bool vocHigh = vocKnown && r.voc >= VOC_AIR;
  bool vocSoft = vocKnown && r.voc >= VOC_SOFT;
  bool humid = r.humidity >= 0 && r.humidity >= HUMIDITY_HIGH;
  bool outdoorCooler = r.outdoorOk && (r.outdoorTemp < r.indoorTemp - 1.0f);
  bool coldOutside = r.outdoorOk && r.outdoorTemp <= COLD_OUTSIDE_C;

  // The physically correct humidity check — see absoluteHumidity() above.
  float indoorAbs = absoluteHumidity(r.indoorTemp, r.humidity);
  float outdoorAbs = r.outdoorOk ? absoluteHumidity(r.outdoorTemp, r.outdoorHumidity) : NAN;
  bool absHumidityKnown = !isnan(indoorAbs) && !isnan(outdoorAbs);
  bool outdoorDrier = absHumidityKnown && (outdoorAbs < indoorAbs - HUMIDITY_MARGIN_ABS);

  // Every sentence below is kept short on purpose: at this font scale the box fits 3 lines, roughly
  // 80-90 characters total — a longer sentence used to just stop mid-word on the real display with
  // no sign it was cut. wrapText() now backstops that with "...", but the fix that actually matters
  // is staying short here.
  String burst = coldOutside ? " Kurz stosslueften." : "";

  // 1) Air quality problem — CO2 and/or VOC seriously elevated. Strongest trigger, always wins.
  if (co2High || vocHigh) {
    String reason = (co2High && vocHigh) ? "CO2+VOC hoch" : (co2High ? "CO2 hoch" : "VOC hoch");

    if (r.outdoorOk && !outdoorCooler && r.outdoorTemp > r.indoorTemp + 1.0f) {
      return { "Noch warten", reason + " - draussen waermer (" + String(r.outdoorTemp, 0) +
               String((char)176) + "). Warten bis kuehler." };
    }
    String s = reason + ".";
    if (outdoorCooler) s += " Kuehlt mit.";
    if (humid) {
      if (!absHumidityKnown) {
        s += " Feuchte auch hoch.";
      } else if (!outdoorDrier) {
        s += " Wird dabei nicht trockener.";
      } else {
        s += " Trocknet nebenbei mit.";
      }
    }
    s += burst;
    return { "Fenster auf", s };
  }

  // 2) Humid indoors, but airing wouldn't actually help right now — the case that started this:
  //    raining outside, indoor humidity high, opening the window would only import more moisture.
  if (humid && absHumidityKnown && !outdoorDrier) {
    String s = "Feuchte " + String(r.humidity) + "% - draussen genauso feucht, warten.";
    if (co2Soft || vocSoft) s += " CO2/VOC leicht erhoeht.";
    return { "Feucht, aber warten", s };
  }

  // 3) Worth a short airing: soft CO2/VOC rise, or humidity that outside air can genuinely fix (or
  //    where outdoor humidity is unknown, so it can't be ruled out).
  if (co2Soft || vocSoft || (humid && (!absHumidityKnown || outdoorDrier))) {
    String s;
    if (humid && absHumidityKnown && outdoorDrier) {
      s = "Feuchte " + String(r.humidity) + "% - draussen trockener, kurz lueften.";
    } else if (humid) {
      s = "Feuchte ueber " + String(HUMIDITY_HIGH) + "% - kurz stosslueften.";
    } else if (co2Soft && vocSoft) {
      s = "CO2+VOC leicht erhoeht - kurz lueften.";
    } else if (co2Soft) {
      s = "CO2 leicht erhoeht - kurz lueften.";
    } else {
      s = "VOC leicht erhoeht - kurz lueften.";
    }
    return { "Bald lueften", s };
  }
  return { "Alles gut", "Luft ist fuer die Nacht in Ordnung." };
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

// Very small word-wrap: greedily fills lines up to maxW, returns up to maxLines lines. If the text
// doesn't fit, the last line gets "..." appended instead of silently stopping mid-word — a verdict
// sentence that ran too long once did exactly that on the real display, with no sign it was cut.
// The real fix is keeping decideAiring()'s sentences short; this is just the backstop.
int wrapText(const String& text, int maxW, uint8_t font, String outLines[], int maxLines, uint8_t textSize = 1) {
  epaper.setTextFont(font);
  epaper.setTextSize(textSize);   // textWidth() measures at the currently set size — must match
  int lineCount = 0;              // what drawString() will actually render, or wrapping is wrong
  String line, word;
  int i = 0, n = text.length();
  bool truncated = false;
  while (i <= n) {
    char c = (i < n) ? text[i] : ' ';
    if (c == ' ' || i == n) {
      String candidate = line.length() ? (line + " " + word) : word;
      if (epaper.textWidth(candidate) > maxW && line.length()) {
        if (lineCount >= maxLines) { truncated = true; break; }
        outLines[lineCount++] = line;
        line = word;
      } else {
        line = candidate;
      }
      word = "";
      if (i == n) {
        if (line.length()) {
          if (lineCount >= maxLines) truncated = true;
          else outLines[lineCount++] = line;
        }
        break;
      }
    } else {
      word += c;
    }
    i++;
  }
  if (truncated && lineCount > 0) {
    String& last = outLines[lineCount - 1];
    while (last.length() > 0 && epaper.textWidth(last + "...") > maxW) last.remove(last.length() - 1);
    last += "...";
  }
  epaper.setTextSize(1);   // leave text size as we found it — don't leak state to the caller
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

  // Verdict: black box, white text, left-aligned inside it — matches Markus's design mockup
  // (bold ALL-CAPS headline, up to 3 lines of matching-scale sub-text below it). Box is taller
  // than the two-line version (264 vs 248) to fit a third sub-text line without crowding the
  // footer — footY below is computed from boxH, so it always clears it automatically.
  const int boxX = MARGIN, boxY = 112, boxW = CONTENT_W, boxH = 264;
  epaper.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
  epaper.setTextColor(TFT_WHITE, TFT_BLACK);
  epaper.setTextDatum(TL_DATUM);
  epaper.setTextFont(4);
  epaper.setTextSize(2);
  String leadUpper = String(v.lead);
  leadUpper.toUpperCase();
  epaper.drawString(leadUpper, boxX + 32, boxY + 24);

  // Sub-text at the same 2x scale as the headline (52px), up to 3 lines — wrapText measures at
  // that scale too (passing textSize 2 via a temporary bump), so wrapping matches what actually
  // gets drawn. Tighter line pitch (54 vs the 2-line version's 60) so 3 lines still fit boxH.
  epaper.setTextFont(4);
  String subLines[3];
  int nLines = wrapText(v.sub, boxW - 64, 4, subLines, 3, 2);
  for (int i = 0; i < nLines; i++) {
    epaper.setTextSize(2);
    epaper.drawString(subLines[i], boxX + 32, boxY + 96 + i * 54);
  }
  epaper.setTextSize(1);

  // Footer: six equal columns, each centered, one font, no line above it.
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
  const int footY = boxY + boxH + 40;      // labels
  const int valY  = footY + 26;            // values
  const int colCount = 6;
  const int colW = CONTENT_W / colCount;

  const char* labels[colCount] = { "CO2", "INNEN", "DRAUSSEN", "FEUCHTE", "FEINSTAUB", "VOC" };
  String values[colCount] = {
    r.sensorOk && r.co2 >= 0 ? String(r.co2) : "--",
    r.sensorOk && !isnan(r.indoorTemp) ? String(r.indoorTemp, 1) : "--",
    r.outdoorOk ? String(r.outdoorTemp, 0) : "--",
    r.sensorOk && r.humidity >= 0 ? String(r.humidity) : "--",
    !isnan(r.pm25) ? String(r.pm25, 0) : "--",   // "--" until the SEN54's next intermittent burst
    !isnan(r.voc) ? String(r.voc, 0) : "--",
  };
  bool hasDegree[colCount] = { false, true, true, false, false, false };
  const char* suffix[colCount] = { "", "", "", "%", "", "" };

  for (int i = 0; i < colCount; i++) {
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

#ifdef EPAPER_ENABLE
  epaper.begin();
  epaper.setRotation(0);
  draw(r, v);
#endif

  sleep();   // deep sleep until the next refresh; setup() runs again on wake
}

void loop() { /* never reached — the device deep-sleeps out of setup() */ }
