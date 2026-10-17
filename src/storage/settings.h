#pragma once
#include <Arduino.h>
#include "../config.h"
#include "../touch/touch.h"

constexpr int MAX_NETWORKS = 3;         // e.g. home Wi-Fi + phone hotspot, tried in this order
constexpr uint8_t BRIGHTNESS_MIN = 10;  // percent; below this the panel looks off

struct Settings {
  // Wi-Fi
  String ssid[MAX_NETWORKS];
  String pass[MAX_NETWORKS];
  String webPass;  // setup page login (user "admin"); empty = the page is open
  String lastSsid; // the network joined last: tried first next time (the car: the hotspot)

  // Display
  uint8_t brightness = 100;             // percent
  uint8_t rotation = DEFAULT_ROTATION;  // 1 or 3 (landscape, 180° apart)
  bool invert = DEFAULT_INVERT;
  uint16_t panel = DEFAULT_PANEL;       // PANEL_ST7789 or PANEL_ILI9341

  // Behaviour (used from Stage 3 / 6)
  uint8_t pollPreset = 0;     // 0 normal (4/10/30 s), 1 data saver (8/20/60 s)
  uint8_t screenOffMin = 0;   // idle backlight-off timeout in minutes, 0 = never
  String tz = DEFAULT_TZ;     // POSIX time zone for the idle clock
};

namespace settings {

void begin();
Settings& get();
void save();      // writes only keys whose value changed (brownout/flash-wear safe)
void saveSoon();  // the same, ~2 s after the LAST call (for sliders)
void tick();      // call from loop; runs a pending saveSoon()

int networkCount();  // saved networks with a non-empty SSID

bool loadTouchCal(touch::Calibration& out);
void saveTouchCal(const touch::Calibration& cal);

void eraseAll();

}  // namespace settings
