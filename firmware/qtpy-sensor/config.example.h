// Copy this file to config.h and fill in your values. config.h is git-ignored.
#pragma once

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID   "your-network"
#define WIFI_PASS   "your-password"

// A FIXED IP is strongly recommended so the display can always find the sensor after a reboot.
// Easiest: reserve one for this board's MAC in your router (DHCP reservation) and leave the block
// below commented out. Or uncomment and set a static IP that is free on your LAN.
// #define STATIC_IP   192, 168, 1, 50
// #define GATEWAY_IP  192, 168, 1, 1
// #define SUBNET_IP   255, 255, 255, 0

// ── Optional logging endpoint (leave POST_URL empty to disable) ──────────────
// The sensor POSTs one JSON reading here every POST_MINUTES. Not needed for the display.
#define POST_URL      ""                       // e.g. "https://example.com/api/ingest/bedroom"
#define POST_SECRET   ""                       // sent as the x-ingest-secret header, if set
#define POST_MINUTES  5

// A label sent with every reading, so a logger can tell where the sensor stood (see the parent
// project's note on why moving the sensor is a "device change"). Purely informational.
#define PLACEMENT     "dresser"

// ── SEN54 dust/VOC fan behaviour ─────────────────────────────────────────────
// The SEN54 has an audible fan. INTERMITTENT (recommended for a bedroom): keep it OFF, and only
// spin it up for a short burst every SEN54_BURST_MINUTES to sample dust — so it's silent almost
// all the time. Set to 0 to run it continuously instead (louder, but PM every loop).
#define SEN54_INTERMITTENT   1
#define SEN54_BURST_MINUTES  15    // how often to sample dust
#define SEN54_WARMUP_SECONDS 30    // fan must run this long before a PM reading is trustworthy

// ── CO₂ sensor calibration ───────────────────────────────────────────────────
// Automatic Self-Calibration assumes the room regularly sees fresh ~400 ppm air (i.e. you air it
// out). Leave ON if you ventilate regularly; turn OFF for a room that's rarely aired, and calibrate
// manually once instead (see docs). Temperature offset compensates the sensor's self-heating.
#define CO2_AUTO_CALIBRATION  1
#define CO2_TEMP_OFFSET_C     4.0f   // default; tune against a reference thermometer after a few hours
