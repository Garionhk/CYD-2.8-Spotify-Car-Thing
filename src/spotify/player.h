#pragma once
#include <Arduino.h>

// What is playing, from GET /me/player (plan §5.3-5.4), polled by the Spotify task:
//   playing 4 s, paused 10 s, nothing active (204) 30 s -- doubled in data-saver mode --
//   plus a poll just after the current track should end, and one ~600 ms after a control.
// The UI reads snapshot() and moves the progress bar on by itself between polls.
namespace player {

struct Snapshot {
  bool known = false;     // at least one poll answered
  bool cached = false;    // the last track from flash (fast boot), not yet confirmed by a poll
  bool active = false;    // a device has a session (false on 204)
  bool hasItem = false;   // something to show (false for an ad, or an empty session)
  bool playing = false;
  bool episode = false;   // podcast: artist holds the show name, no like
  bool isAd = false;
  bool supportsVolume = false;
  int8_t volume = -1;     // percent, -1 unknown
  uint32_t progressMs = 0, durationMs = 0;
  uint32_t fetchedAt = 0;  // millis() when progressMs was true
  String uri, track, artist, album, device;
  String artUrl;           // ~300 px image (Stage 5), else the smallest
  String artUrlSmall;      // ~64 px fallback
  uint32_t version = 0;    // bumps on every change worth redrawing
  uint32_t trackVersion = 0;  // bumps when the item changes

  // Progress now, moved on locally from the last poll.
  uint32_t progressNow() const {
    if (!playing) return progressMs;
    const uint32_t p = progressMs + (millis() - fetchedAt);
    return durationMs && p > durationMs ? durationMs : p;
  }
};

// Fast boot: the last track seen before power-off, so the screen has something to show at
// once. Saved by the Spotify task whenever the track changes (write to a temp file, rename).
bool loadLast();

// Copy of the latest state. Cheap to call; compare version to see if anything changed.
Snapshot snapshot();
uint32_t version();

// Ask for a poll about delayMs from now (after a control action). With expectNewTrack (next /
// previous), polls once more a second later if the track has not changed yet.
void pollSoon(uint32_t delayMs = 600, bool expectNewTrack = false);

// A touch on the screen: poll now (if no poll happened in the last 2 s) -- so pressing play on
// the phone and then touching the CYD shows it at once.
void nudge();

// Show a control's effect at once; the confirmation poll corrects it if Spotify disagrees.
void assumePlaying(bool playing);
void assumeVolume(int8_t percent);

// Spotify task only: polls if due. Returns false if it did not need the network.
bool tick();

}  // namespace player
