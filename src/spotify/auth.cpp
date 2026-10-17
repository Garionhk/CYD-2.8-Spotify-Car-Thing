#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <mbedtls/md.h>
#include <esp_task_wdt.h>
#include "auth.h"
#include "api.h"
#include "body_stream.h"
#include "art.h"
#include "control.h"
#include "player.h"
#include "playlists.h"
#include "devices.h"
#include "../util/heap.h"
#include "../util/log.h"

// Root CAs (tools/gen_ca_bundle.py), embedded by board_build.embed_files.
extern const uint8_t ca_bundle_start[] asm("_binary_data_x509_crt_bundle_bin_start");

namespace spotify {

static const char* TOKEN_URL = "https://accounts.spotify.com/api/token";
static const char* API_BASE = "https://api.spotify.com/v1";
static const uint32_t REFRESH_MARGIN_MS = 5 * 60 * 1000;  // refresh this long before expiry
static const uint32_t BACKOFF_MIN_MS = 5000;
static const uint32_t BACKOFF_MAX_MS = 5 * 60 * 1000;

enum Cmd : uint8_t { CMD_EXCHANGE = 1, CMD_WAKE = 2 };

// Created at static init, so the accessors are safe even before begin().
static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();  // guards every String below
static QueueHandle_t s_queue;
static Preferences s_prefs;

static String s_clientId, s_refresh, s_access;
static String s_verifier, s_loginState, s_pendingCode;
static String s_error;
static String s_scope;  // granted scopes, from the last token response
static volatile AuthState s_state = AuthState::NoClientId;
static volatile uint32_t s_accessExpiresAt = 0;
static volatile int s_apiStatus = 0;
static volatile uint32_t s_changes = 0;
static uint32_t s_nextTryAt = 0, s_backoff = BACKOFF_MIN_MS;

static WiFiClientSecure s_tls;  // the one TLS client
static HTTPClient s_api;        // kept across calls for keep-alive to api.spotify.com

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

static void setState(AuthState st) {
  if (s_state != st) {
    s_state = st;
    s_changes++;
  }
}

static void setError(const String& e) {
  Lock l;
  if (s_error != e) {
    s_error = e;
    s_changes++;
  }
}

// ---------------------------------------------------------------------------
// Persistence (own namespace; written only when the value changes)
// ---------------------------------------------------------------------------
static void storeString(const char* key, const String& v) {
  Lock l;  // called from the web handler and the worker task
  s_prefs.begin("spotify", false);
  if (s_prefs.getString(key, "") != v) {
    if (v.isEmpty()) s_prefs.remove(key);
    else s_prefs.putString(key, v);
  }
  s_prefs.end();
}

// ---------------------------------------------------------------------------
// PKCE helpers
// ---------------------------------------------------------------------------
static String randomString(size_t n) {
  // RFC 7636 unreserved characters. esp_random() is a true RNG while the radio is on.
  static const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  String s;
  s.reserve(n);
  for (size_t i = 0; i < n; i++) s += kChars[esp_random() % (sizeof(kChars) - 1)];
  return s;
}

static String base64url(const uint8_t* data, size_t len) {
  static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  String out;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = data[i] << 16;
    if (i + 1 < len) v |= data[i + 1] << 8;
    if (i + 2 < len) v |= data[i + 2];
    out += kB64[(v >> 18) & 63];
    out += kB64[(v >> 12) & 63];
    if (i + 1 < len) out += kB64[(v >> 6) & 63];
    if (i + 2 < len) out += kB64[v & 63];
  }
  return out;  // no '=' padding
}

static String challengeFor(const String& verifier) {
  uint8_t hash[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t*)verifier.c_str(), verifier.length(),
             hash);
  return base64url(hash, sizeof(hash));
}

static String urlEncode(const String& s) {
  String o;
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += c;
    } else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", (uint8_t)c);
      o += b;
    }
  }
  return o;
}

static String urlDecode(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+') {
      o += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      o += (char)strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16);
      i += 2;
    } else {
      o += c;
    }
  }
  return o;
}

// The value of name=... in a query string (anywhere after '?' or '#').
static String queryParam(const String& url, const char* name) {
  const String key = String(name) + "=";
  int from = 0;
  while (true) {
    const int at = url.indexOf(key, from);
    if (at < 0) return String();
    if (at == 0 || url[at - 1] == '?' || url[at - 1] == '&' || url[at - 1] == '#') {
      int end = url.indexOf('&', at);
      if (end < 0) end = url.length();
      return urlDecode(url.substring(at + key.length(), end));
    }
    from = at + 1;
  }
}

// ---------------------------------------------------------------------------
// HTTP (worker task only)
// ---------------------------------------------------------------------------
static uint32_t s_retryAfterMs = 0;

static int postToken(const String& body, JsonDocument& doc) {
  // Never two TLS sessions at once: drop the API keep-alive before talking to accounts.
  s_tls.stop();
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(10000);
  const char* hdrs[] = {"Retry-After"};
  http.collectHeaders(hdrs, 1);
  if (!http.begin(s_tls, TOKEN_URL)) return -1;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const int code = http.POST(body);
  s_retryAfterMs = http.header("Retry-After").toInt() * 1000UL;
  if (code > 0) {
    const String resp = http.getString();  // small (< 1 KB): tokens + scope
    JsonDocument filter;
    filter["access_token"] = true;
    filter["expires_in"] = true;
    filter["refresh_token"] = true;
    filter["error"] = true;
    filter["error_description"] = true;
    filter["scope"] = true;
    deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  }
  http.end();
  s_tls.stop();  // the token endpoint is used only every ~hour: do not hold it open
  logHeap("tls-token");
  return code;
}

static void backoff(const char* why) {
  const uint32_t wait = max(s_backoff, s_retryAfterMs);
  s_nextTryAt = millis() + wait;
  LOGF("spotify: %s -- retrying in %lu s", why, (unsigned long)(wait / 1000));
  s_backoff = min(s_backoff * 2, BACKOFF_MAX_MS);
}

// Takes the tokens from a 200 response. Returns false if there was no access token.
static bool takeTokens(JsonDocument& doc) {
  const char* access = doc["access_token"];
  if (!access) return false;
  const uint32_t expiresIn = doc["expires_in"] | 3600;
  if (doc["scope"].is<const char*>()) {
    Lock l;
    if (s_scope != doc["scope"].as<const char*>()) {
      s_scope = doc["scope"].as<const char*>();
      s_changes++;
    }
  }
  String newRefresh = doc["refresh_token"] | "";
  {
    Lock l;
    s_access = access;
    // A refresh response may omit the refresh token: keep using the one we have.
    if (newRefresh.length() && newRefresh != s_refresh) {
      s_refresh = newRefresh;
    } else {
      newRefresh = "";
    }
  }
  s_accessExpiresAt = millis() + expiresIn * 1000UL;
  if (newRefresh.length()) storeString("refresh", newRefresh);
  s_backoff = BACKOFF_MIN_MS;
  return true;
}

static void dropLogin(const String& why, AuthState next) {
  {
    Lock l;
    s_refresh = "";
    s_access = "";
  }
  s_accessExpiresAt = 0;
  storeString("refresh", "");
  setError(why);
  setState(next);
}

static void doExchange() {
  String code, verifier, cid;
  {
    Lock l;
    code = s_pendingCode;
    verifier = s_verifier;
    cid = s_clientId;
    s_pendingCode = "";
  }
  LOGF("spotify: exchanging the login code");
  const String body = "grant_type=authorization_code&code=" + urlEncode(code) +
                      "&redirect_uri=" + urlEncode(REDIRECT_URI) + "&client_id=" + urlEncode(cid) +
                      "&code_verifier=" + urlEncode(verifier);
  JsonDocument doc;
  const int status = postToken(body, doc);
  if (status == 200 && takeTokens(doc)) {
    {
      Lock l;
      s_verifier = "";  // one use only
    }
    setError("");
    setState(AuthState::Authorized);
    LOGF("spotify: logged in (access token valid %lu s)", (unsigned long)((s_accessExpiresAt - millis()) / 1000));
    return;
  }
  const String err = doc["error"] | "";
  const String desc = doc["error_description"] | "";
  LOGF("spotify: login failed: HTTP %d %s %s", status, err.c_str(), desc.c_str());
  if (status <= 0) setError("Could not reach Spotify. Check the Wi-Fi has internet, then paste the address again.");
  else if (err == "invalid_client") setError("Spotify does not know this Client ID. Copy it again from the dashboard.");
  else if (desc.indexOf("redirect") >= 0)
    setError(String("The Redirect URI in your Spotify app must be exactly ") + REDIRECT_URI);
  else if (err == "invalid_grant")
    setError("That login was already used or has expired. Tap Log in with Spotify again.");
  else setError("Spotify refused the login (" + err + "). Try again.");
  setState(AuthState::LoggedOut);
}

static bool doRefresh() {
  String refresh, cid;
  {
    Lock l;
    refresh = s_refresh;
    cid = s_clientId;
  }
  const String body = "grant_type=refresh_token&refresh_token=" + urlEncode(refresh) + "&client_id=" + urlEncode(cid);
  JsonDocument doc;
  const int status = postToken(body, doc);
  if (status == 200 && takeTokens(doc)) {
    setError("");
    LOGF("spotify: access token refreshed (valid %lu s)", (unsigned long)((s_accessExpiresAt - millis()) / 1000));
    return true;
  }
  const String err = doc["error"] | "";
  LOGF("spotify: refresh failed: HTTP %d %s", status, err.c_str());
  if (err == "invalid_grant") {
    // Revoked, or past its 6 months. Only a new login helps.
    dropLogin("Login expired. Log in again at cyd-spotify.local", AuthState::Expired);
  } else if (err == "invalid_client") {
    dropLogin("Spotify no longer accepts this Client ID. Check the app on the dashboard.", AuthState::Expired);
  } else if (status == 429) {
    setError("Spotify rate limit. Retrying shortly.");
    backoff("rate limited");
  } else {
    setError("Could not reach Spotify. Retrying.");
    backoff(status <= 0 ? "network error" : "server error");
  }
  return false;
}

bool hasScope(const char* scope) {
  Lock l;
  return (" " + s_scope + " ").indexOf(" " + String(scope) + " ") >= 0;
}

int apiRequest(const char* method, const String& path, JsonDocument* doc, const JsonDocument* filter,
               ApiResult& r, const String* jsonBody) {
  String token;
  {
    Lock l;
    token = s_access;
  }
  r = ApiResult();
  const bool isGet = strcmp(method, "GET") == 0;
  s_api.setReuse(true);  // keep-alive: one handshake, then plain requests
  s_api.setTimeout(10000);
  const char* hdrs[] = {"Transfer-Encoding", "Retry-After"};
  s_api.collectHeaders(hdrs, 2);
  if (!s_api.begin(s_tls, String(API_BASE) + path)) return API_NETWORK_ERROR;
  s_api.addHeader("Authorization", "Bearer " + token);
  const bool hasBody = jsonBody && jsonBody->length();
  // HTTPClient sends no Content-Length for an empty body; Spotify answers 411 without one.
  if (!isGet && !hasBody) s_api.addHeader("Content-Length", "0");
  if (hasBody) s_api.addHeader("Content-Type", "application/json");
  const uint32_t t0 = millis();
  const int status = isGet ? s_api.GET()
                     : hasBody ? s_api.sendRequest(method, (uint8_t*)jsonBody->c_str(), jsonBody->length())
                               : s_api.sendRequest(method, (uint8_t*)nullptr, 0);
  const bool firstOk = isGet && s_apiStatus != 200 && s_apiStatus != 204;
  if (isGet) {
    if (status != s_apiStatus) s_changes++;
    s_apiStatus = status;
  }

  if (status > 0) {
    // A 204 has no body and no Content-Length. Reading one anyway blocks on the keep-alive
    // connection until Spotify closes it (minutes), so read only when a body is announced.
    const bool chunked = s_api.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    const int size = s_api.getSize();
    if (status != 204 && (chunked || size > 0)) {
      BodyStream in(s_api.getStream(), chunked, size);
      in.setTimeout(5000);
      if (status == 200 && doc) {
        const DeserializationError err = filter ? deserializeJson(*doc, in, DeserializationOption::Filter(*filter))
                                                : deserializeJson(*doc, in);
        if (err) LOGF("spotify: %s %s -- JSON %s", method, path.c_str(), err.c_str());
      } else if (status >= 400) {
        // {"error":{"status":404,"message":"...","reason":"NO_ACTIVE_DEVICE"}}
        JsonDocument f, e;
        f["error"]["message"] = true;
        f["error"]["reason"] = true;
        if (!deserializeJson(e, in, DeserializationOption::Filter(f))) {
          r.reason = e["error"]["reason"] | "";
          r.message = e["error"]["message"] | "";
        }
      }
      in.drain();  // the connection is reused: leave nothing unread
    }
    r.retryAfterMs = s_api.header("Retry-After").toInt() * 1000UL;
  }
  s_api.end();
  const uint32_t ms = millis() - t0;

  if (status >= 200 && status < 300) {
    if (isGet) setError("");
    if (firstOk) {
      LOGF("spotify: API OK (GET %s -> %d in %lu ms)", path.c_str(), status, (unsigned long)ms);
      logHeap("tls-api");
    }
  } else if (status == 401) {
    LOGF("spotify: API said 401 -- refreshing the token");
    s_accessExpiresAt = millis();  // refresh on the next pass
  } else if (status == 403 && (r.reason == "PREMIUM_REQUIRED" || path.startsWith("/me/player?"))) {
    // Account-level: the playback state itself is refused. A 403 on one command (a device
    // that refuses volume, say) is that command's business, reported by the caller.
    setError("Spotify refused access. The app owner needs Premium, and your account must be added "
             "under User Management in the Spotify app.");
    LOGF("spotify: API 403 %s: %s", r.reason.c_str(), r.message.c_str());
  } else if (status == 429) {
    setError("Spotify rate limit. Retrying shortly.");
  } else if (status < 0) {
    LOGF("spotify: %s %s -- network error %d", method, path.c_str(), status);
    s_tls.stop();  // start the next call on a fresh connection
  } else {
    LOGF("spotify: %s %s -> HTTP %d %s %s", method, path.c_str(), status, r.reason.c_str(), r.message.c_str());
  }
  return status;
}

int apiGet(const String& path, JsonDocument& doc, const JsonDocument& filter, uint32_t& retryAfterMs) {
  ApiResult r;
  const int status = apiRequest("GET", path, &doc, &filter, r);
  retryAfterMs = r.retryAfterMs;
  return status;
}

void stopTls() { s_tls.stop(); }
WiFiClientSecure& tlsClient() { return s_tls; }

void wake() {
  const uint8_t cmd = CMD_WAKE;
  if (s_queue) xQueueSend(s_queue, &cmd, 0);
}

static void worker(void*) {
  esp_task_wdt_add(nullptr);  // the watchdog watches this task too (main.cpp sets 30 s)
  for (;;) {
    esp_task_wdt_reset();
    uint8_t cmd = 0;
    // Short wait: the player asks for polls ~600 ms after a control action.
    xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(200));
    if (WiFi.status() != WL_CONNECTED) continue;
    if (cmd == CMD_EXCHANGE) {
      doExchange();
      continue;
    }
    if (s_state != AuthState::Authorized) continue;
    const uint32_t now = millis();
    if (s_nextTryAt && (int32_t)(now - s_nextTryAt) < 0) continue;
    s_nextTryAt = 0;
    bool haveAccess;
    {
      Lock l;
      haveAccess = s_access.length() > 0;
    }
    if (!haveAccess || (int32_t)(s_accessExpiresAt - now) < (int32_t)REFRESH_MARGIN_MS) {
      if (!doRefresh()) continue;
    }
    control::tick();  // taps first: they are what the person is waiting for
    esp_task_wdt_reset();
    playlists::tick();
    devices::tick();
    esp_task_wdt_reset();
    player::tick();
    esp_task_wdt_reset();
    // Art last: a download must not hold up a tap or the poll that shows the new title.
    const player::Snapshot snap = player::snapshot();
    if (snap.hasItem) art::tick(snap.artUrl, snap.artUrlSmall);
  }
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void begin() {
  s_queue = xQueueCreate(4, sizeof(uint8_t));
  s_prefs.begin("spotify", true);
  s_clientId = s_prefs.getString("clientId", "");
  s_refresh = s_prefs.getString("refresh", "");
  s_prefs.end();
  if (s_clientId.isEmpty()) s_state = AuthState::NoClientId;
  else if (s_refresh.isEmpty()) s_state = AuthState::LoggedOut;
  else s_state = AuthState::Authorized;  // refreshes as soon as Wi-Fi is up
  LOGF("spotify: client ID %s, %s", s_clientId.length() ? "set" : "not set", stateText());
  // RFC 7636 appendix B test vector: a wrong challenge would only show up as a vague login error.
  const bool pkceOk = challengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
                      "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
  LOGF("spotify: PKCE self-test %s", pkceOk ? "OK" : "FAILED");
  if (!pkceOk) LOGF("spotify: PKCE got \"%s\"", challengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk").c_str());

  s_tls.setCACertBundle(ca_bundle_start);
  // TLS handshakes need a deep stack; 12 KB measured comfortable for HTTPClient + mbedTLS.
  xTaskCreatePinnedToCore(worker, "spotify", 12288, nullptr, 1, nullptr, 0);
}

String clientId() {
  Lock l;
  return s_clientId;
}

void setClientId(const String& idIn) {
  String id = idIn;
  id.trim();
  {
    Lock l;
    if (id == s_clientId) return;
    s_clientId = id;
  }
  storeString("clientId", id);
  dropLogin("", id.isEmpty() ? AuthState::NoClientId : AuthState::LoggedOut);
  LOGF("spotify: client ID %s", id.length() ? "changed" : "cleared");
}

String startLogin() {
  String cid, challenge, st;
  {
    Lock l;
    s_verifier = randomString(64);
    s_loginState = randomString(16);
    cid = s_clientId;
    challenge = challengeFor(s_verifier);
    st = s_loginState;
  }
  LOGF("spotify: login started");
  return "https://accounts.spotify.com/authorize?response_type=code&client_id=" + urlEncode(cid) +
         "&scope=" + urlEncode(SCOPES) + "&redirect_uri=" + urlEncode(REDIRECT_URI) +
         "&code_challenge_method=S256&code_challenge=" + challenge + "&state=" + st;
}

bool submitRedirect(const String& pastedIn, String& reason) {
  String pasted = pastedIn;
  pasted.trim();
  const bool looksLikeUrl = pasted.indexOf('=') >= 0;
  const String error = looksLikeUrl ? queryParam(pasted, "error") : String();
  if (error.length()) {
    reason = error == "access_denied" ? "Login was cancelled on the Spotify page." : "Spotify said: " + error;
    return false;
  }
  const String code = looksLikeUrl ? queryParam(pasted, "code") : pasted;
  if (code.length() < 20) {
    reason = "That does not look like the address from the browser. Copy the whole address after logging in.";
    return false;
  }
  {
    Lock l;
    if (s_verifier.isEmpty()) {
      reason = "Tap Log in with Spotify first (the device restarted since, or the login was already used).";
      return false;
    }
    const String st = looksLikeUrl ? queryParam(pasted, "state") : String();
    if (st.length() && st != s_loginState) {
      reason = "That address is from an older login attempt. Tap Log in with Spotify again.";
      return false;
    }
    s_pendingCode = code;
  }
  setError("");
  setState(AuthState::LoggingIn);
  const uint8_t cmd = CMD_EXCHANGE;
  xQueueSend(s_queue, &cmd, 0);
  return true;
}

void logout() {
  dropLogin("", clientId().isEmpty() ? AuthState::NoClientId : AuthState::LoggedOut);
  LOGF("spotify: logged out");
}

AuthState state() { return s_state; }

const char* stateText() {
  switch (s_state) {
    case AuthState::NoClientId: return "not set up";
    case AuthState::LoggedOut: return "not logged in";
    case AuthState::LoggingIn: return "logging in...";
    case AuthState::Authorized: return "logged in";
    case AuthState::Expired: return "login expired";
  }
  return "";
}

String lastError() {
  Lock l;
  return s_error;
}

int lastApiStatus() { return s_apiStatus; }

void testExpiry() {
  {
    Lock l;
    if (s_access.isEmpty()) return;
    s_access = "expired-on-purpose";
  }
  LOGF("spotify: test -- access token replaced with a bad one");
}
uint32_t changes() { return s_changes; }

}  // namespace spotify
