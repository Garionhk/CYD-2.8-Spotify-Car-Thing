#pragma once
#include <Arduino.h>

// The owner's playlists (GET /me/playlists, 50 per page) and starting one
// (PUT /me/player/play with its context_uri). The UI asks; the Spotify task does the network.
// Needs the playlist-read-private scope: a login from before playlists were added must be
// renewed once (State::NeedLogin).
namespace playlists {

enum class State : uint8_t { Empty, Loading, Ready, NeedLogin, Error };

struct Item {
  String name;
  String uri;
  uint16_t tracks = 0;
};

constexpr uint16_t MAX_ITEMS = 100;  // two pages; names in RAM only (~4 KB)

// UI side
void load(bool more = false);  // first page, or the next one (more)
void play(uint16_t index);
State state();
uint16_t count();   // loaded
uint16_t total();   // on the account
bool item(uint16_t index, Item& out);
String error();
uint32_t version();  // bumps on every change

// Spotify task only.
void tick();

}  // namespace playlists
