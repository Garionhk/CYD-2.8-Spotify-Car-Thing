#include "screens.h"
#include "../config.h"
#include "../display/display.h"
#include "../net/portal.h"
#include "../spotify/auth.h"
#include "../storage/settings.h"

using display::tft;

namespace screens {

const ui::Rect R_CALIBRATE = {212, 196, 100, 38};

void drawCalibrateButton(bool pressed) {
  const ui::Rect& r = R_CALIBRATE;
  const uint16_t fill = pressed ? ui::ACCENT : ui::PANEL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, pressed ? ui::TEXT : ui::EDGE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ui::TEXT, fill);
  tft.drawString("Calibrate", r.cx(), r.cy(), 2);
}

static void title(const char* text, uint16_t colour = ui::ACCENT) {
  tft.fillScreen(ui::BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(colour, ui::BG);
  tft.drawString(text, 12, 8, 4);
}

static void loginLine(int y) {
  tft.setTextDatum(TL_DATUM);
  if (portal::hasPassword()) {
    tft.setTextColor(ui::DIM, ui::BG);
    tft.drawString("Login: admin + your password", 12, y, 2);
  } else {
    tft.setTextColor(ui::WARN, ui::BG);
    tft.drawString("No page password set yet", 12, y, 2);
  }
}

// ---------------------------------------------------------------------------
// Setup (hotspot)
// ---------------------------------------------------------------------------
void drawSetup() {
  title("Setup");
  // A phone has to join the hotspot first: the QR is the Wi-Fi join code (iOS and Android
  // camera apps both read "WIFI:"), and the captive page then opens by itself.
  const String ap = portal::apName();
  ui::drawQr("WIFI:T:nopass;S:" + ap + ";;", 240, 112, 150);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::TEXT, ui::BG);
  tft.drawString("1. Scan to join Wi-Fi", 12, 48, 2);
  tft.setTextColor(ui::ACCENT, ui::BG);
  tft.drawString("   " + ap, 12, 68, 2);
  tft.setTextColor(ui::TEXT, ui::BG);
  tft.drawString("2. The page opens by", 12, 96, 2);
  tft.drawString("   itself, or go to", 12, 114, 2);
  tft.setTextColor(ui::ACCENT, ui::BG);
  tft.drawString("   " + portal::url(), 12, 132, 2);
  tft.setTextColor(ui::DIM, ui::BG);
  tft.drawString("3. Add networks, Save", 12, 160, 2);
  if (portal::problem().length()) {  // e.g. a mistyped password: say which network
    tft.setTextColor(ui::WARN, ui::BG);
    tft.drawString(portal::problem().substring(0, 26), 12, 182, 2);
  }
  drawCalibrateButton(false);
}

// ---------------------------------------------------------------------------
// Waiting for Wi-Fi
// ---------------------------------------------------------------------------
static uint8_t s_spin = 0;

static void drawStatusLine() {
  String text;
  if (portal::problem().length()) text = portal::problem();
  else switch (portal::wifiState()) {
    case portal::WifiState::Scanning: text = "Looking for saved networks..."; break;
    case portal::WifiState::Joining: text = "Joining..."; break;
    case portal::WifiState::Waiting: text = "None in range. Retrying..."; break;
    default: break;
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(portal::problem().length() ? ui::WARN : ui::DIM, ui::BG);
  tft.setTextPadding(250);
  tft.drawString(text, 12, 168, 2);
  tft.setTextPadding(0);
}

void drawWaiting() {
  title("Waiting for Wi-Fi", ui::TEXT);
  const Settings& s = settings::get();
  const String joining = portal::wifiState() == portal::WifiState::Joining ? portal::ssid() : String();
  tft.setTextDatum(TL_DATUM);
  int y = 52;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (s.ssid[i].isEmpty()) continue;
    const bool now = s.ssid[i] == joining;
    tft.setTextColor(now ? ui::ACCENT : ui::TEXT, ui::BG);
    tft.drawString(String(i + 1) + ". " + s.ssid[i] + (now ? "  <" : ""), 12, y, 4);
    y += 32;
  }
  drawStatusLine();
  tft.setTextColor(ui::DIM, ui::BG);
  tft.drawString("Hold BOOT 5 s for setup", 12, 206, 2);
  drawCalibrateButton(false);
}

void tickWaiting() {
  // Eight dots round a circle; the bright one walks.
  const int cx = 284, cy = 72, r = 16;
  for (int i = 0; i < 8; i++) {
    const float a = i * PI / 4;
    const uint16_t c = i == s_spin ? ui::ACCENT : (i == (s_spin + 7) % 8 ? ui::DIM : ui::EDGE);
    tft.fillCircle(cx + (int)(r * cosf(a)), cy + (int)(r * sinf(a)), 3, c);
  }
  s_spin = (s_spin + 1) % 8;
  drawStatusLine();
}

// ---------------------------------------------------------------------------
// Connected
// ---------------------------------------------------------------------------
static void drawSignal() {
  const int8_t rssi = portal::rssi();
  const int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi ? 1 : 0;
  const int x0 = 280, yb = 30;
  for (int i = 0; i < 4; i++) {
    const int h = 6 + i * 5;
    tft.fillRect(x0 + i * 8, yb - h, 6, h, i < bars ? ui::ACCENT : ui::EDGE);
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::DIM, ui::BG);
  tft.setTextPadding(150);
  tft.drawString(portal::ssid() + "  " + String(rssi) + " dBm", 12, 40, 2);
  tft.setTextPadding(0);
}

static void drawSpotifyLine() {
  const spotify::AuthState st = spotify::state();
  const bool ok = st == spotify::AuthState::Authorized;
  const bool bad = st == spotify::AuthState::Expired || spotify::lastError().length();
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ok && !bad ? ui::GOOD : bad ? ui::WARN : ui::DIM, ui::BG);
  tft.setTextPadding(190);
  String line = String("Spotify: ") + spotify::stateText();
  if (ok && spotify::lastApiStatus() == 200) line += ", playing";
  else if (ok && spotify::lastApiStatus() == 204) line += ", API OK";
  tft.drawString(line, 12, 160, 2);
  // The error itself is on the setup page; here, where to go.
  tft.setTextColor(ui::DIM, ui::BG);
  tft.drawString(ok && !bad ? "" : "Scan the QR to set up", 12, 178, 2);
  tft.setTextPadding(0);
}

void drawConnected() {
  title("Wi-Fi connected");
  drawSignal();
  // The QR is the IP address: phones resolve .local unreliably (Android especially).
  // Until Spotify is logged in it opens the page at the Spotify section.
  const String url = portal::url();
  const bool needLogin = spotify::state() != spotify::AuthState::Authorized;
  ui::drawQr(needLogin ? url + "/#spotify" : url, 248, 120, 128);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::TEXT, ui::BG);
  tft.drawString("Settings page:", 12, 70, 2);
  tft.setTextColor(ui::ACCENT, ui::BG);
  tft.drawString(portal::mdnsUrl(), 12, 90, 2);
  tft.drawString(url, 12, 110, 2);
  loginLine(138);
  drawSpotifyLine();
  drawCalibrateButton(false);
}

void tickConnected() { drawSignal(); }

void refreshSpotify() { drawSpotifyLine(); }

}  // namespace screens
