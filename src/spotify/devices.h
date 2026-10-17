#pragma once
#include <Arduino.h>

// Spotify Connect devices (GET /me/player/devices) and moving playback to one
// (PUT /me/player {"device_ids":[id], "play":true}). The UI asks; the Spotify task does the
// network. Uses the playback scopes the login already has.
namespace devices {

enum class State : uint8_t { Empty, Loading, Ready, Error };

struct Item {
  String id;
  String name;
  String type;  // Smartphone, Computer, Speaker, TV, AVR, Automobile, ...
  bool active = false;
  bool restricted = false;  // accepts no Web API commands
};

constexpr uint8_t MAX_ITEMS = 16;

// UI side
void load();
void transfer(uint8_t index);
State state();
uint8_t count();
bool item(uint8_t index, Item& out);
String error();
uint32_t version();

// Spotify task only.
void tick();

}  // namespace devices
