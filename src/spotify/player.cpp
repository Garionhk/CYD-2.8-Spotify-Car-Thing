#include "player.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "api.h"
#include "../storage/settings.h"
#include "../util/heap.h"
#include "../util/log.h"

namespace player {

static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
static Snapshot s_snap;
static volatile uint32_t s_version = 0;
static volatile uint32_t s_nextPollAt = 0;  // 0 = now
static volatile bool s_expectNewTrack = false;
static String s_uriBeforeSkip;
static String s_endCatchUri;  // the track the end-of-track poll was already spent on
static uint32_t s_polls = 0;

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

Snapshot snapshot() {
  Lock l;
  return s_snap;
}

uint32_t version() { return s_version; }

void pollSoon(uint32_t delayMs, bool expectNewTrack) {
  if (expectNewTrack) {
    Lock l;
    s_uriBeforeSkip = s_snap.uri;
  }
  s_expectNewTrack = expectNewTrack;
  s_nextPollAt = millis() + delayMs;
}

void assumePlaying(bool playing) {
  Lock l;
  if (s_snap.playing == playing) return;
  s_snap.progressMs = s_snap.progressNow();  // freeze or restart the local clock from here
  s_snap.fetchedAt = millis();
  s_snap.playing = playing;
  s_version = ++s_snap.version;
}

void assumeVolume(int8_t percent) {
  Lock l;
  if (s_snap.volume == percent) return;
  s_snap.volume = percent;
  s_version = ++s_snap.version;
}

static uint32_t s_quietSince = 0;  // when playback stopped (0 while playing)
static uint32_t s_lastPollAt = 0;
static const uint32_t RECENT_MS = 2 * 60 * 1000;

// Playing 4 s. Paused or nothing playing: fast for the first 2 minutes -- that is when people
// press play again -- then 8 / 10 s (owner: the old 10 / 30 s took up to 30 s to pick music up).
// Data saver doubles everything.
static uint32_t intervalMs(const Snapshot& s) {
  const uint32_t k = settings::get().pollPreset == 1 ? 2 : 1;
  if (s.playing) return 4000 * k;
  const bool recent = s_quietSince && millis() - s_quietSince < RECENT_MS;
  if (!s.active) return (recent ? 5000 : 10000) * k;
  return (recent ? 4000 : 8000) * k;
}

void nudge() {
  // A touch on the screen: look now, unless a poll just happened or one is due soon anyway.
  if (millis() - s_lastPollAt < 2000) return;
  if (s_nextPollAt && (int32_t)(s_nextPollAt - millis()) < 1000) return;
  pollSoon(150);
}

static const JsonDocument& filter() {
  static JsonDocument f;
  if (f.isNull()) {
    f["is_playing"] = true;
    f["progress_ms"] = true;
    f["currently_playing_type"] = true;
    f["device"]["name"] = true;
    f["device"]["volume_percent"] = true;
    f["device"]["supports_volume"] = true;
    JsonObject item = f["item"].to<JsonObject>();
    item["uri"] = true;
    item["name"] = true;
    item["duration_ms"] = true;
    item["artists"][0]["name"] = true;  // [0] in a filter applies to every element
    item["album"]["name"] = true;
    item["album"]["images"][0]["url"] = true;
    item["album"]["images"][0]["width"] = true;
    item["images"][0]["url"] = true;  // episodes carry their own images
    item["images"][0]["width"] = true;
    item["show"]["name"] = true;
  }
  return f;
}

// The image closest to `want` px wide, and the smallest one.
static void pickImages(JsonArrayConst images, String& best, String& smallest) {
  int bestDiff = INT_MAX, smallestW = INT_MAX;
  for (JsonObjectConst im : images) {
    const int w = im["width"] | 0;
    const char* url = im["url"] | "";
    if (!*url) continue;
    const int diff = abs(w - 300);
    if (diff < bestDiff) {
      bestDiff = diff;
      best = url;
    }
    if (w < smallestW) {
      smallestW = w;
      smallest = url;
    }
  }
}

static const char* LAST_PATH = "/last.json";
static const char* LAST_TMP = "/last.tmp";

// One small write per track change. A power cut mid-write leaves last.tmp, never a torn last.json.
static void saveLast(const Snapshot& s) {
  JsonDocument d;
  d["uri"] = s.uri;
  d["track"] = s.track;
  d["artist"] = s.artist;
  d["album"] = s.album;
  d["device"] = s.device;
  d["duration"] = s.durationMs;
  d["progress"] = s.progressMs;
  d["art"] = s.artUrl;
  d["artSmall"] = s.artUrlSmall;
  d["episode"] = s.episode;
  fs::File f = LittleFS.open(LAST_TMP, "w");
  if (!f) return;
  const bool ok = serializeJson(d, f) > 0;
  f.close();
  if (ok) {
    LittleFS.remove(LAST_PATH);
    LittleFS.rename(LAST_TMP, LAST_PATH);
  }
}

bool loadLast() {
  fs::File f = LittleFS.open(LAST_PATH, "r");
  if (!f) return false;
  JsonDocument d;
  const bool ok = !deserializeJson(d, f);
  f.close();
  if (!ok || !d["uri"].is<const char*>()) return false;
  Lock l;
  Snapshot& s = s_snap;
  s.cached = true;
  s.hasItem = true;
  s.playing = false;
  s.uri = d["uri"] | "";
  s.track = d["track"] | "";
  s.artist = d["artist"] | "";
  s.album = d["album"] | "";
  s.device = d["device"] | "";
  s.durationMs = d["duration"] | 0;
  s.progressMs = d["progress"] | 0;
  s.artUrl = d["art"] | "";
  s.artUrlSmall = d["artSmall"] | "";
  s.episode = d["episode"] | false;
  s.fetchedAt = millis();
  s.version = s.trackVersion = 1;
  s_version = 1;
  LOGF("player: last track from flash: \"%s\" - %s", s.track.c_str(), s.artist.c_str());
  return true;
}

static void apply(const JsonDocument& doc, int status) {
  Snapshot n;
  n.known = true;
  n.fetchedAt = millis();
  if (status == 200) {
    n.active = true;
    n.playing = doc["is_playing"] | false;
    n.progressMs = doc["progress_ms"] | 0;
    const String type = doc["currently_playing_type"] | "";
    n.isAd = type == "ad";
    n.episode = type == "episode";
    n.device = doc["device"]["name"] | "";
    n.supportsVolume = doc["device"]["supports_volume"] | false;
    n.volume = doc["device"]["volume_percent"].isNull() ? -1 : (int8_t)(doc["device"]["volume_percent"] | 0);
    JsonObjectConst item = doc["item"];
    if (!item.isNull()) {
      n.hasItem = true;
      n.uri = item["uri"] | "";
      n.track = item["name"] | "";
      n.durationMs = item["duration_ms"] | 0;
      if (n.episode) {
        n.artist = item["show"]["name"] | "";
        pickImages(item["images"], n.artUrl, n.artUrlSmall);
      } else {
        for (JsonObjectConst a : item["artists"].as<JsonArrayConst>()) {
          if (n.artist.length()) n.artist += ", ";
          n.artist += a["name"] | "";
        }
        n.album = item["album"]["name"] | "";
        pickImages(item["album"]["images"], n.artUrl, n.artUrlSmall);
      }
    }
  }

  Lock l;
  const Snapshot& o = s_snap;
  const bool trackChanged = n.uri != o.uri || n.hasItem != o.hasItem || n.known != o.known;
  // Progress drifts a little between polls; only a jump (a seek) is worth a redraw by itself.
  const int32_t drift = (int32_t)n.progressMs - (int32_t)o.progressNow();
  const bool changed = trackChanged || n.known != o.known || n.active != o.active || n.playing != o.playing ||
                       n.device != o.device || n.volume != o.volume || n.isAd != o.isAd || abs(drift) > 2000;
  n.trackVersion = o.trackVersion + (trackChanged ? 1 : 0);
  if (trackChanged && n.hasItem) saveLast(n);
  n.version = o.version + (changed ? 1 : 0);
  s_snap = n;
  if (changed) s_version = n.version;
  if (trackChanged) {
    // The position when first seen: for a track just skipped to, that is how late we noticed.
    if (n.hasItem)
      LOGF("player: %s \"%s\" - %s (%s, volume %s), seen at %lu.%lu s, poll #%lu", n.playing ? "playing" : "paused",
           n.track.c_str(), n.artist.c_str(), n.device.c_str(), n.supportsVolume ? "settable" : "not settable",
           (unsigned long)(n.progressMs / 1000), (unsigned long)(n.progressMs % 1000 / 100), (unsigned long)s_polls);
    else LOGF("player: %s", n.active ? "session without a track" : "nothing playing");
  } else if (changed) {
    LOGF("player: %s, %s, volume %d (%s), poll #%lu", n.playing ? "playing" : "paused", n.device.c_str(), n.volume,
         n.supportsVolume ? "settable" : "device refuses volume control", (unsigned long)s_polls);
  }
}

bool tick() {
  const uint32_t now = millis();
  if (s_nextPollAt && (int32_t)(now - s_nextPollAt) < 0) return false;

  JsonDocument doc;
  uint32_t retryAfter = 0;
  const int status = spotify::apiGet("/me/player?additional_types=episode", doc, filter(), retryAfter);
  s_polls++;

  Snapshot cur;
  s_lastPollAt = millis();
  if (status == 200 || status == 204) {
    apply(doc, status);
    cur = snapshot();
    if (cur.playing) s_quietSince = 0;
    else if (!s_quietSince) s_quietSince = millis();
    uint32_t wait = intervalMs(cur);
    // Catch the song change: poll just after the track should end, if that is sooner. Once
    // per track: Spotify can report the position at (or past) the end for a while, and without
    // this limit that meant a poll every ~0.8 s until the next track started.
    if (cur.playing && cur.durationMs && cur.uri != s_endCatchUri) {
      const uint32_t left = cur.durationMs - min(cur.progressMs, cur.durationMs);
      if (left + 800 < wait) {
        wait = max<uint32_t>(left + 800, 1500);
        s_endCatchUri = cur.uri;
        LOGF("player: track ends in %lu ms -- next poll in %lu ms", (unsigned long)left, (unsigned long)wait);
      }
    }
    if (s_expectNewTrack) {
      s_expectNewTrack = false;
      // Spotify can take a moment to switch after next/previous: look once more, sooner.
      if (cur.uri == s_uriBeforeSkip) wait = min<uint32_t>(wait, 1000);
    }
    s_nextPollAt = millis() + wait;
  } else if (status == 429) {
    const uint32_t wait = max<uint32_t>(retryAfter, 5000);
    LOGF("player: rate limited, next poll in %lu s", (unsigned long)(wait / 1000));
    s_nextPollAt = millis() + wait;
  } else if (status == 401) {
    s_nextPollAt = 0;  // straight after the refresh
  } else {
    s_nextPollAt = millis() + 5000;
  }
  if (s_polls % 50 == 1) {
    LOGF("player: poll #%lu, next in %lu ms", (unsigned long)s_polls, (unsigned long)(s_nextPollAt - millis()));
    logHeap("poll");
  }
  return true;
}

}  // namespace player
