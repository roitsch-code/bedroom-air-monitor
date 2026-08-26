// Seeed_GFX panel selection for the TRMNL 7.5" OG DIY kit.
//
// This is Seeed's own documented driver.h for exactly this kit (verified against
// wiki.seeedstudio.com/ogdiy_kit_works_with_arduino/ and against the library's own
// Dynamic_Setup.h / EPaper_Board_Pins_Setups.h): BOARD_SCREEN_COMBO picks the 7.5" mono panel
// setup, USE_XIAO_EPAPER_DISPLAY_BOARD_EE04 picks the pin map for this exact carrier board.
// Seeed_GFX (a TFT_eSPI fork) reads both via User_Setup_Select.h, which looks for a "driver.h" in
// the sketch folder — this file.
//
// Board in Arduino: "XIAO_ESP32S3_PLUS" (Tools → Board → ESP32 Arduino).
#pragma once

#define BOARD_SCREEN_COMBO 502 // 7.5" monochrome ePaper screen (UC8179)
#define USE_XIAO_EPAPER_DISPLAY_BOARD_EE04
