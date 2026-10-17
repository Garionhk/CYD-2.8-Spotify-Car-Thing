#include "devices.h"
#include <ArduinoJson.h>
#include <vector>
#include "api.h"
#include "control.h"
#include "player.h"
#include "../util/log.h"

namespace devices {

static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
static std::vector<Item> s_items;
static volatile State s_state = State::Empty;
static volatile uint32_t s_version = 0;
static String s_error;
static volatile bool s_loadRequested = false;
static volatile int16_t s_transferRequested = -1;

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

void load() {
  s_state = State::Loading;
  s_version++;
  s_loadRequested = true;
  spotify::wake();
}

void transfer(uint8_t index) {
  s_transferRequested = index;
  spotify::wake();
}

State state() { return s_state; }
uint32_t version() { return s_version; }

uint8_t count() {
  Lock l;
  return s_items.size();
}

bool item(uint8_t index, Item& out) {
  Lock l;
  if (index >= s_items.size()) return false;
  out = s_items[index];
  return true;
}

String error() {
  Lock l;
  return s_error;
}

static void fetch() {
  static JsonDocument filter;
  if (filter.isNull()) {
    JsonObject d = filter["devices"][0].to<JsonObject>();
    d["id"] = true;
    d["name"] = true;
    d["type"] = true;
    d["is_active"] = true;
    d["is_restricted"] = true;
  }
  JsonDocument doc;
  spotify::ApiResult r;
  const int status = spotify::apiRequest("GET", "/me/player/devices", &doc, &filter, r);
  if (status != 200) {
    {
      Lock l;
      s_error = status < 0 ? "No connection to Spotify" : "Spotify error " + String(status);
    }
    s_state = State::Error;
    s_version++;
    return;
  }
  {
    Lock l;
    s_items.clear();
    for (JsonObjectConst d : doc["devices"].as<JsonArrayConst>()) {
      if (s_items.size() >= MAX_ITEMS) break;
      Item it;
      it.id = d["id"] | "";
      it.name = d["name"] | "";
      it.type = d["type"] | "";
      it.active = d["is_active"] | false;
      it.restricted = d["is_restricted"] | false;
      if (it.id.isEmpty()) continue;  // no id: cannot be targeted
      s_items.push_back(it);
    }
  }
  s_state = State::Ready;
  s_version++;
  LOGF("devices: %u available", count());
}

static void start(uint8_t index) {
  Item it;
  if (!item(index, it)) return;
  if (it.restricted) {
    control::notify(it.name + " can't be controlled from here");
    return;
  }
  JsonDocument body;
  body["device_ids"][0] = it.id;
  body["play"] = true;  // keep the music going on the new device
  String json;
  serializeJson(body, json);
  spotify::ApiResult r;
  const int status = spotify::apiRequest("PUT", "/me/player", nullptr, nullptr, r, &json);
  LOGF("devices: transfer to \"%s\" -> %d %s", it.name.c_str(), status, r.reason.c_str());
  if (status / 100 == 2) {
    control::notify("Playing on " + it.name);
    player::pollSoon(1500);
  } else if (status == 404 || r.reason == "NO_ACTIVE_DEVICE") {
    control::notify(it.name + " is not available - open Spotify on it");
  } else if (r.reason == "PREMIUM_REQUIRED") {
    control::notify("Spotify Premium required");
  } else {
    control::notify("Couldn't switch to " + it.name + " (" + String(status) + ")");
  }
}

void tick() {
  if (s_loadRequested) {
    s_loadRequested = false;
    fetch();
  }
  const int16_t t = s_transferRequested;
  if (t >= 0) {
    s_transferRequested = -1;
    start(t);
  }
}

}  // namespace devices
