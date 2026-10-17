// CYD Spotify Remote — Stage 0 hardware test (env: hwtest).
//
// Checks: display driver/colour inversion (test pattern), touch accuracy (targets),
// backlight PWM, RGB LED, BOOT button, LittleFS, and heap figures. See docs/STAGE0.md.

#include <Arduino.h>
#include "../config.h"
#include "../display/display.h"
#include "../display/ui.h"
#include "../hw/board.h"
#include "../storage/fs.h"
#include "../storage/settings.h"
#include "../touch/touch.h"
#include "../util/heap.h"
#include "../util/log.h"
#include "../util/timer.h"

using display::tft;

namespace {

struct Button {
  ui::Rect r;
  const char* label;
  uint16_t color;
};

enum ButtonId { BTN_INV, BTN_FLIP, BTN_BRIGHT, BTN_PANEL, BTN_CAL, BTN_COUNT };
const Button kButtons[BTN_COUNT] = {
    {{0, 192, 64, 48}, "INVERT", TFT_DARKCYAN},
    {{64, 192, 64, 48}, "FLIP", TFT_DARKGREEN},
    {{128, 192, 64, 48}, "BRIGHT", TFT_ORANGE},
    {{192, 192, 64, 48}, "PANEL", TFT_PURPLE},
    {{256, 192, 64, 48}, "RECAL", TFT_MAROON},
};

const ui::Rect kTargetArea = {0, 72, SCREEN_W, 88};
const int16_t kTargets[][2] = {{30, 92}, {160, 92}, {290, 92}, {95, 140}, {225, 140}};
constexpr int kTargetCount = sizeof(kTargets) / sizeof(kTargets[0]);
constexpr int16_t kGoodErrPx = 10;

const uint8_t kBrightLevels[] = {100, 70, 40, 15};  // percent

TFT_eSprite sStatus(&tft);
TFT_eSprite sHeap(&tft);
int sPressedBtn = -1;
uint16_t sTaps = 0, sGood = 0;
int16_t sMaxErr = 0;
uint8_t sLedIdx = 0;
Every sHeapOverlay(1000);
Every sHeapSerial(10000);

void setStatus(const char* fmt, ...) {
  char buf[80];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  sStatus.fillSprite(TFT_BLACK);
  sStatus.setTextDatum(ML_DATUM);
  sStatus.setTextColor(TFT_WHITE, TFT_BLACK);
  sStatus.drawString(buf, 2, 8, 2);
  sStatus.pushSprite(0, 160);
}

void drawHeapOverlay() {
  HeapInfo h = heapInfo();
  char buf[80];
  snprintf(buf, sizeof(buf), "heap %u  max %u  min %u  up %lus", h.free, h.maxAlloc, h.minFree,
           (unsigned long)(millis() / 1000));
  sHeap.fillSprite(TFT_BLACK);
  sHeap.setTextDatum(ML_DATUM);
  sHeap.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sHeap.drawString(buf, 2, 8, 2);
  sHeap.pushSprite(0, 176);
}

void drawColourBars() {
  struct Bar {
    const char* name;
    uint16_t color;
    uint16_t text;
  };
  const Bar bars[] = {{"RED", TFT_RED, TFT_WHITE},    {"GREEN", TFT_GREEN, TFT_BLACK},
                      {"BLUE", TFT_BLUE, TFT_WHITE},  {"WHITE", TFT_WHITE, TFT_BLACK},
                      {"BLACK", TFT_BLACK, TFT_WHITE}, {"YELLOW", TFT_YELLOW, TFT_BLACK}};
  const int16_t w = SCREEN_W / 6;
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 6; i++) {
    int16_t x = i * w;
    tft.fillRect(x, 0, w, 36, bars[i].color);
    tft.setTextColor(bars[i].text, bars[i].color);
    tft.drawString(bars[i].name, x + w / 2, 18, 1);
  }
  tft.drawRect(4 * w, 0, w, 36, TFT_DARKGREY);  // outline the black bar

  // Gradients: smooth ramps mean 16-bit colour is working; banding order R,G,B,grey.
  for (int16_t x = 0; x < SCREEN_W; x++) {
    uint8_t v = x * 255 / (SCREEN_W - 1);
    tft.drawFastVLine(x, 38, 8, tft.color565(v, 0, 0));
    tft.drawFastVLine(x, 46, 8, tft.color565(0, v, 0));
    tft.drawFastVLine(x, 54, 8, tft.color565(0, 0, v));
    tft.drawFastVLine(x, 62, 8, tft.color565(v, v, v));
  }
}

void drawTargets() {
  tft.fillRect(kTargetArea.x, kTargetArea.y, kTargetArea.w, kTargetArea.h, TFT_BLACK);
  for (int i = 0; i < kTargetCount; i++) ui::drawCrosshair(tft, kTargets[i][0], kTargets[i][1], TFT_WHITE);
}

void drawButtons() {
  for (int i = 0; i < BTN_COUNT; i++) ui::drawButton(tft, kButtons[i].r, kButtons[i].label, kButtons[i].color, false);
}

void drawAll() {
  tft.fillScreen(TFT_BLACK);
  drawColourBars();
  drawTargets();
  drawButtons();
  drawHeapOverlay();
}

void showConfigStatus() {
  setStatus("%s inv:%d rot:%u bl:%u  tap the targets", display::panelName(display::panel()), display::inverted(),
            display::rotation(), display::brightness());
}

void resetTapStats() {
  sTaps = sGood = 0;
  sMaxErr = 0;
}

void handleTargetTap(int16_t x, int16_t y) {
  int16_t best = INT16_MAX;
  for (int i = 0; i < kTargetCount; i++) {
    int16_t dx = x - kTargets[i][0], dy = y - kTargets[i][1];
    int16_t d = (int16_t)sqrtf(dx * dx + dy * dy);
    if (d < best) best = d;
  }
  bool good = best <= kGoodErrPx;
  sTaps++;
  if (good) sGood++;
  if (best > sMaxErr) sMaxErr = best;
  tft.fillCircle(x, y, 2, good ? TFT_GREEN : TFT_RED);
  setStatus("tap %d,%d err %dpx | %u/%u ok, max %d", x, y, best, sGood, sTaps, sMaxErr);
  LOGF("touch: tap (%d,%d) err=%dpx %s", x, y, best, good ? "OK" : "OFF");
}

void calibrate() {
  touch::Calibration cal;
  if (touch::runCalibration(cal)) settings::saveTouchCal(cal);  // else: previous mapping kept
  resetTapStats();
}

void onButton(int id) {
  Settings& s = settings::get();
  switch (id) {
    case BTN_INV:
      display::setInvert(!display::inverted());
      s.invert = display::inverted();
      settings::save();
      showConfigStatus();
      break;
    case BTN_FLIP:
      s.rotation = display::rotation() == 1 ? 3 : 1;
      display::setRotation(s.rotation);
      settings::save();
      resetTapStats();
      drawAll();
      showConfigStatus();
      break;
    case BTN_BRIGHT: {
      size_t n = sizeof(kBrightLevels);
      size_t next = 0;
      for (size_t i = 0; i < n; i++)
        if (kBrightLevels[i] == display::brightness()) next = (i + 1) % n;
      display::setBrightness(kBrightLevels[next]);
      s.brightness = kBrightLevels[next];
      settings::save();
      showConfigStatus();
      break;
    }
    case BTN_PANEL:
      s.panel = display::panel() == PANEL_ST7789 ? PANEL_ILI9341 : PANEL_ST7789;
      settings::save();
      setStatus("Switching to %s, restarting...", display::panelName(s.panel));
      LOGF("ui: PANEL -> %s, restarting", display::panelName(s.panel));
      delay(600);
      ESP.restart();
      break;
    case BTN_CAL:
      calibrate();
      drawAll();
      showConfigStatus();
      break;
  }
  LOGF("ui: %s -> inv=%d rot=%u bl=%u", kButtons[id].label, display::inverted(), display::rotation(),
       display::brightness());
}

int buttonAt(int16_t x, int16_t y) {
  for (int i = 0; i < BTN_COUNT; i++)
    if (kButtons[i].r.contains(x, y)) return i;
  return -1;
}

void cycleLed() {
  static const bool kColours[][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
  static const char* kNames[] = {"off", "red", "green", "blue", "white"};
  sLedIdx = (sLedIdx + 1) % 5;
  led::set(kColours[sLedIdx][0], kColours[sLedIdx][1], kColours[sLedIdx][2]);
  setStatus("BOOT short press: LED %s", kNames[sLedIdx]);
  LOGF("led: %s", kNames[sLedIdx]);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  LOGF("CYD Spotify Remote %s — Stage 0 hardware test", FW_VERSION);
  LOGF("chip: %s rev%d, %u MHz, flash %u bytes, PSRAM %u bytes", ESP.getChipModel(), ESP.getChipRevision(),
       ESP.getCpuFreqMHz(), ESP.getFlashChipSize(), ESP.getPsramSize());
  logHeap("boot");

  led::begin();
  bootbtn::begin();
  settings::begin();

  touch::begin();
  touch::Calibration cal;
  bool haveCal = settings::loadTouchCal(cal);
  if (haveCal) touch::setCalibration(cal);
  const touch::Calibration& c = touch::calibration();
  LOGF("touch: %s x[%d..%d] y[%d..%d] swap=%d z>=%d (bit-bang)",
       haveCal ? "calibration loaded" : "no stored calibration, defaults", c.xAt0, c.xAtW, c.yAt0, c.yAtH, c.swapXY,
       c.zThreshold);

  // Before the panel comes up: a finger held 8s from power-up switches controller (blind recovery).
  display::bootGesture();

  Settings& s = settings::get();
  display::begin(s.panel, s.rotation, s.invert, s.brightness);
  logHeap("display");

  storage::fsSelfTest();
  logHeap("fs");

  // LED self-test: R, G, B
  led::set(1, 0, 0);
  delay(150);
  led::set(0, 1, 0);
  delay(150);
  led::set(0, 0, 1);
  delay(150);
  led::set(0, 0, 0);

  sStatus.createSprite(SCREEN_W, 16);
  sHeap.createSprite(SCREEN_W, 16);
  logHeap("sprites");

  if (!haveCal) calibrate();
  drawAll();
  showConfigStatus();
  logHeap("idle");
}

void loop() {
  const touch::State& t = touch::update();
  if (t.pressed) {
    sPressedBtn = buttonAt(t.x, t.y);
    if (sPressedBtn >= 0) {
      const Button& b = kButtons[sPressedBtn];
      ui::drawButton(tft, b.r, b.label, b.color, true);
    } else if (kTargetArea.contains(t.x, t.y)) {
      handleTargetTap(t.x, t.y);
    }
  }
  if (t.released && sPressedBtn >= 0) {
    int id = sPressedBtn;
    sPressedBtn = -1;
    const Button& b = kButtons[id];
    ui::drawButton(tft, b.r, b.label, b.color, false);
    if (b.r.contains(t.x, t.y)) onButton(id);
  }

  switch (bootbtn::update()) {
    case bootbtn::Event::Short:
      cycleLed();
      drawTargets();  // also clears tap dots
      resetTapStats();
      break;
    case bootbtn::Event::Long:
      // Stage 0: wipe settings + calibration. (Later stages: enter setup/AP mode.)
      setStatus("BOOT held 5s: erasing settings, rebooting...");
      settings::eraseAll();
      delay(800);
      ESP.restart();
      break;
    default:
      break;
  }

  uint32_t now = millis();
  if (sHeapOverlay.due(now)) drawHeapOverlay();
  if (sHeapSerial.due(now)) logHeap("idle");
  delay(2);
}
