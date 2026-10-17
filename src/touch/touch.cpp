#include "touch.h"
#include <esp_task_wdt.h>
#include "../config.h"
#include "../display/display.h"
#include "../display/ui.h"
#include "../util/log.h"

using display::tft;

namespace touch {

// Fallback until the wizard runs: measured on the owner's board in the MP3 project's stage 0.
static Calibration s_cal = {3896, 176, 193, 3782, 400, 0, 0};
static State s_state;

// ---------------------------------------------------------------------------
// Bit-banged XPT2046
// ---------------------------------------------------------------------------
static inline void clockDelay() { delayMicroseconds(5); }

static uint16_t xpt(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_TOUCH_MOSI, (cmd >> i) & 1);
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    clockDelay();
    digitalWrite(PIN_TOUCH_CLK, LOW);
    clockDelay();
  }
  digitalWrite(PIN_TOUCH_MOSI, LOW);
  // Null bit, then D11..D0, each shifted out on a falling edge.
  uint16_t v = 0;
  for (int i = 11; i >= 0; i--) {
    digitalWrite(PIN_TOUCH_CLK, HIGH);
    clockDelay();
    digitalWrite(PIN_TOUCH_CLK, LOW);
    clockDelay();
    v |= (uint16_t)digitalRead(PIN_TOUCH_MISO) << i;
  }
  return v;
}

void begin() {
  pinMode(PIN_TOUCH_CS, OUTPUT);
  pinMode(PIN_TOUCH_CLK, OUTPUT);
  pinMode(PIN_TOUCH_MOSI, OUTPUT);
  pinMode(PIN_TOUCH_MISO, INPUT);
  pinMode(PIN_TOUCH_IRQ, INPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  digitalWrite(PIN_TOUCH_CLK, LOW);
}

void setCalibration(const Calibration& cal) { s_cal = cal; }
const Calibration& calibration() { return s_cal; }

bool readRaw(int16_t& rx, int16_t& ry, int16_t& z) {
  digitalWrite(PIN_TOUCH_CS, LOW);
  const int z1 = xpt(0xB1), z2 = xpt(0xC1);
  z = max(0, z1 + 4095 - z2);  // same convention as XPT2046_Touchscreen
  // First read after switching channel is the noisy one: discard, average two.
  xpt(0xD1);
  const int x1 = xpt(0xD1), x2 = xpt(0xD1);
  xpt(0x91);
  const int y1 = xpt(0x91), y2 = xpt(0x90);  // PD=00: power down, PENIRQ armed
  digitalWrite(PIN_TOUCH_CS, HIGH);
  rx = (x1 + x2) / 2;
  ry = (y1 + y2) / 2;
  return z >= s_cal.zThreshold;
}

static int16_t measureNoiseFloor() {
  int16_t worst = 0, rx, ry, z;
  for (int i = 0; i < 60; i++) {
    readRaw(rx, ry, z);
    worst = max(worst, z);
    delay(5);
    esp_task_wdt_reset();  // the wizard waits up to 60 s for a press
  }
  return worst;
}

static void nativePoint(int16_t rx, int16_t ry, int16_t& px, int16_t& py) {
  const long a = s_cal.swapXY ? ry : rx;
  const long b = s_cal.swapXY ? rx : ry;
  px = constrain((int)map(a, s_cal.xAt0, s_cal.xAtW, 0, PANEL_W - 1), 0, PANEL_W - 1);
  py = constrain((int)map(b, s_cal.yAt0, s_cal.yAtH, 0, PANEL_H - 1), 0, PANEL_H - 1);
}

void screenPoint(int16_t rx, int16_t ry, int16_t& sx, int16_t& sy) {
  int16_t px, py;
  nativePoint(rx, ry, px, py);
  // Rotation convention verified on the panel in the MP3 project's stage0/s01.
  switch (tft.getRotation()) {
    case 0: sx = px; sy = py; break;
    case 1: sx = py; sy = PANEL_W - 1 - px; break;
    case 2: sx = PANEL_W - 1 - px; sy = PANEL_H - 1 - py; break;
    default: sx = PANEL_H - 1 - py; sy = px; break;
  }
}

// ---------------------------------------------------------------------------
// Debounced press state
// ---------------------------------------------------------------------------
static uint32_t s_firstSeen = 0, s_lastSeen = 0;

const State& update() {
  const uint32_t now = millis();
  s_state.pressed = false;
  s_state.released = false;

  int16_t rx, ry, z;
  if (readRaw(rx, ry, z)) {
    if (!s_firstSeen) s_firstSeen = now;
    s_lastSeen = now;
    // Ignore contacts shorter than TOUCH_MIN_PRESS_MS; report where the press landed.
    if (!s_state.down && now - s_firstSeen >= TOUCH_MIN_PRESS_MS) {
      s_state.down = true;
      s_state.pressed = true;
      screenPoint(rx, ry, s_state.x, s_state.y);
    }
  } else if (s_firstSeen && now - s_lastSeen >= TOUCH_RELEASE_MS) {
    // One dropped sample mid-press must not split a press in two.
    if (s_state.down) {
      s_state.down = false;
      s_state.released = true;
    }
    s_firstSeen = 0;
  }
  return s_state;
}

// ---------------------------------------------------------------------------
// Calibration wizard (ported from the MP3 player's calibrate.cpp)
// ---------------------------------------------------------------------------
static const int TARGET_INSET = 25;
static const int SAMPLES_WANTED = 24;
static const int ACCEPT_ERR_PX = 25;
static const int MAX_ATTEMPTS = 3;
static const uint32_t PRESS_TIMEOUT_MS = 60000;
static const int W = PANEL_W, H = PANEL_H;  // the wizard runs in rotation 0

static void drawTarget(int x, int y, uint16_t colour) {
  tft.drawCircle(x, y, 12, colour);
  tft.drawCircle(x, y, 6, colour);
  tft.drawFastHLine(x - 16, y, 33, colour);
  tft.drawFastVLine(x, y - 16, 33, colour);
}

static void message(const char* line1, const char* line2, uint16_t colour) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colour, TFT_BLACK);
  tft.drawString(line1, W / 2, H / 2 - 14, 2);
  if (line2) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(line2, W / 2, H / 2 + 12, 2);
  }
}

static bool readStablePress(long& rx, long& ry) {
  const uint32_t deadline = millis() + PRESS_TIMEOUT_MS;
  while ((int32_t)(millis() - deadline) < 0) {
    int16_t x, y, z;
    if (readRaw(x, y, z)) {
      long sx = 0, sy = 0;
      int n = 0;
      while (n < SAMPLES_WANTED && readRaw(x, y, z)) {
        sx += x;
        sy += y;
        n++;
        delay(4);
      }
      if (n >= SAMPLES_WANTED) {
        rx = sx / n;
        ry = sy / n;
        return true;
      }
    }
    delay(5);
    esp_task_wdt_reset();  // the wizard waits up to 60 s for a press
  }
  return false;
}

static void waitForRelease() {
  uint32_t quietSince = millis();
  while (millis() - quietSince < 150) {
    int16_t x, y, z;
    if (readRaw(x, y, z)) quietSince = millis();
    delay(5);
    esp_task_wdt_reset();  // the wizard waits up to 60 s for a press
  }
}

static bool collectCorners(Calibration& out) {
  const int xL = TARGET_INSET, xR = W - 1 - TARGET_INSET;
  const int yT = TARGET_INSET, yB = H - 1 - TARGET_INSET;
  const int tx[4] = {xL, xR, xR, xL};  // TL, TR, BR, BL
  const int ty[4] = {yT, yT, yB, yB};
  static const char* NAMES[4] = {"Top left", "Top right", "Bottom right", "Bottom left"};
  long rx[4], ry[4];

  for (int i = 0; i < 4; i++) {
    tft.fillScreen(TFT_BLACK);
    drawTarget(tx[i], ty[i], TFT_YELLOW);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(NAMES[i], W / 2, H / 2 - 12, 2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Press and hold the target", W / 2, H / 2 + 12, 2);
    LOGF("calibrate: target %d/4 (%s) at %d,%d", i + 1, NAMES[i], tx[i], ty[i]);
    if (!readStablePress(rx[i], ry[i])) {
      LOGF("calibrate: timed out waiting for a press");
      return false;
    }
    drawTarget(tx[i], ty[i], TFT_GREEN);
    LOGF("calibrate:   raw %ld,%ld", rx[i], ry[i]);
    waitForRelease();
    delay(120);
  }

  // Which raw axis drives native x? The one that swings more across the width.
  const long rawXacrossWidth = labs(((rx[1] + rx[2]) / 2) - ((rx[0] + rx[3]) / 2));
  const long rawXdownHeight = labs(((rx[2] + rx[3]) / 2) - ((rx[0] + rx[1]) / 2));
  out.swapXY = rawXdownHeight > rawXacrossWidth;

  const long aL = out.swapXY ? (ry[0] + ry[3]) / 2 : (rx[0] + rx[3]) / 2;
  const long aR = out.swapXY ? (ry[1] + ry[2]) / 2 : (rx[1] + rx[2]) / 2;
  const long bT = out.swapXY ? (rx[0] + rx[1]) / 2 : (ry[0] + ry[1]) / 2;
  const long bB = out.swapXY ? (rx[2] + rx[3]) / 2 : (ry[2] + ry[3]) / 2;

  // Targets are inset: extrapolate to the true edges.
  const float slopeX = float(aR - aL) / float(xR - xL);
  const float slopeY = float(bB - bT) / float(yB - yT);
  out.xAt0 = lround(aL - slopeX * xL);
  out.xAtW = lround(aL + slopeX * ((W - 1) - xL));
  out.yAt0 = lround(bT - slopeY * yT);
  out.yAtH = lround(bT + slopeY * ((H - 1) - yT));

  const bool sane = abs(out.xAtW - out.xAt0) > 500 && abs(out.yAtH - out.yAt0) > 500;
  if (!sane) LOGF("calibrate: corner spread too small -- bad presses");
  return sane;
}

static bool confirmTap(int& errPx) {
  const int cx = W / 2, cy = H / 2;
  tft.fillScreen(TFT_BLACK);
  drawTarget(cx, cy, TFT_YELLOW);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Now tap the centre to confirm", W / 2, H - 30, 2);

  long rx, ry;
  if (!readStablePress(rx, ry)) return false;
  int16_t px, py;
  nativePoint(rx, ry, px, py);
  errPx = (int)lround(sqrt(double((px - cx) * (px - cx) + (py - cy) * (py - cy))));
  tft.fillCircle(px, py, 4, errPx <= ACCEPT_ERR_PX ? TFT_GREEN : TFT_RED);
  LOGF("calibrate: confirm tap at %d,%d (%d px off)", px, py, errPx);
  waitForRelease();
  return errPx <= ACCEPT_ERR_PX;
}

bool runCalibration(Calibration& out) {
  LOGF("calibrate: starting wizard");
  const Calibration previous = s_cal;
  const uint8_t prevRotation = tft.getRotation();
  display::setRotation(0);

  message("Touch calibration", "Press and hold each target", TFT_WHITE);
  delay(1200);
  const int16_t noise = measureNoiseFloor();

  bool accepted = false;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    Calibration c = previous;
    c.zThreshold = max(300, noise + 150);
    c.magic = CAL_MAGIC;
    s_cal.zThreshold = c.zThreshold;  // the corner reads use the new threshold

    if (!collectCorners(c)) {
      s_cal = previous;
      message("Calibration skipped", "Keeping the previous mapping", TFT_ORANGE);
      delay(1500);
      break;
    }
    s_cal = c;  // apply for the confirm tap, not returned yet

    int errPx = 0;
    if (confirmTap(errPx)) {
      LOGF("calibrate: accepted x[%d..%d] y[%d..%d] swap=%d z>=%d (noise %d)", c.xAt0, c.xAtW, c.yAt0, c.yAtH,
           c.swapXY, c.zThreshold, noise);
      message("Calibration done", nullptr, TFT_GREEN);
      delay(900);
      out = c;
      accepted = true;
      break;
    }
    if (attempt < MAX_ATTEMPTS) {
      char buf[24];
      snprintf(buf, sizeof(buf), "%d px off", errPx);
      message("Not quite - try again", buf, TFT_ORANGE);
      delay(1500);
    } else {
      // Approximate touch beats none; RECAL re-runs this any time.
      message("Rough calibration kept", "Use RECAL to redo it", TFT_ORANGE);
      delay(1800);
      out = c;
      accepted = true;
    }
  }

  display::setRotation(prevRotation);
  return accepted;
}

}  // namespace touch
