#pragma once
#include <Arduino.h>

// Playback controls (plan §5.3, Stage 4). The UI queues a command and returns at once; the
// Spotify task sends it, then polls ~0.9 s later to confirm. Play/pause, volume and like are
// shown immediately (optimistic) and corrected by that poll if Spotify disagrees.
namespace control {

enum class Cmd : uint8_t { PlayPause, Next, Previous, ToggleLike, VolumeStep, Mute };

// From the UI. VolumeStep takes +10 / -10; steps add up and are sent once taps settle.
// Mute: Spotify's API has no mute, so it sets the volume to 0 and back to what it was -- only
// on devices that accept volume (supports_volume; phones usually do not).
void send(Cmd c, int8_t arg = 0);

// Like state of the current track: -1 unknown / not likeable, 0 no, 1 yes.
int8_t liked(const String& uri);
uint32_t likeVersion();  // bumps whenever the like state changes, for the UI to redraw

// The volume the UI should show: the pending target while steps are being sent, else -1.
int8_t pendingVolume();

// A short message for the person after a command failed ("Open Spotify on your phone").
// Bumps messageVersion() each time; the UI shows it for a few seconds.
String message();
uint32_t messageVersion();
void notify(const String& m);  // the same channel, for other modules (playlists)

// Spotify task only: sends queued commands and checks the like status of a new track.
void tick();

// Test hook (Stage 5 acceptance): sends "next" every intervalMs, n times (0 stops).
void testSkips(uint16_t n, uint32_t intervalMs = 6000);

}  // namespace control
