#pragma once
#include "../display/ui.h"

// The Stage 1 screens. Each draws in full; tick*() redraw only the parts that change.
namespace screens {

// Bottom-right "Calibrate" button, on every Stage 1 screen.
extern const ui::Rect R_CALIBRATE;

void drawSetup();      // hotspot up: QR to join it, then the page address
void drawWaiting();    // saved networks, the one being tried, a spinner
void tickWaiting();
void drawConnected();  // joined: page address + QR, signal
void tickConnected();
void refreshSpotify();  // Spotify status line on the connected screen

void drawCalibrateButton(bool pressed);

}  // namespace screens
