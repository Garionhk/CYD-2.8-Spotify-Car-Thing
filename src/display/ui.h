#pragma once
#include <TFT_eSPI.h>

namespace ui {

// One fixed dark palette (as the CYD Stream Deck).
constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t PANEL = 0x2124;   // dark grey
constexpr uint16_t EDGE = 0x4208;    // mid grey
constexpr uint16_t TEXT = TFT_WHITE;
constexpr uint16_t DIM = 0x9CF3;     // light grey
constexpr uint16_t ACCENT = 0x1DB9;  // Spotify-ish green
constexpr uint16_t GOOD = 0x07E8;
constexpr uint16_t WARN = 0xFD20;
constexpr uint16_t BAD = 0xF8A2;

struct Rect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  int16_t cx() const { return x + w / 2; }
  int16_t cy() const { return y + h / 2; }
};

void drawButton(TFT_eSPI& g, const Rect& r, const char* label, uint16_t color, bool pressed);
void drawCrosshair(TFT_eSPI& g, int16_t x, int16_t y, uint16_t color, int16_t size = 8);

// Clear the screen and centre one or two lines on it.
void message(const String& line1, const String& line2 = String(), uint16_t colour = TEXT);

// A QR code for text, black on white with its quiet zone, as large as fits in a maxPx square
// centred on (cx, cy). ESP-IDF's encoder, shipped with the core. False if it would not encode.
bool drawQr(const String& text, int cx, int cy, int maxPx);

}  // namespace ui
