#pragma once
#include <Arduino.h>

// XPT2046 by bit-bang, as in the MP3 player project (proven on this board in its stage 0).
// Calibration is stored in the panel's NATIVE portrait frame (240x320) and turned into screen
// coordinates for whatever rotation is active, so one calibration serves rotation 1 and 3.
namespace touch {

// Raw value at native pixel 0 and at the far edge, per axis. Reversed ranges encode a flip.
// swapXY = true when raw X drives native y.
struct Calibration {
  int16_t xAt0, xAtW;  // native x: 0 .. PANEL_W-1
  int16_t yAt0, yAtH;  // native y: 0 .. PANEL_H-1
  int16_t zThreshold;
  uint8_t swapXY;
  uint8_t magic;  // CAL_MAGIC when valid
};
constexpr uint8_t CAL_MAGIC = 0xC2;

struct State {
  bool down = false;      // finger is (debounced) on the screen
  bool pressed = false;   // edge: became down this update
  bool released = false;  // edge: lifted this update
  int16_t x = 0, y = 0;   // screen coordinates; where the press landed
};

void begin();
void setCalibration(const Calibration& cal);
const Calibration& calibration();

// Call every loop iteration. Never blocks.
const State& update();

// One bit-banged read. Returns true if pressure is above the threshold.
bool readRaw(int16_t& rx, int16_t& ry, int16_t& z);
void screenPoint(int16_t rx, int16_t ry, int16_t& sx, int16_t& sy);

// The MP3 player's wizard: native rotation, 4 corners, centre confirm tap. Blocking (setup
// screen only); gives up after 60s without a press. On success applies and returns it.
bool runCalibration(Calibration& out);

}  // namespace touch
