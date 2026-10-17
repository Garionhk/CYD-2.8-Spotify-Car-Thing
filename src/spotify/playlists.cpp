#include "playlists.h"
#include <ArduinoJson.h>
#include <vector>
#include "api.h"
#include "auth.h"
#include "control.h"
#include "player.h"
#include "../util/log.h"

namespace playlists {

static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
static std::vector<Item> s_items;
static volatile State s_state = State::Empty;
static volatile uint16_t s_total = 0;
static volatile uint32_t s_version = 0;
static String s_error;

static volatile bool s_loadRequested = false, s_loadMore = false;
static volatile int16_t s_playRequested = -1;

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

static void bump() { s_version++; }

void load(bool more) {
  s_loadMore = more;
  s_loadRequested = true;
  if (!more) {
    s_state = State::Loading;
    bump();
  }
  spotify::wake();
}

void play(uint16_t index) {
  s_playRequested = index;
  spotify::wake();
}

State state() { return s_state; }
uint16_t count() {
  Lock l;
  return s_items.size();
}
uint16_t total() { return s_total; }
uint32_t version() { return s_version; }

bool item(uint16_t index, Item& out) {
  Lock l;
  if (index >= s_items.size()) return false;
  out = s_items[index];
  return true;
}

String error() {
  Lock l;
  return s_error;
}

static void fail(const String& e) {
  {
    Lock l;
    s_error = e;
  }
  s_state = State::Error;
  bump();
}

static void fetch(bool more) {
  if (!spotify::hasScope("playlist-read-private")) {
    s_state = State::NeedLogin;
    bump();
    LOGF("playlists: this login has no playlist scope -- log in again");
    return;
  }
  uint16_t offset = 0;
  {
    Lock l;
    if (!more) s_items.clear();
    offset = s_items.size();
  }
  if (offset >= MAX_ITEMS) return;

  static JsonDocument filter;
  if (filter.isNull()) {
    filter["total"] = true;
    filter["items"][0]["name"] = true;
    filter["items"][0]["uri"] = true;
    filter["items"][0]["items"]["total"] = true;   // 2026 name
    filter["items"][0]["tracks"]["total"] = true;  // deprecated, older responses
  }
  JsonDocument doc;
  spotify::ApiResult r;
  const uint32_t t0 = millis();
  const int status =
      spotify::apiRequest("GET", "/me/playlists?limit=50&offset=" + String(offset), &doc, &filter, r);
  if (status != 200) {
    if (status == 401) fail("Reconnecting to Spotify... try again");
    else if (status == 403) fail("Spotify refused. Log in again at cyd-spotify.local");
    else if (status < 0) fail("No connection to Spotify");
    else fail("Spotify error " + String(status));
    return;
  }
  s_total = doc["total"] | 0;
  uint16_t added = 0;
  {
    Lock l;
    for (JsonObjectConst p : doc["items"].as<JsonArrayConst>()) {
      if (s_items.size() >= MAX_ITEMS) break;
      Item it;
      it.name = p["name"] | "";
      it.uri = p["uri"] | "";
      it.tracks = p["items"]["total"] | (p["tracks"]["total"] | 0);
      if (it.uri.isEmpty()) continue;
      s_items.push_back(it);
      added++;
    }
  }
  s_state = State::Ready;
  bump();
  LOGF("playlists: %u loaded (%u of %u) in %lu ms", added, count(), s_total, (unsigned long)(millis() - t0));
}

static void start(uint16_t index) {
  Item it;
  if (!item(index, it)) return;
  // {"context_uri":"spotify:playlist:..."} -- built with ArduinoJson so names never need escaping
  // (only the URI goes in, but this keeps it correct whatever it holds).
  JsonDocument body;
  body["context_uri"] = it.uri;
  String json;
  serializeJson(body, json);
  spotify::ApiResult r;
  const int status = spotify::apiRequest("PUT", "/me/player/play", nullptr, nullptr, r, &json);
  LOGF("playlists: play \"%s\" -> %d %s", it.name.c_str(), status, r.reason.c_str());
  if (status / 100 == 2) {
    control::notify("Playing " + it.name);
    player::pollSoon(1200, true);
  } else if (status == 404 || r.reason == "NO_ACTIVE_DEVICE") {
    control::notify("Open Spotify on your phone");
  } else if (r.reason == "PREMIUM_REQUIRED") {
    control::notify("Spotify Premium required");
  } else {
    control::notify("Couldn't start the playlist (" + String(status) + ")");
  }
}

void tick() {
  if (s_loadRequested) {
    s_loadRequested = false;
    fetch(s_loadMore);
  }
  const int16_t p = s_playRequested;
  if (p >= 0) {
    s_playRequested = -1;
    start(p);
  }
}

}  // namespace playlists
