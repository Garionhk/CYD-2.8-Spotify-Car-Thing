// CYD Spotify Remote — app: Wi-Fi, Spotify login, now playing (design B), controls, album art, playlists, devices.
//
// Boot: touch calibration on first boot, then Wi-Fi (src/net/portal). With no network saved
// the setup hotspot opens; otherwise the saved networks are tried in order, retrying in the
// background for as long as it takes (the phone hotspot in the car comes up late).
// BOOT held 5 s opens the setup hotspot at any time.

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "app/nowplaying.h"
#include "app/list_screen.h"
#include "app/lists.h"
#include "app/screens.h"
#include "config.h"
#include "display/display.h"
#include "display/textfont.h"
#include "hw/board.h"
#include "net/portal.h"
#include "spotify/art.h"
#include "spotify/auth.h"
#include "spotify/control.h"
#include "spotify/player.h"
#include "storage/fs.h"
#include "storage/settings.h"
#include "touch/touch.h"
#include "util/clock.h"
#include "util/heap.h"
#include "util/log.h"
#include "util/timer.h"

using display::tft;

namespace {

enum class Screen { None, Setup, Waiting, Connected, NowPlaying, Idle, List };
Screen sScreen = Screen::None;
uint32_t sSeenChanges = UINT32_MAX;
uint32_t sSeenSpotify = UINT32_MAX;
bool sNeedLogin = true;  // which QR the connected screen shows
// Cached player state: copied when its version changes, and once a second so the progress
// bar follows each poll (small drifts do not bump the version).
player::Snapshot sSnap;
uint32_t sSnapVersion = UINT32_MAX;
uint32_t sDrawnTrack = UINT32_MAX;
Every sSnapRefresh(1000);
nowplaying::Button sPressedBtn = nowplaying::Button::None;
uint32_t sSeenMessage = 0, sSeenLike = 0, sSeenArt = 0;
const listscreen::Source* sList = nullptr;
// Screen off (plan: idle timeout): backlight off while nothing plays; the first touch only wakes.
bool sScreenOff = false, sSwallowTouch = false;
uint32_t sLastActivity = 0;  // a picker opened from the cover menu (playlists, devices)
uint32_t sLastCoverTap = 0;    // double tap on the picture opens the menu
const uint32_t DOUBLE_TAP_MS = 500;
Every sStrip(3000);
bool sCalPressed = false;
Every sSpinner(150);
Every sSignal(3000);
Every sHeapLog(60000);

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "WATCHDOG (interrupt)";
    case ESP_RST_TASK_WDT: return "WATCHDOG (task)";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT (power dipped)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "other";
  }
}

void calibrate() {
  touch::Calibration cal;
  if (touch::runCalibration(cal)) settings::saveTouchCal(cal);  // else: previous mapping kept
}

Screen wantedScreen() {
  if (portal::apActive()) return Screen::Setup;
  // Fast boot: the last track from flash while Wi-Fi and Spotify come back (the car's hotspot
  // can take a minute), with a "Waiting for Wi-Fi" pill over it.
  const bool lastTrack = sSnap.cached && spotify::state() == spotify::AuthState::Authorized;
  if (!portal::wifiUp()) return lastTrack ? Screen::NowPlaying : Screen::Waiting;
  // Not logged in (or the login expired): the screen with the QR to the Spotify section.
  if (spotify::state() != spotify::AuthState::Authorized) return Screen::Connected;
  if (sList) return Screen::List;
  return sSnap.hasItem || sSnap.isAd ? Screen::NowPlaying : Screen::Idle;
}

void drawScreen(Screen s) {
  if (s != sScreen) sLastActivity = millis();
  sScreen = s;
  switch (s) {
    case Screen::Setup: screens::drawSetup(); break;
    case Screen::Waiting: screens::drawWaiting(); break;
    case Screen::Connected: screens::drawConnected(); break;
    case Screen::NowPlaying:
      nowplaying::draw(sSnap);
      sDrawnTrack = sSnap.trackVersion;
      break;
    case Screen::Idle: nowplaying::drawIdle(sSnap); break;
    case Screen::List: listscreen::open(*sList); break;
    default: break;
  }
}

bool hasCalibrateButton() {
  return sScreen == Screen::Setup || sScreen == Screen::Waiting || sScreen == Screen::Connected;
}

void act(nowplaying::Button b) {
  using nowplaying::Button;
  if (!portal::wifiUp()) {  // the fast-boot screen: nothing to send a command to yet
    nowplaying::showToast("Not connected yet");
    return;
  }
  switch (b) {
    case Button::Previous: control::send(control::Cmd::Previous); break;
    case Button::PlayPause: control::send(control::Cmd::PlayPause); break;
    case Button::Next: control::send(control::Cmd::Next); break;
    case Button::Like:
      if (control::liked(sSnap.uri) < 0) nowplaying::showToast("Can't like this right now");
      else control::send(control::Cmd::ToggleLike);
      break;
    case Button::Mute:
      if (sSnap.supportsVolume) control::send(control::Cmd::Mute);
      // Spotify refuses remote volume for phones (supports_volume: false), e.g. a Galaxy S20 FE.
      else nowplaying::showToast("Spotify can't mute this device - use the phone or car knob");
      break;
    case Button::Cover: break;  // the menu opens on a double tap (handleNowPlayingTouch)
    default: break;
  }
}

// Now playing: the controls row and the volume overlay. Never waits on the network:
// control::send() only queues.
void handleNowPlayingTouch(const touch::State& t) {
  using nowplaying::Button;
  if (nowplaying::menuOpen()) {
    if (!t.pressed && !t.released) return;
    switch (nowplaying::menuTouch(sSnap, t.x, t.y, t.pressed, t.released)) {
      case nowplaying::MenuAction::Close: drawScreen(Screen::NowPlaying); break;
      case nowplaying::MenuAction::Playlists:
        sList = &lists::PLAYLISTS;
        drawScreen(Screen::List);
        break;
      case nowplaying::MenuAction::Devices:
        sList = &lists::DEVICES;
        drawScreen(Screen::List);
        break;
      case nowplaying::MenuAction::Volume:
        drawScreen(Screen::NowPlaying);  // the picture back under the overlay
        nowplaying::openVolume(sSnap);
        break;
      default: break;
    }
    return;
  }
  if (nowplaying::volumeOpen()) {
    if ((t.pressed || t.released) && nowplaying::volumeTouch(sSnap, t.x, t.y, t.pressed, t.released))
      drawScreen(Screen::NowPlaying);  // closed: show what was under it
    return;
  }
  if (t.pressed) {
    sPressedBtn = nowplaying::buttonAt(t.x, t.y);
    if (sPressedBtn != Button::Cover) nowplaying::drawButton(sSnap, sPressedBtn, true);
  }
  if (t.released && sPressedBtn != Button::None) {
    const Button b = sPressedBtn;
    sPressedBtn = Button::None;
    nowplaying::drawButton(sSnap, b, false);
    if (nowplaying::buttonAt(t.x, t.y) != b) return;
    if (b == Button::Cover) {
      // Double tap: a single tap on the picture does nothing, so the menu never opens by accident.
      const uint32_t now = millis();
      if (sLastCoverTap && now - sLastCoverTap < DOUBLE_TAP_MS) {
        sLastCoverTap = 0;
        nowplaying::openMenu(sSnap);
      } else {
        sLastCoverTap = now;
      }
      return;
    }
    act(b);
  }
}

void wake() {
  sLastActivity = millis();
  if (!sScreenOff) return;
  sScreenOff = false;
  display::setBrightness(settings::get().brightness);
  LOGF("screen: on");
}

void handleTouch() {
  const touch::State& t = touch::update();
  if (t.pressed) {
    const bool wasOff = sScreenOff;
    wake();
    if (portal::wifiUp() && !sSnap.playing) player::nudge();  // music started on the phone? look now
    if (wasOff) sSwallowTouch = true;  // that touch only wakes the screen
  }
  if (sSwallowTouch) {
    if (t.released) sSwallowTouch = false;
    return;
  }
  if (sScreen == Screen::NowPlaying) {
    handleNowPlayingTouch(t);
    return;
  }
  if (sScreen == Screen::List) {
    if (!t.pressed && !t.released) return;
    if (listscreen::touch(t.x, t.y, t.pressed, t.released) != listscreen::Result::None) {
      sList = nullptr;  // back, or chosen: Now Playing shows the result
      drawScreen(wantedScreen());
    }
    return;
  }
  if (t.pressed && hasCalibrateButton() && screens::R_CALIBRATE.contains(t.x, t.y)) {
    sCalPressed = true;
    screens::drawCalibrateButton(true);
  }
  if (t.released && sCalPressed) {
    sCalPressed = false;
    if (screens::R_CALIBRATE.contains(t.x, t.y)) {
      calibrate();
      drawScreen(wantedScreen());
    } else {
      screens::drawCalibrateButton(false);
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  LOGF("CYD Spotify Remote %s", FW_VERSION);
  logHeap("boot");

  led::begin();
  bootbtn::begin();
  settings::begin();

  touch::begin();
  touch::Calibration cal;
  const bool haveCal = settings::loadTouchCal(cal);
  if (haveCal) touch::setCalibration(cal);

  // Before the panel comes up: a finger held 8 s from power-up switches controller.
  display::bootGesture();

  const Settings& s = settings::get();
  clockutil::setZone(s.tz);
  display::begin(s.panel, s.rotation, s.invert, s.brightness);
  LOGF("boot: reset reason %s", resetReason());
  storage::mount();
  LittleFS.remove("/stage0.txt");  // the Stage 0 test's boot counter (no-op once gone)
  fonts::begin();
  art::begin();
  player::loadLast();
  if (!haveCal) calibrate();
  logHeap("display");

  spotify::begin();  // reads the login: the fast-boot screen needs to know there is one
  sSnap = player::snapshot();
  sSnapVersion = player::version();
  drawScreen(wantedScreen());  // the last track at once, before Wi-Fi
  portal::begin();
  // Watchdog (plan: car hardening): if the loop or the Spotify task stops checking in for 30 s,
  // restart. 30 s, not the core's 5: one slow network call may legitimately take 10-15 s.
  esp_task_wdt_init(30, true);
  enableLoopWDT();  // the core feeds it after every loop()
  logHeap("wifi");
}

void loop() {
  portal::tick();
  settings::tick();
  handleTouch();

  if (bootbtn::update() == bootbtn::Event::Long) {
    LOGF("boot: BOOT held 5 s -- opening the setup hotspot");
    portal::startHotspot();
  }

  // Redraw when the wanted screen changes; animate the parts that move.
  const uint32_t now = millis();
  const uint32_t pv = player::version();
  if (pv != sSnapVersion || sSnapRefresh.due(now)) {
    sSnap = player::snapshot();
  }
  const uint32_t changes = portal::stateChanges();
  static bool sWasUp = false;
  const bool up = portal::wifiUp();
  const uint32_t sp = spotify::changes();
  const Screen w = wantedScreen();
  if (w != sScreen) {
    drawScreen(w);
  } else if (changes != sSeenChanges && w == Screen::Waiting) {
    drawScreen(w);  // marks the network being tried
  } else if (up != sWasUp && w == Screen::NowPlaying && !nowplaying::menuOpen() && !nowplaying::volumeOpen()) {
    drawScreen(w);  // the "Waiting for Wi-Fi" pill on or off
  } else if (pv != sSnapVersion && w == Screen::NowPlaying) {
    // A new track redraws everything; the rest (play/pause, device, volume) only what changed.
    // Under the volume overlay only its value moves; closing it redraws the screen.
    if (nowplaying::volumeOpen()) nowplaying::refreshVolume(sSnap);
    else if (sSnap.trackVersion != sDrawnTrack) drawScreen(w);
    else nowplaying::update(sSnap);
  } else if (pv != sSnapVersion && w == Screen::Idle) {
    drawScreen(w);
  } else if (sp != sSeenSpotify) {
    const bool needLogin = spotify::state() != spotify::AuthState::Authorized;
    if (w == Screen::Connected) {
      // The QR changes target with the login; otherwise only the status line.
      if (needLogin != sNeedLogin) drawScreen(w);
      else screens::refreshSpotify();
    } else if (w == Screen::NowPlaying && !nowplaying::menuOpen() && !nowplaying::volumeOpen()) {
      drawScreen(w);  // a Spotify problem appeared or cleared: its pill is over the picture
    } else if (w == Screen::Idle) {
      nowplaying::drawStatusStrip();
    }
  }
  if (changes != sSeenChanges) logHeap("wifi");
  sSeenChanges = changes;
  sWasUp = up;
  sSeenSpotify = sp;
  sSnapVersion = pv;
  sNeedLogin = spotify::state() != spotify::AuthState::Authorized;

  // Command results and like state (Stage 4).
  const uint32_t mv = control::messageVersion();
  if (mv != sSeenMessage) {
    sSeenMessage = mv;
    if (sScreen == Screen::NowPlaying || sScreen == Screen::Idle) nowplaying::showToast(control::message());
  }
  if ((sScreen == Screen::NowPlaying || sScreen == Screen::Idle) && nowplaying::toastExpired()) {
    if (sScreen == Screen::Idle) nowplaying::drawStatusStrip();
    else if (!nowplaying::menuOpen() && !nowplaying::volumeOpen()) drawScreen(Screen::NowPlaying);  // the pill off
  }
  const uint32_t lv = control::likeVersion();
  if (lv != sSeenLike) {
    sSeenLike = lv;
    if (sScreen == Screen::NowPlaying && !nowplaying::volumeOpen() && !nowplaying::menuOpen())
      nowplaying::drawButton(sSnap, nowplaying::Button::Like, sPressedBtn == nowplaying::Button::Like);
  }
  if (sScreen == Screen::NowPlaying && (nowplaying::volumeTimedOut() || nowplaying::menuTimedOut()))
    drawScreen(Screen::NowPlaying);
  if (sScreen == Screen::List) {
    listscreen::refreshIfChanged();
    if (listscreen::timedOut()) {
      sList = nullptr;
      drawScreen(wantedScreen());
    }
  }
  const uint32_t av = art::version();
  if (av != sSeenArt) {
    sSeenArt = av;
    // The new cover: redraw the whole picture (title, copies and all).
    if (sScreen == Screen::NowPlaying && !nowplaying::volumeOpen() && !nowplaying::menuOpen())
      drawScreen(Screen::NowPlaying);
  }

  if (sScreen == Screen::Waiting && sSpinner.due(now)) screens::tickWaiting();
  if (sScreen == Screen::Connected && sSignal.due(now)) screens::tickConnected();
  if (sScreen == Screen::NowPlaying) nowplaying::tick(sSnap);
  if (sScreen == Screen::Idle && sStrip.due(now)) nowplaying::drawStatusStrip();
  if (sScreen == Screen::Idle) nowplaying::tickIdle();

  // Screen off after the idle timeout: nothing playing (idle screen) or paused. Playing music,
  // any screen change and any touch count as activity.
  const bool quiet = sScreen == Screen::Idle || (sScreen == Screen::NowPlaying && !sSnap.playing);
  if (!quiet) sLastActivity = now;
  const uint8_t offMin = settings::get().screenOffMin;
  if (!sScreenOff && quiet && offMin && now - sLastActivity > offMin * 60000UL) {
    sScreenOff = true;
    display::setBrightness(0);
    LOGF("screen: off after %u min idle", offMin);
  } else if (sScreenOff && !quiet) {
    wake();  // the music started again
  }
  if (sHeapLog.due(now)) logHeap("idle");
  delay(2);
}
