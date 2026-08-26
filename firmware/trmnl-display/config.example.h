// Copy this file to config.h and fill in your values. config.h is git-ignored.
#pragma once

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID   "your-network"
#define WIFI_PASS   "your-password"

// ── Where to fetch the reading from ──────────────────────────────────────────
// The sensor node's /now endpoint. Use the sensor's FIXED IP.
#define SENSOR_URL  "http://192.168.1.50/now"

// ── Outdoor temperature (Open-Meteo, no key needed) ─────────────────────────
// Your rough coordinates — this is only used for the outdoor reading, nothing else.
#define WEATHER_LAT  "51.2277"
#define WEATHER_LON  "6.7735"

// ── Refresh cadence ──────────────────────────────────────────────────────────
// How long between redraws. Every wake costs a Wi-Fi connect + a full e-ink flash, so don't go
// too low — 10–15 minutes is plenty for air.
#define REFRESH_MINUTES  12

// ── Airing verdict — PLACEHOLDER thresholds ──────────────────────────────────
// Markus is designing the real decision (CO₂ + indoor/outdoor temp + humidity together) himself —
// this is only a stand-in so the display shows something sensible until that's ready. Swap the
// numbers here, or replace decideAiring() in the .ino once the real rules exist.
#define CO2_AIR_PPM      1400   // at/above this: window should go, full stop
#define CO2_SOFT_PPM     1000   // between SOFT and AIR: worth a quick airing
#define HUMIDITY_HIGH    60     // %RH at/above this: airing hint mentions it regardless of CO₂
