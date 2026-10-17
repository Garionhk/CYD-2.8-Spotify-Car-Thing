#include "settings.h"
#include <Preferences.h>
#include "../util/log.h"

namespace settings {

static Preferences s_prefs;
static Settings s_settings;
static uint32_t s_saveAt = 0;

static const char* KEY_BRIGHT = "brightPct";  // percent (Stage 0 stored 0..255 under "bright")
static const char* KEY_ROT = "rot";
static const char* KEY_INVERT = "invert";
static const char* KEY_PANEL = "panel";
static const char* KEY_POLL = "poll";
static const char* KEY_SCREENOFF = "scrOff";
static const char* KEY_TZ = "tz";
static const char* KEY_LASTSSID = "lastSsid";
static const char* KEY_WEBPASS = "webPass";
static const char* KEY_TOUCHCAL = "touchcal";

static String ssidKey(int i) { return "ssid" + String(i); }
static String passKey(int i) { return "pass" + String(i); }

static void sanitise(Settings& s) {
  const Settings d;
  s.brightness = constrain(s.brightness, BRIGHTNESS_MIN, 100);
  if (s.rotation != 1 && s.rotation != 3) s.rotation = d.rotation;
  if (s.panel != PANEL_ST7789 && s.panel != PANEL_ILI9341) s.panel = d.panel;
  if (s.pollPreset > 1) s.pollPreset = 0;
  if (s.screenOffMin > 120) s.screenOffMin = 0;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    s.ssid[i].trim();
    if (s.ssid[i].length() > 32) s.ssid[i] = s.ssid[i].substring(0, 32);
    if (s.ssid[i].isEmpty()) s.pass[i] = "";
  }
  // Move networks up so the list has no gaps: slot order is priority order.
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (s.ssid[i].length()) continue;
    for (int j = i + 1; j < MAX_NETWORKS; j++) {
      if (s.ssid[j].isEmpty()) continue;
      s.ssid[i] = s.ssid[j];
      s.pass[i] = s.pass[j];
      s.ssid[j] = s.pass[j] = "";
      break;
    }
  }
}

void begin() {
  s_prefs.begin("cyd", false);
  const Settings d;
  Settings& s = s_settings;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    s.ssid[i] = s_prefs.getString(ssidKey(i).c_str(), "");
    s.pass[i] = s_prefs.getString(passKey(i).c_str(), "");
  }
  s.webPass = s_prefs.getString(KEY_WEBPASS, "");
  s.brightness = s_prefs.getUChar(KEY_BRIGHT, d.brightness);
  s.rotation = s_prefs.getUChar(KEY_ROT, d.rotation);
  s.invert = s_prefs.getBool(KEY_INVERT, d.invert);
  s.panel = s_prefs.getUShort(KEY_PANEL, d.panel);
  s.pollPreset = s_prefs.getUChar(KEY_POLL, d.pollPreset);
  s.screenOffMin = s_prefs.getUChar(KEY_SCREENOFF, d.screenOffMin);
  s.tz = s_prefs.getString(KEY_TZ, d.tz);
  s.lastSsid = s_prefs.getString(KEY_LASTSSID, "");
  sanitise(s);
  LOGF("settings: %d network(s), bright=%u%% rot=%u invert=%d panel=%u, screen off %s, web login %s",
       networkCount(), s.brightness, s.rotation, s.invert, s.panel,
       s.screenOffMin ? (String(s.screenOffMin) + " min").c_str() : "never", s.webPass.length() ? "set" : "open");
}

Settings& get() { return s_settings; }

int networkCount() {
  int n = 0;
  for (int i = 0; i < MAX_NETWORKS; i++)
    if (s_settings.ssid[i].length()) n++;
  return n;
}

// Write-only-on-change helpers: an NVS write that does not happen cannot be torn by a power cut.
static void putU8(const char* k, uint8_t v, uint8_t d) {
  if (s_prefs.getUChar(k, d) != v || !s_prefs.isKey(k)) s_prefs.putUChar(k, v);
}
static void putStr(const char* k, const String& v) {
  if (s_prefs.getString(k, "") != v) {
    if (v.isEmpty()) s_prefs.remove(k);
    else s_prefs.putString(k, v);
  }
}

void save() {
  s_saveAt = 0;
  Settings& s = s_settings;
  const Settings d;
  sanitise(s);
  for (int i = 0; i < MAX_NETWORKS; i++) {
    putStr(ssidKey(i).c_str(), s.ssid[i]);
    putStr(passKey(i).c_str(), s.pass[i]);
  }
  putStr(KEY_WEBPASS, s.webPass);
  putU8(KEY_BRIGHT, s.brightness, d.brightness);
  putU8(KEY_ROT, s.rotation, d.rotation);
  putU8(KEY_POLL, s.pollPreset, d.pollPreset);
  putU8(KEY_SCREENOFF, s.screenOffMin, d.screenOffMin);
  putStr(KEY_TZ, s.tz == d.tz ? String() : s.tz);  // the default is not stored
  putStr(KEY_LASTSSID, s.lastSsid);
  if (s_prefs.getBool(KEY_INVERT, d.invert) != s.invert) s_prefs.putBool(KEY_INVERT, s.invert);
  if (s_prefs.getUShort(KEY_PANEL, d.panel) != s.panel) s_prefs.putUShort(KEY_PANEL, s.panel);
}

void saveSoon() { s_saveAt = millis() + 2000; }

void tick() {
  if (s_saveAt && (int32_t)(millis() - s_saveAt) >= 0) save();
}

bool loadTouchCal(touch::Calibration& out) {
  if (s_prefs.getBytesLength(KEY_TOUCHCAL) != sizeof(out)) return false;
  s_prefs.getBytes(KEY_TOUCHCAL, &out, sizeof(out));
  return out.magic == touch::CAL_MAGIC;
}

void saveTouchCal(const touch::Calibration& cal) {
  touch::Calibration cur;
  if (loadTouchCal(cur) && memcmp(&cur, &cal, sizeof(cal)) == 0) return;
  s_prefs.putBytes(KEY_TOUCHCAL, &cal, sizeof(cal));
}

void eraseAll() {
  s_prefs.clear();
  s_settings = Settings();
  LOGF("settings: erased");
}

}  // namespace settings
