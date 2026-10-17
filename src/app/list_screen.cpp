#include "list_screen.h"
#include "nowplaying.h"
#include "../config.h"
#include "../display/display.h"
#include "../display/ui.h"

using display::tft;

namespace listscreen {

static const ui::Rect R_BACK = {0, 0, 96, 40};
static const ui::Rect R_UP = {256, 44, 64, 96};
static const ui::Rect R_DOWN = {256, 144, 64, 96};
static const int16_t LIST_X = 0, LIST_W = 252, ROW_Y = 44, ROW_H = 49, ROWS = 4;
static const uint32_t IDLE_MS = 30000;
static const uint16_t GREEN = 0x1EAC;

static const Source* s_src = nullptr;
static uint16_t s_top = 0;  // first row shown
static uint32_t s_touchedAt = 0;
static uint32_t s_seenVersion = 0;
static int s_pressed = -1;  // row on screen, or 10 back / 11 up / 12 down

static ui::Rect rowRect(int i) { return {LIST_X, (int16_t)(ROW_Y + i * ROW_H), LIST_W, (int16_t)(ROW_H - 3)}; }

static void drawBack(bool pressed) {
  const uint16_t fill = pressed ? ui::TEXT : ui::PANEL, ink = pressed ? ui::BG : ui::TEXT;
  tft.fillRoundRect(R_BACK.x + 4, R_BACK.y + 4, R_BACK.w - 8, R_BACK.h - 8, 8, fill);
  tft.fillTriangle(16, 20, 26, 12, 26, 28, ink);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(ink, fill);
  tft.drawString("Back", 32, 20, 2);
}

static void drawArrow(const ui::Rect& r, bool up, bool enabled, bool pressed) {
  const uint16_t fill = pressed ? ui::TEXT : ui::PANEL;
  const uint16_t ink = pressed ? ui::BG : enabled ? ui::TEXT : ui::EDGE;
  tft.fillRoundRect(r.x + 4, r.y + 2, r.w - 8, r.h - 4, 10, fill);
  const int cx = r.cx(), cy = r.cy();
  if (up) tft.fillTriangle(cx - 14, cy + 8, cx + 14, cy + 8, cx, cy - 10, ink);
  else tft.fillTriangle(cx - 14, cy - 8, cx + 14, cy - 8, cx, cy + 10, ink);
}

static bool canUp() { return s_top > 0; }
static bool canDown() { return s_top + ROWS < s_src->count() || s_src->count() < s_src->total(); }

static void drawRow(int i, bool pressed) {
  const ui::Rect r = rowRect(i);
  Row row;
  if (!s_src->row(s_top + i, row)) {
    tft.fillRect(r.x, r.y, r.w, r.h, ui::BG);
    return;
  }
  const uint16_t fill = pressed ? ui::EDGE : ui::PANEL;
  tft.fillRoundRect(r.x + 4, r.y, r.w - 8, r.h, 8, fill);
  nowplaying::textLine(row.name, r.x + 14, r.y + 2, r.w - 28, row.dim ? ui::DIM : ui::TEXT, fill);
  nowplaying::textLine(row.sub, r.x + 14, r.y + 24, r.w - 28, row.highlight ? GREEN : ui::DIM, fill);
}

static void message(const String& line1, const String& line2) {
  tft.fillRect(0, ROW_Y, 256, SCREEN_H - ROW_Y, ui::BG);
  nowplaying::textLine(line1, 8, 112, 240, ui::TEXT, ui::BG, true);
  if (line2.length()) nowplaying::textLine(line2, 8, 136, 240, ui::DIM, ui::BG, true);
}

static void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, ROW_Y, ui::BG);
  drawBack(false);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(ui::TEXT, ui::BG);
  tft.drawString(s_src->title, 108, 20, 4);
  const uint16_t n = s_src->count();
  if (n > ROWS) {
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(ui::DIM, ui::BG);
    const uint16_t last = min<uint16_t>(s_top + ROWS, n);
    tft.drawString(String(s_top + 1) + "-" + String(last) + " of " + String(max(s_src->total(), n)), SCREEN_W - 8,
                   20, 2);
  }
}

static void drawBody() {
  String l1, l2;
  switch (s_src->status(l1, l2)) {
    case Status::Loading:
      if (s_src->count() == 0) {
        message("Loading...", "");
        break;
      }
      [[fallthrough]];
    case Status::Ready:
      for (int i = 0; i < ROWS; i++) drawRow(i, false);
      break;
    case Status::Message:
      message(l1, l2);
      break;
  }
  drawArrow(R_UP, true, canUp(), false);
  drawArrow(R_DOWN, false, canDown(), false);
}

static void redraw() {
  if (s_top >= s_src->count() && s_top > 0) s_top = s_src->count() >= ROWS ? s_src->count() - ROWS : 0;
  drawHeader();
  drawBody();
}

void open(const Source& source) {
  const bool same = s_src == &source;
  s_src = &source;
  s_touchedAt = millis();
  s_pressed = -1;
  if (!same) s_top = 0;
  tft.fillScreen(ui::BG);
  String l1, l2;
  const Status st = s_src->status(l1, l2);
  if (s_src->reloadOnOpen || (st != Status::Ready && st != Status::Loading)) {
    s_top = 0;
    s_src->load(false);
  }
  s_seenVersion = s_src->version();
  redraw();
}

void refreshIfChanged() {
  if (!s_src || s_src->version() == s_seenVersion) return;
  s_seenVersion = s_src->version();
  redraw();
}

Result touch(int16_t x, int16_t y, bool pressed, bool released) {
  s_touchedAt = millis();
  if (pressed) {
    s_pressed = -1;
    String l1, l2;
    if (R_BACK.contains(x, y)) {
      s_pressed = 10;
      drawBack(true);
    } else if (R_UP.contains(x, y) && canUp()) {
      s_pressed = 11;
      drawArrow(R_UP, true, true, true);
    } else if (R_DOWN.contains(x, y) && canDown()) {
      s_pressed = 12;
      drawArrow(R_DOWN, false, true, true);
    } else if (s_src->status(l1, l2) != Status::Message) {
      for (int i = 0; i < ROWS; i++)
        if (rowRect(i).contains(x, y) && s_top + i < s_src->count()) {
          s_pressed = i;
          drawRow(i, true);
        }
    }
    return Result::None;
  }
  if (!released || s_pressed < 0) return Result::None;
  const int p = s_pressed;
  s_pressed = -1;
  if (p == 10) {
    if (R_BACK.contains(x, y)) return Result::Back;
    drawBack(false);
  } else if (p == 11) {
    s_top = s_top >= ROWS ? s_top - ROWS : 0;
    redraw();
  } else if (p == 12) {
    if (s_top + ROWS < s_src->count()) s_top += ROWS;
    // At the end of what is loaded but not of the account: fetch the next page.
    if (s_top + ROWS >= s_src->count() && s_src->count() < s_src->total()) s_src->load(true);
    redraw();
  } else {
    const bool inside = rowRect(p).contains(x, y);
    drawRow(p, false);
    if (inside) {
      s_src->choose(s_top + p);
      return Result::Chosen;
    }
  }
  return Result::None;
}

bool timedOut() {
  if (millis() - s_touchedAt > IDLE_MS) {
    s_touchedAt = millis();
    return true;
  }
  return false;
}

}  // namespace listscreen
