#include "nowplaying.h"
#include "../config.h"
#include "../display/display.h"
#include "../display/textfont.h"
#include "../display/ui.h"
#include "../net/portal.h"
#include "../spotify/art.h"
#include "../spotify/auth.h"
#include "../spotify/control.h"
#include "../util/clock.h"

using display::tft;

namespace nowplaying {

// ---------------------------------------------------------------------------
// Layout (design B, revision 3: docs/mockups/B3_big_cover.png)
// ---------------------------------------------------------------------------
// The cover, 225x225: the 300 px image decoded 1:1 with every 4th pixel and row dropped.
static const int16_t CX = 6, CY = 7, CS = 225;
static const ui::Rect R_COVER = {CX, CY, CS, CS};
// Text over the cover's darkened bottom, each glyph with a 1 px shadow.
static const int16_t TX = CX + 6, TW = CS - 12;
// Rows sized to the fonts' real line heights (Noto: 20 px for the 16 px font, 15 px for 12 px).
static const int16_t TY = CY + 160, TH = 20;   // title, 16 px font
static const int16_t AY = CY + 181, AH = 15;   // artist, 12 px font
static const int16_t RY = CY + 200, RH = 15;   // times, 12 px font, with the bar between them
static const int16_t TIME_W = 38;
static const int16_t TIME_LX = TX, TIME_RX = TX + TW - TIME_W;
static const int16_t BAR_X = TX + TIME_W + 2, BAR_W = TW - 2 * TIME_W - 4, BAR_Y = RY + 7, BAR_H = 3;
static const int16_t HEART_X = CX + CS - 21, HEART_Y = CY + 22, HEART_R = 15;  // top right of the cover
static const int16_t COL_X = 236, COL_W = 78;  // the button column: margins 6 | cover | 5 | buttons | 6
static const ui::Rect R_PREV = {COL_X, 4, COL_W, 52};
static const ui::Rect R_PLAY = {COL_X, 62, COL_W, 60};
static const ui::Rect R_NEXT = {COL_X, 128, COL_W, 52};
static const ui::Rect R_MUTE = {COL_X, 186, COL_W, 52};
static const ui::Rect R_HEART = {HEART_X - 22, HEART_Y - 22, 44, 44};

static const uint16_t GLASS = 0x2124;   // ui::PANEL
static const uint16_t GREEN = 0x1EAC;   // (30,215,96)
static const uint16_t HEART_BG = 0x2945;
static const uint16_t LIKE_RED = 0xF9A6;
static const uint16_t VOLUME_BLUE = 0x3B9F;
static const uint16_t SOFT = 0xE71C;    // (225,225,225)
static const uint16_t BAR_TRACK = 0x6B6D;  // (110,110,110)

static bool s_volOpen = false;
static bool s_menuOpen = false;
static bool s_onScreen = false;  // Now Playing is what is on the panel now

static inline uint16_t swap16(uint16_t c) { return (c >> 8) | (c << 8); }

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
String printable(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length();) {
    const uint8_t c = in[i];
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < in.length(); k++) cp = (cp << 6) | (in[i + k] & 0x3F);
    i += n;
    if (cp >= 32 && cp < 127) { out += (char)cp; continue; }
    // Latin-1 and Latin Extended-A letters to their base letter; typographic punctuation to ASCII.
    static const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";  // U+00C0..
    if (cp >= 0xC0 && cp <= 0xFF) { out += kLatin1[cp - 0xC0]; continue; }
    if (cp >= 0x100 && cp <= 0x17F) {
      static const char kExtA[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
      out += kExtA[cp - 0x100];
      continue;
    }
    switch (cp) {
      case 0x2018: case 0x2019: case 0x2032: out += '\''; break;
      case 0x201C: case 0x201D: case 0x2033: out += '"'; break;
      case 0x2013: case 0x2014: case 0x2212: out += '-'; break;
      case 0x2026: out += "..."; break;
      case 0x00A0: case 0x2009: case 0x200A: out += ' '; break;
      default: out += '?';
    }
  }
  return out;
}

// Cut to fit maxW at font, with "..." when shortened.
static String fit(const String& s, int16_t maxW, uint8_t font) {
  if (tft.textWidth(s, font) <= maxW) return s;
  String t = s;
  while (t.length() > 1 && tft.textWidth(t + "...", font) > maxW) t.remove(t.length() - 1);
  t.trim();
  return t + "...";
}

static String mmss(uint32_t ms) {
  const uint32_t s = ms / 1000;
  char b[12];
  if (s >= 3600) snprintf(b, sizeof(b), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  else snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return b;
}

static TFT_eSprite s_line(&tft);
static TextStrip s_lineStrip;

void textLine(const String& text, int16_t x, int16_t y, int16_t w, uint16_t fg, uint16_t bg, bool centre) {
  if (fonts::available() && s_lineStrip.render(fonts::body(), text, w)) {
    const int16_t h = fonts::body().lineHeight();
    if (!s_line.created() || s_line.width() != w) {
      s_line.deleteSprite();
      s_line.createSprite(w, h);
    }
    if (s_line.created()) {
      uint16_t pal[4];
      fonts::palette(fg, bg, pal);
      s_line.fillSprite(bg);
      s_lineStrip.blit(s_line, 0, centre ? (w - s_lineStrip.width()) / 2 : 0, pal);
      s_line.pushSprite(x, y);
      return;
    }
  }
  tft.setTextDatum(centre ? TC_DATUM : TL_DATUM);
  tft.setTextColor(fg, bg);
  tft.setTextPadding(w);
  tft.drawString(fit(printable(text), w, 2), centre ? x + w / 2 : x, y, 2);
  tft.setTextPadding(0);
}

// ---------------------------------------------------------------------------
// Text over the cover: glyph levels with a 1 px shadow (right, down, diagonal)
// ---------------------------------------------------------------------------
static inline uint16_t overlay(uint16_t c, const TextStrip& t, int16_t x, int16_t y, uint16_t fg) {
  const uint8_t sh = max(max(t.level(x - 1, y), t.level(x, y - 1)), t.level(x - 1, y - 1));
  if (sh) c = display::tft.alphaBlend(sh * 85, 0x0000, c);
  const uint8_t lv = t.level(x, y);
  if (lv) c = display::tft.alphaBlend(lv * 85, fg, c);
  return c;
}

// ---------------------------------------------------------------------------
// Cover: down-sampled, darkened at the bottom, artist blended in, copies kept
// ---------------------------------------------------------------------------
static uint8_t s_k[CS];                     // brightness per cover row, 0..255
static uint16_t s_titleBg[TW * TH];         // byte-swapped, as pushImage sends (7.2 KB)
static uint16_t s_timeBg[2][TIME_W * RH];   // left, right (1 KB each)
static TextStrip s_artistStrip;
static bool s_artistFont = false;
static int16_t s_srcW = 0, s_srcH = 0;      // the decoded image, before down-sampling

static void initRows() {
  // Full brightness above, then down to 30 % over the bottom 100 rows (where the text is).
  for (int y = 0; y < CS; y++) {
    const float k = y < 125 ? 1.0f : max(0.3f, 1.0f - 0.7f * (y - 125) / 60.0f);
    s_k[y] = (uint8_t)(k * 255);
  }
}

static inline uint16_t darken(uint16_t c, uint8_t k) {
  if (k == 255) return c;
  const uint16_t r = ((c >> 11) & 31) * k / 255, g = ((c >> 5) & 63) * k / 255, b = (c & 31) * k / 255;
  return (r << 11) | (g << 5) | b;
}

// One block in screen coordinates inside the cover (pixels byte-swapped): darken, blend the
// artist in, keep the title / time areas, push.
static void emit(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* px) {
  for (int16_t r = 0; r < h; r++) {
    const int16_t Y = y + r, cyRow = Y - CY;
    if (cyRow < 0 || cyRow >= CS) continue;
    const uint8_t k = s_k[cyRow];
    for (int16_t c = 0; c < w; c++) {
      const int16_t X = x + c;
      uint16_t& p = px[r * w + c];
      uint16_t v = darken(swap16(p), k);
      if (s_artistFont && Y >= AY && Y < AY + AH && X >= TX && X < TX + TW)
        v = overlay(v, s_artistStrip, X - TX, Y - AY, SOFT);
      p = swap16(v);
      if (Y >= TY && Y < TY + TH && X >= TX && X < TX + TW) s_titleBg[(Y - TY) * TW + (X - TX)] = p;
      if (Y >= RY && Y < RY + RH) {
        if (X >= TIME_LX && X < TIME_LX + TIME_W) s_timeBg[0][(Y - RY) * TIME_W + (X - TIME_LX)] = p;
        if (X >= TIME_RX && X < TIME_RX + TIME_W) s_timeBg[1][(Y - RY) * TIME_W + (X - TIME_RX)] = p;
      }
    }
  }
  tft.pushImage(x, y, w, h, px);
}

// TJpgDec block (image coordinates, image at 0,0) -> the cover's 225 px: keep the first source
// row/column of each screen row/column (nearest), drop the rest. 3/4 for the 300 px image.
static bool onBlock(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  static uint16_t out[16 * 16];
  int16_t ox = -1, oy = -1, ow = 0, oh = 0;
  int16_t keepRow[16], keepCol[16];
  for (int16_t r = 0; r < h && r < 16; r++) {
    const int16_t sy = y + r, dy = (int32_t)sy * CS / s_srcH;
    if (sy == 0 || (int32_t)(sy - 1) * CS / s_srcH != dy) {
      if (oy < 0) oy = dy;
      keepRow[oh++] = r;
    }
  }
  for (int16_t c = 0; c < w && c < 16; c++) {
    const int16_t sx = x + c, dx = (int32_t)sx * CS / s_srcW;
    if (sx == 0 || (int32_t)(sx - 1) * CS / s_srcW != dx) {
      if (ox < 0) ox = dx;
      keepCol[ow++] = c;
    }
  }
  if (!ow || !oh) return true;
  for (int16_t r = 0; r < oh; r++)
    for (int16_t c = 0; c < ow; c++) out[r * ow + c] = bitmap[keepRow[r] * w + keepCol[c]];
  emit(CX + ox, CY + oy, ow, oh, out);
  return true;
}

// No cover yet: the panel colour through the same path (gradient and copies), and a note.
static void drawPlaceholder() {
  static uint16_t band[CS * 4];
  for (int16_t y = 0; y < CS; y += 4) {
    for (auto& p : band) p = swap16(ui::PANEL);
    emit(CX, CY + y, CS, min<int16_t>(4, CS - y), band);
  }
  const int cx = CX + CS / 2, cy = CY + 80;  // a quaver: head, stem, flag
  tft.fillCircle(cx - 14, cy + 26, 14, ui::EDGE);
  tft.fillRect(cx - 2, cy - 36, 6, 62, ui::EDGE);
  tft.fillTriangle(cx + 4, cy - 36, cx + 30, cy - 14, cx + 4, cy - 14, ui::EDGE);
}

// The cover if it is this track's (300 px, or the 64 px fallback scaled up the same way) --
// never the previous album's under a new title; the placeholder until it arrives.
static void drawCover(const player::Snapshot& s) {
  const String ready = art::readyUrl();
  const bool mine = ready.length() && (ready == s.artUrl || ready == s.artUrlSmall);
  const bool swap = tft.getSwapBytes();
  tft.setSwapBytes(false);
  bool ok = false;
  if (mine && art::decodedSize(320, 320, s_srcW, s_srcH) && s_srcW >= CS / 2) {
    // A small (64 px) image would need scaling up; it keeps the placeholder instead.
    ok = art::decode(s_srcW / 2, s_srcH / 2, 320, 320, onBlock);
  }
  if (!ok) drawPlaceholder();
  tft.setSwapBytes(swap);
}

// ---------------------------------------------------------------------------
// Title marquee, composed over its copy of the cover
// ---------------------------------------------------------------------------
static TextStrip s_titleStrip;
static bool s_titleFont = false;
static String s_titleText;
static int16_t s_titleW = 0, s_scroll = 0;
static uint32_t s_scrollAt = 0, s_pauseUntil = 0;
static const int16_t MARQUEE_GAP = 40;

// The title strip as seen through the window at scroll offset (wrapping once when it scrolls).
struct ScrolledTitle {
  int16_t off;
  uint8_t level(int16_t x, int16_t y) const {
    int16_t sx = x + off;
    if (s_titleW > TW && sx >= s_titleW + MARQUEE_GAP) sx -= s_titleW + MARQUEE_GAP;
    return s_titleStrip.level(sx, y);
  }
};

static void drawTitle() {
  static uint16_t row[TW];
  const ScrolledTitle t{s_scroll};
  const bool swap = tft.getSwapBytes();
  tft.setSwapBytes(false);
  for (int16_t y = 0; y < TH; y++) {
    const uint16_t* bg = s_titleBg + y * TW;
    for (int16_t x = 0; x < TW; x++) {
      if (!s_titleFont) {
        row[x] = bg[x];
        continue;
      }
      uint16_t c = swap16(bg[x]);
      const uint8_t sh = max(max(t.level(x - 1, y), t.level(x, y - 1)), t.level(x - 1, y - 1));
      if (sh) c = tft.alphaBlend(sh * 85, 0x0000, c);
      const uint8_t lv = t.level(x, y);
      if (lv) c = tft.alphaBlend(lv * 85, ui::TEXT, c);
      row[x] = swap16(c);
    }
    tft.pushImage(TX, TY + y, TW, 1, row);
  }
  tft.setSwapBytes(swap);
  if (!s_titleFont) {  // no LittleFS fonts: plain ASCII over the cover, no scrolling
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(ui::TEXT);
    tft.drawString(fit(s_titleText, TW, 2), TX, TY, 2);
  }
}

static void setTitle(const String& t) {
  s_titleFont = fonts::available() && s_titleStrip.render(fonts::title(), t);
  s_titleText = s_titleFont ? t : printable(t);
  s_titleW = s_titleFont ? s_titleStrip.width() : 0;
  s_scroll = 0;
  s_pauseUntil = millis() + 2000;  // let it be read before it moves
}

static void tickTitle() {
  if (!s_titleFont || s_titleW <= TW) return;
  const uint32_t now = millis();
  if ((int32_t)(now - s_pauseUntil) < 0 || now - s_scrollAt < 40) return;
  s_scrollAt = now;
  s_scroll += 2;
  if (s_scroll >= s_titleW + MARQUEE_GAP) {
    s_scroll = 0;
    s_pauseUntil = now + 2000;
  }
  drawTitle();
}

// ---------------------------------------------------------------------------
// Progress and times (over the cover)
// ---------------------------------------------------------------------------
static int16_t s_barPx = -1;
static String s_timeText[2];
static TextStrip s_timeStrip;

static void drawTime(int i, const String& text, bool alignRight) {
  static uint16_t buf[TIME_W * RH];
  memcpy(buf, s_timeBg[i], sizeof(buf));
  const int16_t x0 = i ? TIME_RX : TIME_LX;
  const bool swap = tft.getSwapBytes();
  tft.setSwapBytes(false);
  if (fonts::available() && s_timeStrip.render(fonts::small(), text, TIME_W)) {
    const int16_t off = alignRight ? TIME_W - s_timeStrip.width() : 0;
    for (int16_t y = 0; y < RH; y++)
      for (int16_t x = 0; x < TIME_W; x++)
        buf[y * TIME_W + x] = swap16(overlay(swap16(buf[y * TIME_W + x]), s_timeStrip, x - off, y, SOFT));
    tft.pushImage(x0, RY, TIME_W, RH, buf);
  } else {
    tft.pushImage(x0, RY, TIME_W, RH, buf);
    tft.setTextDatum(alignRight ? TR_DATUM : TL_DATUM);
    tft.setTextColor(SOFT);
    tft.drawString(text, alignRight ? x0 + TIME_W : x0, RY, 1);
  }
  tft.setSwapBytes(swap);
}

static void drawProgress(const player::Snapshot& s, bool full) {
  const uint32_t pos = s.progressNow();
  const int16_t px = s.durationMs ? (int16_t)((uint64_t)pos * BAR_W / s.durationMs) : 0;
  const uint16_t fill = s.playing ? ui::TEXT : ui::DIM;
  // A solid bar on a fixed track: it only ever repaints its own pixels, so no copy is needed.
  if (full || px < s_barPx) {
    tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, BAR_TRACK);
    if (px > 0) tft.fillRect(BAR_X, BAR_Y, px, BAR_H, fill);
  } else if (px > s_barPx) {
    tft.fillRect(BAR_X + s_barPx, BAR_Y, px - s_barPx, BAR_H, fill);
  }
  s_barPx = px;

  const String l = mmss(pos), r = mmss(s.durationMs);
  if (full || l != s_timeText[0]) {
    s_timeText[0] = l;
    drawTime(0, l, false);
  }
  if (full || r != s_timeText[1]) {
    s_timeText[1] = r;
    drawTime(1, r, true);
  }
}

// ---------------------------------------------------------------------------
// Buttons and heart
// ---------------------------------------------------------------------------
static void heart(int cx, int cy, int s, uint16_t c) {
  const int r = s / 2;
  tft.fillCircle(cx - r, cy - r / 2 - 1, r, c);
  tft.fillCircle(cx + r, cy - r / 2 - 1, r, c);
  tft.fillTriangle(cx - s, cy - r / 2 + 1, cx + s, cy - r / 2 + 1, cx, cy + s - 1, c);
}

Button buttonAt(int16_t x, int16_t y) {
  if (x >= COL_X - 5) {  // the column: zones fill the gaps between the buttons
    if (y < 59) return Button::Previous;
    if (y < 125) return Button::PlayPause;
    if (y < 183) return Button::Next;
    return Button::Mute;
  }
  if (R_HEART.contains(x, y)) return Button::Like;
  if (R_COVER.contains(x, y)) return Button::Cover;
  return Button::None;
}

static bool muted(const player::Snapshot& s) {
  const int8_t p = control::pendingVolume();
  return (p >= 0 ? p : s.volume) == 0;
}

void drawButton(const player::Snapshot& s, Button b, bool pressed) {
  switch (b) {
    case Button::Mute: {
      const uint16_t fill = pressed ? ui::TEXT : muted(s) ? LIKE_RED : GLASS;
      const uint16_t ink = pressed ? ui::BG : s.supportsVolume ? ui::TEXT : ui::EDGE;
      tft.fillRoundRect(R_MUTE.x, R_MUTE.y, R_MUTE.w, R_MUTE.h, 12, fill);
      const int cx = R_MUTE.cx() - 5, cy = R_MUTE.cy();
      tft.fillRect(cx - 12, cy - 5, 7, 10, ink);
      tft.fillTriangle(cx - 8, cy, cx + 2, cy - 12, cx + 2, cy + 12, ink);
      if (muted(s)) {  // an X: muted
        tft.drawLine(cx + 8, cy - 6, cx + 20, cy + 6, ink);
        tft.drawLine(cx + 9, cy - 6, cx + 21, cy + 6, ink);
        tft.drawLine(cx + 8, cy + 6, cx + 20, cy - 6, ink);
        tft.drawLine(cx + 9, cy + 6, cx + 21, cy - 6, ink);
      } else {  // sound waves
        tft.drawArc(cx + 3, cy, 10, 8, 240, 300, ink, fill, false);
        tft.drawArc(cx + 3, cy, 17, 15, 235, 305, ink, fill, false);
      }
      break;
    }
    case Button::Previous:
    case Button::PlayPause:
    case Button::Next: {
      const ui::Rect& r = b == Button::Previous ? R_PREV : b == Button::Next ? R_NEXT : R_PLAY;
      const uint16_t fill = pressed ? ui::TEXT : b == Button::PlayPause ? GREEN : GLASS;
      const uint16_t ink = pressed || b == Button::PlayPause ? ui::BG : ui::TEXT;
      tft.fillRoundRect(r.x, r.y, r.w, r.h, 12, fill);
      const int cx = r.cx(), cy = r.cy();
      if (b == Button::Previous) {
        tft.fillRect(cx - 12, cy - 11, 4, 22, ink);
        tft.fillTriangle(cx + 11, cy - 11, cx + 11, cy + 11, cx - 7, cy, ink);
      } else if (b == Button::Next) {
        tft.fillRect(cx + 8, cy - 11, 4, 22, ink);
        tft.fillTriangle(cx - 11, cy - 11, cx - 11, cy + 11, cx + 7, cy, ink);
      } else if (s.playing) {
        tft.fillRect(cx - 10, cy - 14, 7, 28, ink);
        tft.fillRect(cx + 3, cy - 14, 7, 28, ink);
      } else {
        tft.fillTriangle(cx - 8, cy - 15, cx - 8, cy + 15, cx + 15, cy, ink);
      }
      break;
    }
    case Button::Like: {
      if (s.isAd || s.uri.isEmpty()) return;
      const uint16_t fill = pressed ? ui::TEXT : HEART_BG;
      tft.fillCircle(HEART_X, HEART_Y, HEART_R, fill);  // its own circle: repaints only itself
      const int8_t liked = control::liked(s.uri);
      if (liked == 1) {
        heart(HEART_X, HEART_Y, 8, LIKE_RED);
      } else {
        heart(HEART_X, HEART_Y, 8, liked == 0 ? (pressed ? ui::BG : ui::TEXT) : ui::EDGE);
        heart(HEART_X, HEART_Y + 1, 4, fill);  // outline: the middle cut out
      }
      break;
    }
    default:
      break;
  }
}

static void drawControls(const player::Snapshot& s) {
  drawButton(s, Button::Previous, false);
  drawButton(s, Button::PlayPause, false);
  drawButton(s, Button::Next, false);
  drawButton(s, Button::Mute, false);
  drawButton(s, Button::Like, false);
}

// ---------------------------------------------------------------------------
// Messages: a pill across the middle (Now Playing), or the top strip (Idle)
// ---------------------------------------------------------------------------
static String s_toast;
static uint32_t s_toastUntil = 0;
static const uint32_t TOAST_MS = 4000;
static const ui::Rect R_PILL = {10, 94, 300, 34};

static String standingProblem() {
  if (!portal::wifiUp()) {
    if (portal::problem().length()) return portal::problem();
    return portal::apActive() ? "Setup hotspot is open" : "Waiting for Wi-Fi...";
  }
  if (spotify::state() == spotify::AuthState::Expired) return "Login expired - log in at cyd-spotify.local";
  return spotify::lastError();
}

static void drawPill(const String& msg, uint16_t fg) {
  tft.fillRoundRect(R_PILL.x, R_PILL.y, R_PILL.w, R_PILL.h, 17, ui::EDGE);
  textLine(msg, R_PILL.x + 14, R_PILL.y + 7, R_PILL.w - 28, fg, ui::EDGE, true);
}

void showToast(const String& msg) {
  s_toast = msg;
  s_toastUntil = millis() + TOAST_MS;
  if (s_onScreen && !s_menuOpen && !s_volOpen) drawPill(msg, ui::TEXT);
  else if (!s_onScreen) drawStatusStrip();
}

bool toastExpired() {
  if (!s_toastUntil || (int32_t)(millis() - s_toastUntil) < 0) return false;
  s_toastUntil = 0;
  return true;
}

void drawStatusStrip() {
  const bool toast = s_toastUntil != 0;
  String msg = toast ? s_toast : standingProblem();
  msg = fit(printable(msg), SCREEN_W - 50, 2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(toast ? ui::TEXT : ui::WARN, ui::BG);
  tft.setTextPadding(SCREEN_W - 46);
  tft.drawString(msg, 8, 2, 2);
  tft.setTextPadding(0);

  const int8_t rssi = portal::rssi();
  const int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    const int h = 3 + i * 3;
    tft.fillRect(SCREEN_W - 34 + i * 7, 14 - h, 5, h, i < bars ? ui::DIM : ui::EDGE);
  }
}

// ---------------------------------------------------------------------------
// The screen
// ---------------------------------------------------------------------------
void draw(const player::Snapshot& s) {
  static bool rowsReady = false;
  if (!rowsReady) {
    initRows();
    rowsReady = true;
  }
  s_onScreen = true;
  s_menuOpen = s_volOpen = false;
  tft.fillScreen(ui::BG);
  setTitle(s.isAd ? String("Advertisement") : s.track);
  s_artistFont = fonts::available() && s_artistStrip.render(fonts::small(), s.artist, TW);
  drawCover(s);  // also blends the artist in and keeps the title / time areas
  drawTitle();
  if (!s_artistFont) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(SOFT);
    tft.drawString(fit(printable(s.artist), TW, 1), TX, AY, 1);
  }
  drawProgress(s, true);
  drawControls(s);
  if (s_toastUntil) drawPill(s_toast, ui::TEXT);
  else if (standingProblem().length()) drawPill(standingProblem(), ui::WARN);
}

void update(const player::Snapshot& s) {
  if (s_menuOpen || s_volOpen) return;
  drawProgress(s, true);
  drawControls(s);
}

void tick(const player::Snapshot& s) {
  if (s_volOpen || s_menuOpen) return;  // an overlay covers the title and the progress bar
  tickTitle();
  static uint32_t last = 0;
  if (millis() - last >= 250) {
    last = millis();
    drawProgress(s, false);
  }
}

// ---------------------------------------------------------------------------
// Cover menu (double tap): Playlists / Devices / Volume / Close
// ---------------------------------------------------------------------------
static const ui::Rect R_MENU = {40, 10, 240, 220};
static const ui::Rect R_M_PLAYLISTS = {56, 22, 208, 46};
static const ui::Rect R_M_DEVICES = {56, 74, 208, 46};
static const ui::Rect R_M_VOLUME = {56, 126, 208, 46};
static const ui::Rect R_M_CLOSE = {56, 178, 208, 40};
static uint32_t s_menuTouchedAt = 0;
static const uint32_t MENU_CLOSE_MS = 6000;
static bool s_menuVolumeOk = false;

static void drawMenuItem(const ui::Rect& r, const char* label, const char* sub, bool enabled, bool pressed) {
  const uint16_t fill = pressed ? ui::TEXT : ui::EDGE;
  const uint16_t ink = pressed ? ui::PANEL : enabled ? ui::TEXT : ui::DIM;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, fill);
  tft.setTextDatum(sub ? ML_DATUM : MC_DATUM);
  tft.setTextColor(ink, fill);
  tft.drawString(label, sub ? r.x + 16 : r.cx(), r.cy(), 4);
  if (sub) {
    tft.setTextDatum(MR_DATUM);
    tft.drawString(sub, r.x + r.w - 12, r.cy(), 2);
  }
}

void openMenu(const player::Snapshot& s) {
  s_menuOpen = true;
  s_menuTouchedAt = millis();
  s_menuVolumeOk = s.supportsVolume;
  tft.fillRoundRect(R_MENU.x, R_MENU.y, R_MENU.w, R_MENU.h, 14, ui::PANEL);
  tft.drawRoundRect(R_MENU.x, R_MENU.y, R_MENU.w, R_MENU.h, 14, ui::EDGE);
  drawMenuItem(R_M_PLAYLISTS, "Playlists", nullptr, true, false);
  drawMenuItem(R_M_DEVICES, "Devices", nullptr, true, false);
  drawMenuItem(R_M_VOLUME, "Volume", s_menuVolumeOk ? nullptr : "not here", s_menuVolumeOk, false);
  drawMenuItem(R_M_CLOSE, "Close", nullptr, true, false);
}

bool menuOpen() { return s_menuOpen; }

MenuAction menuTouch(const player::Snapshot& s, int16_t x, int16_t y, bool pressed, bool released) {
  s_menuTouchedAt = millis();
  static const ui::Rect* s_down = nullptr;
  if (pressed) {
    s_down = R_M_PLAYLISTS.contains(x, y) ? &R_M_PLAYLISTS
             : R_M_DEVICES.contains(x, y) ? &R_M_DEVICES
             : R_M_VOLUME.contains(x, y)  ? &R_M_VOLUME
             : R_M_CLOSE.contains(x, y)   ? &R_M_CLOSE
                                          : nullptr;
    if (s_down == &R_M_PLAYLISTS) drawMenuItem(R_M_PLAYLISTS, "Playlists", nullptr, true, true);
    if (s_down == &R_M_DEVICES) drawMenuItem(R_M_DEVICES, "Devices", nullptr, true, true);
    if (s_down == &R_M_VOLUME && s_menuVolumeOk) drawMenuItem(R_M_VOLUME, "Volume", nullptr, true, true);
    if (s_down == &R_M_CLOSE) drawMenuItem(R_M_CLOSE, "Close", nullptr, true, true);
    if (!s_down && !R_MENU.contains(x, y)) {  // a tap outside closes it
      s_menuOpen = false;
      return MenuAction::Close;
    }
    return MenuAction::None;
  }
  if (!released || !s_down) return MenuAction::None;
  const ui::Rect* r = s_down;
  s_down = nullptr;
  if (!r->contains(x, y)) {
    openMenu(s);  // slid off: just undo the press
    return MenuAction::None;
  }
  s_menuOpen = false;
  if (r == &R_M_PLAYLISTS) return MenuAction::Playlists;
  if (r == &R_M_DEVICES) return MenuAction::Devices;
  if (r == &R_M_VOLUME) {
    if (s_menuVolumeOk) return MenuAction::Volume;
    showToast("Spotify can't set volume here - use the phone or car knob");
    return MenuAction::Close;
  }
  return MenuAction::Close;
}

bool menuTimedOut() {
  if (s_menuOpen && millis() - s_menuTouchedAt > MENU_CLOSE_MS) {
    s_menuOpen = false;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Volume overlay: big - and + (steps of 10), closes 3 s after the last touch
// ---------------------------------------------------------------------------
static const ui::Rect R_VOL = {24, 34, 272, 152};
static const ui::Rect R_MINUS = {40, 112, 104, 60};
static const ui::Rect R_PLUS = {176, 112, 104, 60};
static uint32_t s_volTouchedAt = 0;
static const uint32_t VOLUME_CLOSE_MS = 3000;

static void drawVolumeValue(int8_t v) {
  const ui::Rect bar = {(int16_t)(R_VOL.x + 16), (int16_t)(R_VOL.y + 52), (int16_t)(R_VOL.w - 32), 14};
  tft.fillRoundRect(bar.x, bar.y, bar.w, bar.h, 7, ui::EDGE);
  if (v > 0) tft.fillRoundRect(bar.x, bar.y, max<int16_t>(bar.w * v / 100, 14), bar.h, 7, VOLUME_BLUE);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(ui::TEXT, ui::PANEL);
  tft.setTextPadding(70);
  tft.drawString(v >= 0 ? String(v) + "%" : String("--"), R_VOL.x + R_VOL.w - 16, R_VOL.y + 14, 4);
  tft.setTextPadding(0);
}

static void drawVolumeButton(const ui::Rect& r, bool plus, bool pressed) {
  const uint16_t fill = pressed ? ui::TEXT : ui::EDGE, ink = pressed ? ui::PANEL : ui::TEXT;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, fill);
  tft.fillRect(r.cx() - 16, r.cy() - 3, 32, 6, ink);
  if (plus) tft.fillRect(r.cx() - 3, r.cy() - 16, 6, 32, ink);
}

static int8_t shownVolume(const player::Snapshot& s) {
  const int8_t p = control::pendingVolume();
  return p >= 0 ? p : s.volume;
}

void openVolume(const player::Snapshot& s) {
  s_volOpen = true;
  s_volTouchedAt = millis();
  tft.fillRoundRect(R_VOL.x, R_VOL.y, R_VOL.w, R_VOL.h, 14, ui::PANEL);
  tft.drawRoundRect(R_VOL.x, R_VOL.y, R_VOL.w, R_VOL.h, 14, ui::EDGE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::DIM, ui::PANEL);
  tft.drawString("Volume", R_VOL.x + 16, R_VOL.y + 18, 2);
  drawVolumeValue(shownVolume(s));
  drawVolumeButton(R_MINUS, false, false);
  drawVolumeButton(R_PLUS, true, false);
}

bool volumeOpen() { return s_volOpen; }

void refreshVolume(const player::Snapshot& s) {
  if (s_volOpen) drawVolumeValue(shownVolume(s));
}

bool volumeTouch(const player::Snapshot& s, int16_t x, int16_t y, bool pressed, bool released) {
  s_volTouchedAt = millis();
  const bool minus = R_MINUS.contains(x, y), plus = R_PLUS.contains(x, y);
  if (pressed) {
    if (minus || plus) {
      drawVolumeButton(minus ? R_MINUS : R_PLUS, plus, true);
      control::send(control::Cmd::VolumeStep, plus ? 10 : -10);
      drawVolumeValue(shownVolume(s));
    } else if (!R_VOL.contains(x, y)) {
      s_volOpen = false;  // a tap outside closes it
      return true;
    }
  }
  if (released) {
    drawVolumeButton(R_MINUS, false, false);
    drawVolumeButton(R_PLUS, true, false);
  }
  return false;
}

bool volumeTimedOut() {
  if (s_volOpen && millis() - s_volTouchedAt > VOLUME_CLOSE_MS) {
    s_volOpen = false;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Idle
// ---------------------------------------------------------------------------
// The idle clock, 90 % of the screen width (owner's request): the 7-segment font (digits 32 px,
// colon 12 px wide) at 2x is 280 px for "HH:MM"; 2 px between the five characters makes 288.
// 96 px tall. Redrawn when the minute changes.
static int s_clockMinute = -2;
static const int16_t CLOCK_Y = 22, CLOCK_H = 96, CLOCK_GAP = 2;

static void drawClock() {
  s_clockMinute = clockutil::minuteOfDay();
  tft.fillRect(0, CLOCK_Y, SCREEN_W, CLOCK_H + 24, ui::BG);
  if (s_clockMinute < 0) return;  // not synced yet (NTP needs Wi-Fi)
  const String t = clockutil::hhmm();
  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::TEXT, ui::BG);
  int16_t w = 0;
  for (size_t i = 0; i < t.length(); i++) w += tft.textWidth(String(t[i]), 7) + (i ? CLOCK_GAP : 0);
  int16_t x = (SCREEN_W - w) / 2;
  for (size_t i = 0; i < t.length(); i++) {
    const String c(t[i]);
    tft.drawString(c, x, CLOCK_Y, 7);
    x += tft.textWidth(c, 7) + CLOCK_GAP;
  }
  tft.setTextSize(1);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(ui::DIM, ui::BG);
  tft.drawString(clockutil::dateLine(), SCREEN_W / 2, CLOCK_Y + CLOCK_H + 4, 2);
}

void drawIdle(const player::Snapshot& s) {
  s_onScreen = false;
  s_menuOpen = s_volOpen = false;
  tft.fillScreen(ui::BG);
  drawStatusStrip();
  drawClock();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ui::TEXT, ui::BG);
  // The message block centred between the date (ends y 138) and the settings line (starts
  // y 218): equal space above and below (owner's request).
  const int16_t top = CLOCK_Y + CLOCK_H + 4 + 16, bottom = 218;
  const bool device = s.known && s.device.length();
  const int16_t blockH = 26 + (s.known ? 6 + 16 : 0) + (device ? 4 + 17 : 0);
  int16_t y = top + (bottom - top - blockH) / 2;
  tft.setTextDatum(TC_DATUM);
  tft.drawString(s.known ? "Nothing playing" : "Connecting to Spotify...", SCREEN_W / 2, y, 4);
  y += 26 + 6;
  tft.setTextColor(ui::DIM, ui::BG);
  if (s.known) {
    tft.drawString(s.active ? "Spotify is open but has no track loaded."
                            : "Open Spotify on your phone and play something.",
                   SCREEN_W / 2, y, 2);
    y += 16 + 4;
    if (device) textLine("on " + s.device, 8, y, SCREEN_W - 16, ui::DIM, ui::BG, true);
  }
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(ui::DIM, ui::BG);
  // Both addresses: some Android phones cannot open .local names.
  String where = portal::mdnsUrl();
  where.replace("http://", "");
  String ip = portal::url();
  ip.replace("http://", "");
  tft.drawString("Settings: " + where + (ip.length() ? "  |  " + ip : String()), SCREEN_W / 2, bottom, 2);
}

void tickIdle() {
  if (clockutil::minuteOfDay() != s_clockMinute) drawClock();
}

}  // namespace nowplaying
