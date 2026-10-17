#include "control.h"
#include <ArduinoJson.h>
#include "api.h"
#include "player.h"
#include "../util/log.h"

namespace control {

struct Item {
  Cmd cmd;
  int8_t arg;
};

static QueueHandle_t s_queue = xQueueCreate(8, sizeof(Item));
static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();

static String s_message;
static volatile uint32_t s_messageVersion = 0;

// Like status, for one track at a time.
static String s_likeUri;           // the track it belongs to
static volatile int8_t s_liked = -1;
static volatile uint32_t s_likeVersion = 0;
static uint32_t s_likeCheckedTrack = UINT32_MAX;  // player trackVersion last checked

// Volume: taps change the target at once; the task sends it once taps pause for 350 ms.
static volatile int8_t s_volTarget = -1;
static volatile uint32_t s_volChangedAt = 0;
static int8_t s_volSent = -1;
static int8_t s_unmuteTo = 50;  // the volume before mute

static volatile uint16_t s_testSkipsLeft = 0;
static uint32_t s_testInterval = 6000, s_testNextAt = 0;
static const uint32_t CONFIRM_MS = 900;  // poll after a command: Spotify needs a moment
static const uint32_t VOLUME_SETTLE_MS = 350;

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

static void say(const String& m) {
  {
    Lock l;
    s_message = m;
  }
  s_messageVersion++;
  LOGF("control: %s", m.c_str());
}

void notify(const String& m) { say(m); }

String message() {
  Lock l;
  return s_message;
}
uint32_t messageVersion() { return s_messageVersion; }

int8_t liked(const String& uri) {
  Lock l;
  return uri.length() && uri == s_likeUri ? s_liked : -1;
}

int8_t pendingVolume() { return s_volTarget; }
uint32_t likeVersion() { return s_likeVersion; }

void testSkips(uint16_t n, uint32_t intervalMs) {
  s_testInterval = max<uint32_t>(intervalMs, 3000);
  s_testNextAt = millis();
  s_testSkipsLeft = n;
  LOGF("control: test -- %u skips, one every %lu ms", n, (unsigned long)s_testInterval);
  spotify::wake();
}

void send(Cmd c, int8_t arg) {
  const player::Snapshot s = player::snapshot();
  // Optimistic: show the effect now, from the UI's own task.
  switch (c) {
    case Cmd::PlayPause:
      player::assumePlaying(!s.playing);
      break;
    case Cmd::ToggleLike: {
      Lock l;
      if (s.uri.length() && s.uri == s_likeUri && s_liked >= 0) s_liked = !s_liked;
      s_likeVersion++;
      break;
    }
    case Cmd::Mute:
    case Cmd::VolumeStep: {
      const int base = s_volTarget >= 0 ? s_volTarget : (s.volume >= 0 ? s.volume : 50);
      if (c == Cmd::Mute) {
        if (base > 0) {
          s_unmuteTo = base;
          s_volTarget = 0;
        } else {
          s_volTarget = s_unmuteTo > 0 ? s_unmuteTo : 50;
        }
      } else {
        s_volTarget = constrain(base + arg, 0, 100);
      }
      s_volChangedAt = millis();
      player::assumeVolume(s_volTarget);
      spotify::wake();
      return;  // sent by tick() once the taps settle
    }
    default:
      break;
  }
  const Item it{c, arg};
  if (xQueueSend(s_queue, &it, 0) != pdTRUE) {
    say("Busy - try again");
    return;
  }
  spotify::wake();
}

// Plain language for a failed command (plan §7, "Error" screen texts).
static String explain(int status, const spotify::ApiResult& r, Cmd c) {
  if (status < 0) return "No connection to Spotify";
  if (status == 404 || r.reason == "NO_ACTIVE_DEVICE") return "Open Spotify on your phone";
  if (r.reason == "PREMIUM_REQUIRED") return "Spotify Premium required";
  if (r.reason == "VOLUME_CONTROL_DISALLOW" || (c == Cmd::VolumeStep && status == 403))
    return "This device doesn't allow volume control";
  if (status == 429) return "Rate limited, retrying in " + String(max<uint32_t>(r.retryAfterMs / 1000, 1)) + " s";
  if (status == 401) return "Reconnecting to Spotify...";
  if (status == 403) return "Spotify didn't allow that right now";
  return "Spotify error " + String(status);
}

static int run(const char* method, const String& path, Cmd c) {
  spotify::ApiResult r;
  const uint32_t t0 = millis();
  const int status = spotify::apiRequest(method, path, nullptr, nullptr, r);
  LOGF("control: %s %s -> %d in %lu ms", method, path.c_str(), status, (unsigned long)(millis() - t0));
  if (status < 200 || status >= 300) say(explain(status, r, c));
  return status;
}

static void checkLike(const player::Snapshot& s) {
  s_likeCheckedTrack = s.trackVersion;
  if (s.uri.isEmpty() || s.isAd) {
    Lock l;
    s_likeUri = "";
    s_liked = -1;
    s_likeVersion++;
    return;
  }
  JsonDocument doc;
  spotify::ApiResult r;
  const int status = spotify::apiRequest("GET", "/me/library/contains?uris=" + s.uri, &doc, nullptr, r);
  int8_t v = -1;
  if (status == 200 && doc.is<JsonArray>()) v = doc[0].as<bool>() ? 1 : 0;
  {
    Lock l;
    s_likeUri = s.uri;
    s_liked = v;
  }
  s_likeVersion++;
  LOGF("control: %s %s", s.track.c_str(), v == 1 ? "is liked" : v == 0 ? "is not liked" : "like status unknown");
}

void tick() {
  Item it;
  while (xQueueReceive(s_queue, &it, 0) == pdTRUE) {
    const player::Snapshot s = player::snapshot();
    switch (it.cmd) {
      case Cmd::PlayPause:
        // The snapshot already shows the assumed state: playing now means "play" was asked.
        run("PUT", s.playing ? "/me/player/play" : "/me/player/pause", it.cmd);
        player::pollSoon(CONFIRM_MS);
        break;
      case Cmd::Next:
        if (run("POST", "/me/player/next", it.cmd) / 100 == 2) player::pollSoon(CONFIRM_MS, true);
        break;
      case Cmd::Previous:
        if (run("POST", "/me/player/previous", it.cmd) / 100 == 2) player::pollSoon(CONFIRM_MS, true);
        break;
      case Cmd::ToggleLike: {
        int8_t want;
        String uri;
        {
          Lock l;
          want = s_liked;
          uri = s_likeUri;
        }
        if (uri.isEmpty() || want < 0 || uri != s.uri) {
          say("Can't like this right now");
          break;
        }
        // s_liked already shows the wanted state (optimistic).
        if (run(want ? "PUT" : "DELETE", "/me/library?uris=" + uri, it.cmd) / 100 != 2) {
          Lock l;
          if (s_likeUri == uri) s_liked = !want;  // undo
          s_likeVersion++;
        }
        break;
      }
      default:
        break;
    }
  }

  // Volume: once the taps have settled for a moment, send the latest target.
  if (s_volTarget >= 0 && s_volTarget != s_volSent && millis() - s_volChangedAt >= VOLUME_SETTLE_MS) {
    const int8_t v = s_volTarget;
    const int status = run("PUT", "/me/player/volume?volume_percent=" + String(v), Cmd::VolumeStep);
    s_volSent = v;
    player::pollSoon(CONFIRM_MS);
    if (status / 100 != 2) s_volSent = -1;
  }
  if (s_volTarget >= 0 && s_volTarget == s_volSent && millis() - s_volChangedAt > 3000) {
    s_volTarget = -1;  // the polls show the real volume again
    s_volSent = -1;
  }

  // Test skips: one "next" per interval while any are left.
  if (s_testSkipsLeft && (int32_t)(millis() - s_testNextAt) >= 0) {
    s_testSkipsLeft--;
    s_testNextAt = millis() + s_testInterval;
    LOGF("control: test skip (%u left)", s_testSkipsLeft);
    if (run("POST", "/me/player/next", Cmd::Next) / 100 == 2) player::pollSoon(CONFIRM_MS, true);
  }

  // Like status: once per new track.
  const player::Snapshot s = player::snapshot();
  if (s.known && s.trackVersion != s_likeCheckedTrack) checkLike(s);
}

}  // namespace control
