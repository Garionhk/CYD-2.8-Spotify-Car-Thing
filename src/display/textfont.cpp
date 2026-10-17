#include "textfont.h"
#include <vector>
#include "display.h"
#include "../util/log.h"

static const uint32_t INDEX_BASE = 16;
static const uint32_t ENTRY = 12;

// ---------------------------------------------------------------------------
// CydFont
// ---------------------------------------------------------------------------
bool CydFont::open(const char* path) {
  ok_ = false;
  file_ = LittleFS.open(path, "r");
  if (!file_) return false;
  uint8_t h[16];
  if (file_.read(h, sizeof(h)) != sizeof(h) || memcmp(h, "CYDF", 4) != 0 || h[4] != 1 || h[5] != 2) {
    file_.close();
    return false;
  }
  lineH_ = h[7];
  count_ = h[12] | (h[13] << 8) | (h[14] << 16) | ((uint32_t)h[15] << 24);
  bitmapBase_ = INDEX_BASE + count_ * ENTRY;
  for (auto& c : cache_) c.cp = UINT32_MAX;
  ok_ = count_ > 0;
  return ok_;
}

bool CydFont::readEntry(uint32_t i, Glyph& g) {
  uint8_t e[ENTRY];
  if (!file_.seek(INDEX_BASE + i * ENTRY) || file_.read(e, ENTRY) != ENTRY) return false;
  g.cp = e[0] | (e[1] << 8) | (e[2] << 16);
  g.offset = e[3] | (e[4] << 8) | (e[5] << 16);
  g.w = e[6];
  g.h = e[7];
  g.xOff = (int8_t)e[8];
  g.yOff = (int8_t)e[9];
  g.advance = e[10];
  return true;
}

bool CydFont::glyph(uint32_t cp, Glyph& g) {
  if (!ok_) return false;
  Glyph& slot = cache_[cp % CACHE];
  if (slot.cp == cp) {
    g = slot;
    return true;
  }
  int32_t lo = 0, hi = (int32_t)count_ - 1;
  while (lo <= hi) {
    const int32_t mid = (lo + hi) / 2;
    Glyph e;
    if (!readEntry(mid, e)) return false;
    if (e.cp == cp) {
      slot = e;
      g = e;
      return true;
    }
    if (e.cp < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return false;
}

bool CydFont::bitmap(const Glyph& g, uint8_t* out, size_t cap) {
  const size_t n = ((size_t)g.w * g.h * 2 + 7) / 8;
  if (n > cap) return false;
  if (n == 0) return true;
  return file_.seek(bitmapBase_ + g.offset) && file_.read(out, n) == n;
}

// ---------------------------------------------------------------------------
// TextStrip
// ---------------------------------------------------------------------------
static void decodeUtf8(const String& s, std::vector<uint32_t>& out) {
  out.clear();
  out.reserve(s.length());
  for (size_t i = 0; i < s.length();) {
    const uint8_t c = s[i];
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < s.length(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += n;
    if (cp >= 0xFE00 && cp <= 0xFE0F) continue;  // variation selectors draw nothing
    out.push_back(cp);
  }
}

bool TextStrip::render(CydFont& font, const String& utf8, int16_t maxWidth) {
  free(buf_);
  buf_ = nullptr;
  w_ = h_ = stride_ = 0;
  if (!font.ok()) return false;

  // Layout: one glyph lookup per character; missing characters draw as '?'.
  std::vector<uint32_t> cps;
  decodeUtf8(utf8, cps);
  std::vector<Glyph> glyphs;
  glyphs.reserve(cps.size() + 3);
  Glyph q, dot;
  const bool haveQ = font.glyph('?', q);
  const bool haveDot = font.glyph('.', dot);
  int32_t width = 0;
  for (uint32_t cp : cps) {
    Glyph g;
    if (!font.glyph(cp, g)) {
      if (!haveQ) continue;
      g = q;
    }
    glyphs.push_back(g);
    width += g.advance;
  }
  // Cut to fit with "..." (and always to the strip limit).
  const int16_t limit = maxWidth > 0 ? maxWidth : fonts::MAX_STRIP_W;
  if (width > limit) {
    const int16_t ell = haveDot ? 3 * dot.advance : 0;
    while (!glyphs.empty() && width + ell > limit) {
      width -= glyphs.back().advance;
      glyphs.pop_back();
    }
    while (!glyphs.empty() && glyphs.back().cp == ' ') {
      width -= glyphs.back().advance;
      glyphs.pop_back();
    }
    for (int i = 0; haveDot && i < 3; i++) {
      glyphs.push_back(dot);
      width += dot.advance;
    }
  }

  w_ = max<int32_t>(width, 1);
  h_ = font.lineHeight();
  stride_ = (w_ + 3) / 4;
  buf_ = (uint8_t*)calloc((size_t)stride_ * h_, 1);
  if (!buf_) {
    w_ = h_ = 0;
    return false;
  }

  uint8_t bits[512];  // a 22 px glyph needs ~130 B; bitmap() refuses anything larger
  int16_t x = 0;
  for (const Glyph& g : glyphs) {
    if (g.w && g.h && font.bitmap(g, bits, sizeof(bits))) {
      for (int py = 0; py < g.h; py++) {
        const int16_t ty = g.yOff + py;
        if (ty < 0 || ty >= h_) continue;
        for (int px = 0; px < g.w; px++) {
          const int i = py * g.w + px;
          const uint8_t level = (bits[i >> 2] >> (6 - 2 * (i & 3))) & 3;
          const int16_t tx = x + g.xOff + px;
          if (!level || tx < 0 || tx >= w_) continue;
          uint8_t& b = buf_[ty * stride_ + (tx >> 2)];
          const int shift = 6 - 2 * (tx & 3);
          const uint8_t cur = (b >> shift) & 3;
          if (level > cur) b = (b & ~(3 << shift)) | (level << shift);  // overlaps keep the darker ink
        }
      }
    }
    x += g.advance;
  }
  return true;
}

void TextStrip::blit(TFT_eSprite& spr, int16_t srcX, int16_t dstX, const uint16_t pal[4]) const {
  if (!buf_) return;
  const int16_t sw = spr.width(), sh = spr.height();
  for (int16_t dx = max<int16_t>(dstX, 0); dx < sw; dx++) {
    const int16_t sx = srcX + (dx - dstX);
    if (sx < 0) continue;
    if (sx >= w_) break;
    const uint8_t* col = buf_ + (sx >> 2);
    const int shift = 6 - 2 * (sx & 3);
    for (int16_t y = 0; y < h_ && y < sh; y++) {
      const uint8_t level = (col[y * stride_] >> shift) & 3;
      if (level) spr.drawPixel(dx, y, pal[level]);
    }
  }
}

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------
namespace fonts {

static CydFont s_body, s_small;

bool begin() {
  const bool b = s_body.open("/fonts/b16.cydf");
  const bool s = s_small.open("/fonts/s12.cydf");
  if (b && s) LOGF("fonts: %u px + %u px lines from LittleFS", s_body.lineHeight(), s_small.lineHeight());
  else LOGF("fonts: not in LittleFS (pio run -e cyd -t uploadfs) -- titles fall back to ASCII");
  return b && s;
}

CydFont& title() { return s_body; }
CydFont& body() { return s_body; }
CydFont& small() { return s_small; }
bool available() { return s_body.ok() && s_small.ok(); }

void palette(uint16_t fg, uint16_t bg, uint16_t out[4]) {
  out[0] = bg;
  out[1] = display::tft.alphaBlend(85, fg, bg);
  out[2] = display::tft.alphaBlend(170, fg, bg);
  out[3] = fg;
}

}  // namespace fonts
