#pragma once
#include <Arduino.h>

// Spotify login: Authorization Code with PKCE, no client secret on the device (plan §5.2).
//
// Loopback copy-paste flow: Spotify only accepts HTTPS redirect URIs, or plain HTTP to a
// loopback IP literal ("localhost" is refused). So the app is registered with
// REDIRECT_URI below; after login the browser is sent to 127.0.0.1, which fails to load --
// expected -- and the owner pastes that address into the setup page. The device takes the
// code from it and exchanges it for tokens.
//
// Network work runs in its own FreeRTOS task, so a TLS handshake never stalls touch or the
// web page. One TLS client, used for one host at a time (plan §4).
//
// Refresh tokens last 6 months from the login, and refreshing does not extend that
// (Spotify docs, 2026). When the token endpoint says invalid_grant the device drops the
// token and asks for a new login.
namespace spotify {

constexpr const char* REDIRECT_URI = "http://127.0.0.1:8888/callback";
constexpr const char* SCOPES =
    "user-read-playback-state user-modify-playback-state user-read-currently-playing "
    "user-library-read user-library-modify playlist-read-private playlist-read-collaborative";

enum class AuthState : uint8_t {
  NoClientId,  // nothing configured yet
  LoggedOut,   // client ID set, no refresh token
  LoggingIn,   // exchanging a pasted code
  Authorized,  // refresh token held (access token may be refreshing)
  Expired,     // refresh token rejected: log in again
};

void begin();  // loads the client ID and refresh token, starts the worker task

String clientId();
void setClientId(const String& id);  // a different ID logs out

// New PKCE verifier and state; returns the authorize URL to open in a browser.
String startLogin();
// The address pasted from the browser (or just the code). Queues the token exchange.
// False (with a reason) if it carries no code, an error, or the wrong state.
bool submitRedirect(const String& pasted, String& reason);
void logout();

AuthState state();
const char* stateText();
String lastError();       // plain language, empty when fine
int lastApiStatus();      // HTTP status of the last API call (0 = none yet)
uint32_t changes();       // bumps on every state/error change, for screens to redraw

// True if the current login granted this scope. Spotify grants scopes only at login, so after
// a firmware update adds one (playlists), the owner must log in once more.
bool hasScope(const char* scope);

// Test hook (Stage 2 acceptance): replaces the access token with a bad one, so the next
// call gets a real 401 and must recover by refreshing.
void testExpiry();

}  // namespace spotify
