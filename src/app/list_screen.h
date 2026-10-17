#pragma once
#include <Arduino.h>

// A full-screen picker, opened from the cover menu: Back, four big rows (a name and a line
// under it), page up / down buttons on the right -- buttons rather than swiping, for a resistive
// panel in a moving car. Tapping a row chooses it. The playlist and device pickers are two
// Sources (src/app/lists.cpp).
namespace listscreen {

enum class Status : uint8_t { Loading, Ready, Message };

struct Row {
  String name;
  String sub;
  bool highlight = false;  // e.g. the device playing now: the line under it in green
  bool dim = false;        // e.g. a device that cannot be controlled
};

struct Source {
  const char* title;
  void (*load)(bool more);                          // the first page, or the next one
  Status (*status)(String& line1, String& line2);   // Message: why there is no list
  uint16_t (*count)();                              // rows loaded
  uint16_t (*total)();                              // rows on the account (> count: more pages)
  bool (*row)(uint16_t index, Row& out);
  void (*choose)(uint16_t index);
  uint32_t (*version)();                            // bumps when the list changes
  bool reloadOnOpen;                                // devices change; playlists are kept
};

enum class Result : uint8_t { None, Back, Chosen };

void open(const Source& source);
void refreshIfChanged();  // call every loop while open
Result touch(int16_t x, int16_t y, bool pressed, bool released);
bool timedOut();  // true once after 30 s without a touch

}  // namespace listscreen
