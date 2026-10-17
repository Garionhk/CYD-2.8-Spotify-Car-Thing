#pragma once
#include <Arduino.h>

// ---- Firmware ----
#define FW_VERSION "1.0.0"

// ---- Screen ----
constexpr int16_t SCREEN_W = 320;  // landscape (rotation 1 or 3)
constexpr int16_t SCREEN_H = 240;
constexpr int16_t PANEL_W = 240;   // native portrait (rotation 0)
constexpr int16_t PANEL_H = 320;

// ---- Panel controller (as the MP3 player project) ----
// Boards sold as ESP32-2432S028R carry an ST7789 or an ILI9341 behind identical glass.
// TFT_eSPI is built for ST7789; display.cpp sends the ILI9341 init table by hand when
// the stored panel setting says so. These are boot defaults only; NVS settings win.
constexpr uint16_t PANEL_ILI9341 = 9341;
constexpr uint16_t PANEL_ST7789 = 7789;
constexpr uint16_t DEFAULT_PANEL = PANEL_ST7789;
// CONFIRMED in Stage 0 on the owner's board (ST7789): invert ON showed every colour as its
// complement; OFF gives correct bars and smooth gradients. Boot default only; NVS wins.
constexpr bool DEFAULT_INVERT = false;
constexpr uint8_t DEFAULT_ROTATION = 1;
#define DEFAULT_TZ "PST8PDT,M3.2.0,M11.1.0"  // Vancouver: the owner's Mac; the setup page changes it
#if !defined(ST7789_DRIVER)
#error "TFT_eSPI must be built with ST7789_DRIVER (see platformio.ini)"
#endif

// ---- Pins (ESP32-2432S028R). TFT SPI pins live in platformio.ini for TFT_eSPI. ----
constexpr uint8_t PIN_TFT_BL = 21;

constexpr uint8_t PIN_TOUCH_CLK = 25;
constexpr uint8_t PIN_TOUCH_MOSI = 32;
constexpr uint8_t PIN_TOUCH_MISO = 39;
constexpr uint8_t PIN_TOUCH_CS = 33;
constexpr uint8_t PIN_TOUCH_IRQ = 36;  // touch is bit-banged on these pins (see touch.cpp)

constexpr uint8_t PIN_LED_R = 4;   // RGB LED, active LOW
constexpr uint8_t PIN_LED_G = 16;
constexpr uint8_t PIN_LED_B = 17;

constexpr uint8_t PIN_BOOT_BTN = 0;  // usable as a button after boot

// ---- Backlight PWM ----
constexpr uint8_t BL_LEDC_CHANNEL = 7;
constexpr uint32_t BL_PWM_FREQ = 5000;

// ---- Timings ----
constexpr uint32_t TOUCH_MIN_PRESS_MS = 40;  // ignore touches shorter than this
constexpr uint32_t TOUCH_RELEASE_MS = 60;    // resistive panels drop out briefly while held
constexpr uint32_t BOOT_LONG_PRESS_MS = 5000;

