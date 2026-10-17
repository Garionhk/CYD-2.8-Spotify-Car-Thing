#pragma once
#include <Arduino.h>

namespace led {
void begin();
void set(bool r, bool g, bool b);
}  // namespace led

namespace bootbtn {
enum class Event { None, Short, Long };
void begin();
Event update();  // call every loop; Long fires once when the hold reaches BOOT_LONG_PRESS_MS
uint32_t heldMs();
}  // namespace bootbtn
