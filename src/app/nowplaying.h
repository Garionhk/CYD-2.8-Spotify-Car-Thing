#pragma once
#include "../spotify/player.h"

// Now Playing, design B revision 3 (docs/mockups/B3_big_cover.png), 320x240:
//   the cover at 225x225 (the 300 px image decoded 1:1, every 4th pixel and row dropped), its
//   bottom darkened, with title (16 px, scrolls), artist and "1:08 ---- 4:34" (12 px) over it,
//   each glyph with a 1 px shadow; the heart on its top right corner; and a column of four
//   buttons on the right (78 px wide): previous, play/pause, next, mute.
//   Double-tapping the cover opens a menu: Playlists, Devices, Volume, Close.
//
// Text over the picture: while the cover is decoded, the artist is blended in and the title and
// time areas are copied (~9 KB), so the scrolling title and the clock redraw over the picture.
namespace nowplaying {

void draw(const player::Snapshot& s);    // full screen (new track, new cover, after an overlay)
void update(const player::Snapshot& s);  // same track: buttons, heart, progress, times
void tick(const player::Snapshot& s);    // progress + times + title marquee; call often

void drawIdle(const player::Snapshot& s);  // nothing playing / no active device
void tickIdle();

// Idle screen's top strip: Spotify problems in plain language and Wi-Fi strength.
void drawStatusStrip();
// A message for 4 s (a pill over the picture on Now Playing); toastExpired() is true once
// when it is over -- Now Playing then needs draw() to restore the picture under it.
void showToast(const String& msg);
bool toastExpired();

// Touch targets.
enum class Button : uint8_t { None, Previous, PlayPause, Next, Mute, Like, Cover };
Button buttonAt(int16_t x, int16_t y);
void drawButton(const player::Snapshot& s, Button b, bool pressed);

// Cover menu (double tap).
enum class MenuAction : uint8_t { None, Close, Playlists, Devices, Volume };
void openMenu(const player::Snapshot& s);
bool menuOpen();
MenuAction menuTouch(const player::Snapshot& s, int16_t x, int16_t y, bool pressed, bool released);
bool menuTimedOut();  // true once when it closes by itself

// Volume overlay.
void openVolume(const player::Snapshot& s);
bool volumeOpen();
void refreshVolume(const player::Snapshot& s);
bool volumeTouch(const player::Snapshot& s, int16_t x, int16_t y, bool pressed, bool released);  // true: closed
bool volumeTimedOut();  // true once when it closes by itself

// UTF-8 -> what the built-in ASCII fonts can draw (accents dropped, others '?').
String printable(const String& utf8);

// One line of text in any script on a solid background (LittleFS font; ASCII fallback).
void textLine(const String& text, int16_t x, int16_t y, int16_t w, uint16_t fg, uint16_t bg, bool centre = false);

}  // namespace nowplaying
