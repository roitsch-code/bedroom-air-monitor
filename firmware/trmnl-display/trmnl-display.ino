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
  int hour = -1;     // Berlin hour (0-23), -1 until NTP has synced — drives the time-aware verdict
  int daySeed = 0;   // day of year: the wording rotates day to day but stays stable within a day
};

// Which part of the day it is — the verdict is framed differently in each (a morning "air is fine
// for the night" is nonsense; the night must never nag you to open a window). W_DAY is also the
// safe fallback before NTP has synced. Defined up here for the same reason as Verdict: it appears in
// a function signature (windowFor), and any such type must be declared above every function.
enum Win { W_NIGHT, W_MORNING, W_DAY, W_EVENING };

// The airing verdict. Defined here, right next to Reading, on purpose: the Arduino IDE
// auto-generates function prototypes near the top of the file, before this struct would be
// defined if it stayed down near decideAiring() — and a prototype returning an unknown type fails
// to compile ("'Verdict' does not name a type"). Any type used in a function signature has to be
// defined up here, above every function.
// action = there is something to do right now (open the window). Drawn as a bold BLACK box with
// white text — the attention state. When false (nothing to do: all good, wait, or no data) the
// card is a LIGHT-GREY box with black text — calm, not an alert. This matches Markus's mockups
// exactly: "BALD LUEFTEN" is the black box, "ALLES FEIN" is the light-grey box.
struct Verdict { const char* lead; String sub; bool action; };

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
    r.hour = t.tm_hour;      // drives the time-aware verdict
    r.daySeed = t.tm_yday;   // rotates the wording once per day
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

// ── The airing verdict — time-aware + rotating ────────────────────────────────
// The recommendation is framed by the TIME OF DAY (a morning "air is fine for the night" made no
// sense — the night is over), and the wording ROTATES day to day so it isn't the same sentence
// every morning. The rotation is seeded by r.daySeed (day of year): stable within a day, different
// tomorrow. Thresholds live in config.h (CO2 1000/1400, VOC 150/250, humidity 60 %) — those match
// the sleep research (sleep starts to suffer around 1000 ppm CO2) and the HealthSync Wall, so all
// three describe the air the same way. The smart outdoor logic (don't air when it's warmer/wetter
// outside; short Stosslueften when it's cold) is kept unchanged.

static Win windowFor(int hour) {
  if (hour < 0)  return W_DAY;        // NTP not synced yet → neutral, no night/morning framing
  if (hour < 6)  return W_NIGHT;      // 0-6  : asleep — never nag to open a window
  if (hour < 10) return W_MORNING;    // 6-10 : the night is over, clear its air
  if (hour < 20) return W_DAY;        // 10-20: ambient "right now"
  return W_EVENING;                   // 20-24: what you'll sleep in
}
// Deterministic pick from a set of phrasings — different each day, stable within a day.
static const char* pick(const char* const opts[], int n, int seed) {
  int i = seed % n; if (i < 0) i += n; return opts[i];
}

Verdict decideAiring(const Reading& r) {
  if (!r.sensorOk || r.co2 < 0) {
    return { "Keine Daten", "Sensor gerade nicht erreichbar.", false };
  }

  Win win = windowFor(r.hour);
  int seed = r.daySeed;

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

  // ── NIGHT (0-6): calm, NEVER an action box. No "get up and open the window" at 3 a.m. ──
  if (win == W_NIGHT) {
    if (co2High || vocHigh) {
      static const char* L[] = { "CO2 hoch", "Luft schwer", "Stickig" };
      static const char* S[] = { "Frueh lueften. Jetzt: schlaf.", "Morgens Fenster auf. Ruhe.", "Klaert sich beim Aufstehen." };
      return { pick(L, 3, seed), pick(S, 3, seed), false };
    }
    if (co2Soft || vocSoft || humid) {
      static const char* L[] = { "Leicht stickig", "CO2 zieht an", "Etwas erhoeht" };
      static const char* S[] = { "Nichts zu tun. Ruhe.", "Passt fuers Schlafen.", "Kein Grund aufzustehen." };
      return { pick(L, 3, seed), pick(S, 3, seed), false };
    }
    static const char* L[] = { "Ruhe", "Gute Nacht", "Alles ruhig" };
    static const char* S[] = { "Luft ist ok. Schlaf gut.", "CO2 niedrig. Gute Nacht.", "Frische Luft, schlaf weiter." };
    return { pick(L, 3, seed), pick(S, 3, seed), false };
  }

  // Every sentence is kept short on purpose: at this font scale the box fits ~3 lines.
  String burst = coldOutside ? " Kurz stosslueften." : "";

  // 1) Air-quality problem — CO2/VOC seriously elevated. Strongest trigger.
  if (co2High || vocHigh) {
    String reason = (co2High && vocHigh) ? "CO2+VOC hoch" : (co2High ? "CO2 hoch" : "VOC hoch");
    // Outside warmer → don't air now (physical, time-independent).
    if (r.outdoorOk && !outdoorCooler && r.outdoorTemp > r.indoorTemp + 1.0f) {
      return { "Noch warten", reason + " - draussen waermer (" + String(r.outdoorTemp, 0) +
               String((char)176) + "). Warten bis kuehler.", false };
    }
    const char* lead;
    String pre = "";
    if (win == W_MORNING) {
      static const char* L[] = { "Lueften", "Fenster auf", "Jetzt durchlueften" };
      static const char* P[] = { "Nacht war stickig. ", "Ueber Nacht angestaut. ", "Nachtluft raus. " };
      lead = pick(L, 3, seed); pre = pick(P, 3, seed);
    } else if (win == W_EVENING) {
      static const char* L[] = { "Fenster auf vor dem Schlafen", "Vor dem Schlafen lueften", "Fuer die Nacht lueften" };
      lead = pick(L, 3, seed);
    } else {
      static const char* L[] = { "Fenster auf", "Jetzt lueften", "Durchlueften" };
      lead = pick(L, 3, seed);
    }
    String s = pre + reason + ".";
    if (outdoorCooler) s += " Kuehlt mit.";
    if (humid) {
      if (!absHumidityKnown)  s += " Feuchte auch hoch.";
      else if (!outdoorDrier) s += " Wird nicht trockener.";
      else                    s += " Trocknet mit.";
    }
    s += burst;
    return { lead, s, true };
  }

  // 2) Humid indoors, but airing wouldn't help right now (raining/humid outside).
  if (humid && absHumidityKnown && !outdoorDrier) {
    String s = "Feuchte " + String(r.humidity) + "% - draussen genauso feucht, warten.";
    if (co2Soft || vocSoft) s += " CO2/VOC leicht erhoeht.";
    return { "Feucht, aber warten", s, false };
  }

  // 3) Soft rise — a quick airing helps.
  if (co2Soft || vocSoft || (humid && (!absHumidityKnown || outdoorDrier))) {
    const char* lead;
    if (win == W_MORNING) {
      static const char* L[] = { "Kurz durchlueften", "Einmal stosslueften", "Morgens kurz lueften" };
      lead = pick(L, 3, seed);
    } else if (win == W_EVENING) {
      static const char* L[] = { "Vor dem Bett kurz lueften", "Kurz lueften vor dem Schlafen" };
      lead = pick(L, 2, seed);
    } else {
      static const char* L[] = { "Kurz lueften", "Fenster kurz auf", "Einmal stosslueften" };
      lead = pick(L, 3, seed);
    }
    String s;
    if (humid && absHumidityKnown && outdoorDrier) s = "Feuchte " + String(r.humidity) + "% - draussen trockener, kurz lueften.";
    else if (humid)              s = "Feuchte ueber " + String(HUMIDITY_HIGH) + "% - kurz stosslueften.";
    else if (co2Soft && vocSoft) s = "CO2+VOC leicht erhoeht.";
    else if (co2Soft)            s = "CO2 leicht erhoeht.";
    else                         s = "VOC leicht erhoeht.";
    s += burst;
    return { lead, s, true };
  }

  // 4) Fine — fresh air, framed by time of day (never "for the night" in the morning).
  if (win == W_MORNING) {
    static const char* L[] = { "Frische Luft", "Gut durchgeatmet", "Morgens frisch" };
    static const char* S[] = { "Gut geschlafen - Luft war ok.", "CO2 niedrig, saubere Nacht.", "Nichts zu tun heute frueh." };
    return { pick(L, 3, seed), pick(S, 3, seed), false };
  }
  if (win == W_EVENING) {
    static const char* L[] = { "Gute Luft fuer die Nacht", "Frisch zum Einschlafen", "Bereit fuers Bett" };
    static const char* S[] = { "Frisch genug zum Schlafen.", "So kannst du einschlafen.", "Gute Basis fuer die Nacht." };
    return { pick(L, 3, seed), pick(S, 3, seed), false };
  }
  static const char* L[] = { "Luft ist frisch", "Alles gut", "Saubere Luft" };
  static const char* S[] = { "CO2 im gruenen Bereich.", "Nichts zu tun.", "Passt.", "Luft ist sauber." };
  return { pick(L, 4, seed), pick(S, 4, seed), false };
}

// ── Drawing helpers ──────────────────────────────────────────────────────────

// Fills a rectangle with a light-grey appearance on a pure black/white panel, via a sparse dither
// (one black pixel per 3×3 block ≈ 11% coverage). From normal viewing distance this reads as the
// light-grey card in Markus's "ALLES FEIN" mockup — the panel has no real grey level, so a fill
// pattern is the honest way to get one.
void fillLightGrey(int x, int y, int w, int h) {
  epaper.fillRect(x, y, w, h, TFT_WHITE);
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++)
      if ((xx % 3) == 0 && (yy % 3) == 0)
        epaper.drawPixel(xx, yy, TFT_BLACK);
}

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

  // Verdict card — bold ALL-CAPS headline + up to 3 lines of matching-scale sub-text. Two looks,
  // exactly like Markus's mockups: an ACTION verdict ("Fenster auf" / "Bald lueften") is a BLACK
  // box with white text; a nothing-to-do verdict ("Alles gut" / "warten" / "Keine Daten") is a
  // LIGHT-GREY box with black text. Box is taller (264) so a third sub-text line clears the footer
  // — footY below is computed from boxH, so it always clears automatically.
  const int boxX = MARGIN, boxY = 112, boxW = CONTENT_W, boxH = 264;
  const uint16_t inkColor = v.action ? TFT_WHITE : TFT_BLACK;   // text color inside the box
  if (v.action) epaper.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
  else          fillLightGrey(boxX, boxY, boxW, boxH);
  // Transparent text (single-arg) so the black letters sit directly on the grey dither — an opaque
  // background would paint a white cell behind every glyph and punch holes in the light-grey card.
  epaper.setTextColor(inkColor);
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
    epaper.setTextColor(inkColor);
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

void deepSleepFor(int hour) {
  // At night nobody is looking, and a full e-ink refresh FLASHES black/white — visible and annoying
  // in a dark bedroom. So between roughly midnight and 6 a.m. we sleep long (the panel just holds its
  // calm night image) instead of blinking every REFRESH_MINUTES. Outside that, the normal cadence.
  // hour < 0 (NTP not synced) → normal cadence, so a never-synced clock can't freeze the display.
  uint32_t minutes = (hour >= 0 && hour < 6) ? 60 : REFRESH_MINUTES;
  esp_sleep_enable_timer_wakeup((uint64_t)minutes * 60ULL * 1000000ULL);
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

  deepSleepFor(r.hour);   // deep sleep until the next refresh (longer at night); setup() runs again on wake
}

void loop() { /* never reached — the device deep-sleeps out of setup() */ }
