#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

// Text in any script the font covers (Chinese, Japanese kana, Latin with accents, Greek,
// Cyrillic), from the .cydf fonts that tools/gen_font.py writes to LittleFS.
//
// Nothing of the font is held in RAM: glyphs are found by binary search on flash, with a small
// cache of recent metrics. A string is rendered once into a TextStrip (2 bits per pixel, as
// the MP3 player's title strips), which is then copied to the screen as often as needed --
// the scrolling title reads no flash while it moves.

struct Glyph {
  uint32_t cp = 0;
  uint32_t offset = 0;  // into the bitmap area
  uint8_t w = 0, h = 0;
  int8_t xOff = 0, yOff = 0;  // yOff: from the top of the line
  uint8_t advance = 0;
};

class CydFont {
 public:
  bool open(const char* path);
  bool ok() const { return ok_; }
  uint8_t lineHeight() const { return lineH_; }
  bool glyph(uint32_t cp, Glyph& g);                     // false if the font lacks it
  bool bitmap(const Glyph& g, uint8_t* out, size_t cap);  // 2-bit packed, MSB first

 private:
  bool readEntry(uint32_t i, Glyph& g);
  fs::File file_;  // fs:: -- TFT_eSPI defines FS_NO_GLOBALS, which hides the plain name
  bool ok_ = false;
  uint8_t lineH_ = 0;
  uint32_t count_ = 0, bitmapBase_ = 0;
  static constexpr int CACHE = 64;  // direct-mapped by codepoint
  Glyph cache_[CACHE];
};

class TextStrip {
 public:
  ~TextStrip() { free(buf_); }
  // Lays out utf8 and renders it. maxWidth > 0 cuts it to fit with "...". False if out of memory.
  bool render(CydFont& font, const String& utf8, int16_t maxWidth = 0);
  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
  // Copies the strip into spr with its column srcX at dstX (columns outside either are skipped).
  // pal[level]: the colour for each of the 4 alpha levels (0 = background).
  void blit(TFT_eSprite& spr, int16_t srcX, int16_t dstX, const uint16_t pal[4]) const;
  // Alpha level 0..3 at (x, y); 0 outside the strip. For blending over a picture.
  uint8_t level(int16_t x, int16_t y) const {
    if (!buf_ || x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
    return (buf_[y * stride_ + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;
  }

 private:
  uint8_t* buf_ = nullptr;
  int16_t w_ = 0, h_ = 0;
  int16_t stride_ = 0;  // bytes per row
};

namespace fonts {

constexpr int16_t MAX_STRIP_W = 2048;  // longest title rendered (~11.8 KB at 23 px)

bool begin();         // opens /fonts/b16.cydf and /fonts/s12.cydf (LittleFS must be mounted)
CydFont& title();     // 16 px (the title over the cover)
CydFont& body();      // 16 px
CydFont& small();     // 12 px (artist and times over the cover)
bool available();     // both fonts opened

// The 4 colours for fg on bg.
void palette(uint16_t fg, uint16_t bg, uint16_t out[4]);

}  // namespace fonts
