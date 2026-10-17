#include "board.h"
#include "../config.h"

namespace led {

void begin() {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  set(false, false, false);
}

void set(bool r, bool g, bool b) {  // active LOW
  digitalWrite(PIN_LED_R, r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

}  // namespace led

namespace bootbtn {

constexpr uint32_t DEBOUNCE_MS = 30;

static bool s_stable = false;  // true = pressed
static bool s_lastRaw = false;
static uint32_t s_rawChanged = 0;
static uint32_t s_pressedAt = 0;
static bool s_longFired = false;

void begin() { pinMode(PIN_BOOT_BTN, INPUT_PULLUP); }

uint32_t heldMs() { return s_stable ? millis() - s_pressedAt : 0; }

Event update() {
  uint32_t now = millis();
  bool raw = digitalRead(PIN_BOOT_BTN) == LOW;
  if (raw != s_lastRaw) {
    s_lastRaw = raw;
    s_rawChanged = now;
  }
  if (raw != s_stable && now - s_rawChanged >= DEBOUNCE_MS) {
    s_stable = raw;
    if (s_stable) {
      s_pressedAt = now;
      s_longFired = false;
    } else if (!s_longFired) {
      return Event::Short;
    }
  }
  if (s_stable && !s_longFired && now - s_pressedAt >= BOOT_LONG_PRESS_MS) {
    s_longFired = true;
    return Event::Long;
  }
  return Event::None;
}

}  // namespace bootbtn
