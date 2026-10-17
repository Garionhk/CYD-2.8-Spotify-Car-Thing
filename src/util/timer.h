#pragma once
#include <Arduino.h>

// Non-blocking periodic timer: if (t.due()) { ... }
struct Every {
  uint32_t intervalMs;
  uint32_t last = 0;

  explicit Every(uint32_t ms) : intervalMs(ms) {}

  bool due(uint32_t now = millis()) {
    if (now - last < intervalMs) return false;
    last = now;
    return true;
  }
  void reset(uint32_t now = millis()) { last = now; }
};
