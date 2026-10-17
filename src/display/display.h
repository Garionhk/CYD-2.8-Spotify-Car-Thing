#pragma once
#include <TFT_eSPI.h>

// The panel, done the MP3 player project's way: TFT_eSPI is built for ST7789, and when the
// stored panel setting says ILI9341 the firmware resets the controller and sends the ILI9341
// init table itself, then writes the right MADCTL on every rotation. One binary, both glasses.
namespace display {

extern TFT_eSPI tft;

// tft.init(), the controller patch if needed, rotation and inversion. Backlight comes on last.
void begin(uint16_t panel, uint8_t rotation, bool invert, uint8_t brightnessPct);
void setRotation(uint8_t rot);      // 0-3; the app uses 1 or 3
void setBrightness(uint8_t percent);  // 0..100, PWM on the backlight
void setInvert(bool invert);
uint8_t brightness();
uint8_t rotation();
bool inverted();
uint16_t panel();
const char* panelName(uint16_t panel);

// Blind recovery for a board showing nothing: a finger held from power-up for 8 s switches
// controller and restarts; the blue LED blinks from 4 s. Returns in a few ms when untouched.
void bootGesture();

}  // namespace display
