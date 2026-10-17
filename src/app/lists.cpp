#include "lists.h"
#include "../spotify/devices.h"
#include "../spotify/playlists.h"

namespace lists {

using listscreen::Row;
using listscreen::Status;

// ---------------------------------------------------------------------------
// Playlists
// ---------------------------------------------------------------------------
static Status playlistStatus(String& l1, String& l2) {
  switch (playlists::state()) {
    case playlists::State::Ready:
      if (playlists::count() == 0) {
        l1 = "No playlists on this account";
        return Status::Message;
      }
      return Status::Ready;
    case playlists::State::NeedLogin:
      l1 = "Log in again at cyd-spotify.local";
      l2 = "Spotify needs your OK for playlists";
      return Status::Message;
    case playlists::State::Error:
      l1 = playlists::error();
      l2 = "Tap Back, then try again";
      return Status::Message;
    default:
      return Status::Loading;
  }
}

static bool playlistRow(uint16_t i, Row& out) {
  playlists::Item it;
  if (!playlists::item(i, it)) return false;
  out.name = it.name;
  out.sub = String(it.tracks) + (it.tracks == 1 ? " song" : " songs");
  return true;
}

const listscreen::Source PLAYLISTS = {
    "Playlists",
    [](bool more) { playlists::load(more); },
    playlistStatus,
    playlists::count,
    playlists::total,
    playlistRow,
    [](uint16_t i) { playlists::play(i); },
    playlists::version,
    false,
};

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------
static Status deviceStatus(String& l1, String& l2) {
  switch (devices::state()) {
    case devices::State::Ready:
      if (devices::count() == 0) {
        l1 = "No devices found";
        l2 = "Open Spotify on a phone, computer or speaker";
        return Status::Message;
      }
      return Status::Ready;
    case devices::State::Error:
      l1 = devices::error();
      l2 = "Tap Back, then try again";
      return Status::Message;
    default:
      return Status::Loading;
  }
}

static bool deviceRow(uint16_t i, Row& out) {
  devices::Item it;
  if (!devices::item(i, it)) return false;
  out.name = it.name;
  if (it.active) {
    out.sub = "Playing here";
    out.highlight = true;
  } else if (it.restricted) {
    out.sub = it.type + " - can't be controlled";
    out.dim = true;
  } else {
    out.sub = it.type;
  }
  return true;
}

const listscreen::Source DEVICES = {
    "Devices",
    [](bool) { devices::load(); },
    deviceStatus,
    []() -> uint16_t { return devices::count(); },
    []() -> uint16_t { return devices::count(); },
    deviceRow,
    [](uint16_t i) { devices::transfer(i); },
    devices::version,
    true,  // devices come and go: always fetch fresh
};

}  // namespace lists
