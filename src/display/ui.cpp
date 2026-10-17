#include "ui.h"
#include <qrcode.h>  // ESP-IDF's encoder, shipped with the esp32 core
#include "display.h"

using display::tft;

namespace ui {

void drawButton(TFT_eSPI& g, const Rect& r, const char* label, uint16_t color, bool pressed) {
  constexpr int16_t m = 3;
  g.fillRect(r.x, r.y, r.w, r.h, TFT_BLACK);
  uint16_t fill = pressed ? TFT_WHITE : color;
  uint16_t text = pressed ? color : TFT_WHITE;
  g.fillRoundRect(r.x + m, r.y + m, r.w - 2 * m, r.h - 2 * m, 6, fill);
  g.setTextDatum(MC_DATUM);
  g.setTextColor(text, fill);
  g.drawString(label, r.cx(), r.cy(), 2);
}

void drawCrosshair(TFT_eSPI& g, int16_t x, int16_t y, uint16_t color, int16_t size) {
  g.drawFastHLine(x - size, y, 2 * size + 1, color);
  g.drawFastVLine(x, y - size, 2 * size + 1, color);
  g.drawCircle(x, y, size / 2, color);
}

void message(const String& line1, const String& line2, uint16_t colour) {
  tft.fillScreen(BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colour, BG);
  const int cx = tft.width() / 2, cy = tft.height() / 2;
  tft.drawString(line1, cx, line2.length() ? cy - 14 : cy, 4);
  if (line2.length()) {
    tft.setTextColor(DIM, BG);
    tft.drawString(line2, cx, cy + 18, 2);
  }
}

// esp_qrcode_generate() hands the result to a callback with no user pointer,
// so the drawing target travels in these.
static int s_qrCx, s_qrCy, s_qrMax;
static bool s_qrDrawn;

static void drawQrCallback(esp_qrcode_handle_t qr) {
  const int n = esp_qrcode_get_size(qr);
  const int quiet = 2;  // modules of white round it
  const int scale = max(1, s_qrMax / (n + 2 * quiet));
  const int px = (n + 2 * quiet) * scale;
  const int x0 = s_qrCx - px / 2, y0 = s_qrCy - px / 2;
  tft.fillRect(x0, y0, px, px, TFT_WHITE);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(qr, x, y))
        tft.fillRect(x0 + (x + quiet) * scale, y0 + (y + quiet) * scale, scale, scale, TFT_BLACK);
  s_qrDrawn = true;
}

bool drawQr(const String& text, int cx, int cy, int maxPx) {
  s_qrCx = cx;
  s_qrCy = cy;
  s_qrMax = maxPx;
  s_qrDrawn = false;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawQrCallback;
  cfg.max_qrcode_version = 6;  // plenty for a URL or Wi-Fi string
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  return esp_qrcode_generate(&cfg, text.c_str()) == ESP_OK && s_qrDrawn;
}

}  // namespace ui
