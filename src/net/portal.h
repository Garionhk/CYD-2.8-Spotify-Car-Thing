#pragma once
#include <Arduino.h>

// Wi-Fi and the setup web page, on the CYD Stream Deck's pattern (itself from WOL-NTFY's
// webconfig and the MP3 player's upload mode): one page for every mode, an open hotspot with
// DNS that answers every name so a phone pops the page up by itself, HTTP Basic login as
// "admin" once a password is set, mDNS, and the log at /log.
//
// Differences for the car (build plan §6):
//   - up to 3 saved networks, tried in priority order (home Wi-Fi, phone hotspot, ...)
//   - when none can be joined the station keeps scanning in the background; the hotspot does
//     NOT come up by itself. It opens only with no network saved, or on a BOOT hold (5 s).
//   - the address on the LAN is http://cyd-spotify.local
//
// The page never sends a stored password back to the browser. Basic auth over plain HTTP
// keeps casual hands off; it is not encryption.
namespace portal {

enum class WifiState : uint8_t {
  NoNetworks,  // nothing saved (hotspot is up)
  Scanning,    // looking for a saved network
  Joining,     // WiFi.begin() on one of them
  Waiting,     // none in range; next scan soon
  Up,          // joined
};

void begin();
void tick();  // call every loop; never blocks except for the page's own Scan button

void startHotspot();  // BOOT hold; the station stops scanning while it is up

WifiState wifiState();
bool wifiUp();
bool apActive();
String problem();  // a Wi-Fi problem in plain language ("Wrong password for X"), else empty
String apName();
String ssid();           // network joined or being joined
int8_t rssi();
String url();            // "http://<LAN address>", or the hotspot's
String mdnsUrl();        // "http://cyd-spotify.local" once joined, else empty
uint32_t stateChanges();  // bumps on every state change, for screens to redraw

bool hasPassword();
void clearPassword();  // the device's recovery: page open again

}  // namespace portal
