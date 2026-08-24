// Copy this file to config.h and fill in your values. config.h is git-ignored.
#pragma once

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID   "your-network"
#define WIFI_PASS   "your-password"

// ── Where to fetch the reading from ──────────────────────────────────────────
// The sensor node's /now endpoint. Use the sensor's FIXED IP from step 1 in the README.
#define SENSOR_URL  "http://192.168.1.50/now"

// ── Refresh cadence ──────────────────────────────────────────────────────────
// How long to deep-sleep between redraws. 10–15 minutes is plenty for air — and kind to the
// battery, since every wake costs a Wi-Fi connect plus a full e-ink refresh (which flashes).
#define REFRESH_MINUTES  12

// ── The airing threshold ─────────────────────────────────────────────────────
// At or above this CO₂ (ppm), the display says "open the window"; below it, "air is fine".
// Research puts a measurable sleep effect around 1000 ppm and a clear one around 1400.
#define CO2_AIR_PPM     1400
#define CO2_SOFT_PPM    1000   // between SOFT and AIR: "a quick airing wouldn't hurt"
