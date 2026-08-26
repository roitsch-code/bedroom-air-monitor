// Seeed_GFX panel selection for the TRMNL 7.5" OG DIY kit.
//
// This board is a Seeed XIAO ESP32-S3 PLUS driving an 800×480 mono e-ink panel (UC8179
// controller). Seeed_GFX (a TFT_eSPI fork) needs to know which exact board+panel combo this is —
// that's these two defines. Nothing else in here.
//
// Board in Arduino: "XIAO_ESP32S3_PLUS" (Tools → Board → Seeed XIAO Boards).
#pragma once

#define BOARD_SCREEN_COMBO 502
#define USE_XIAO_EPAPER_DISPLAY_BOARD_EE04
