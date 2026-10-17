#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// Internal to src/spotify: Web API calls, made only from the Spotify task.
namespace spotify {

constexpr int API_NETWORK_ERROR = -1;  // also any HTTPClient error code (< 0)

// GET https://api.spotify.com/v1<path>, parsed straight off the stream through filter into doc
// (the body is never held whole). 204 leaves doc empty. On 401 the access token is marked
// for refresh; the caller simply tries again next round. retryAfterMs is set on 429.
int apiGet(const String& path, JsonDocument& doc, const JsonDocument& filter, uint32_t& retryAfterMs);

struct ApiResult {
  uint32_t retryAfterMs = 0;
  String reason;   // Spotify's error.reason, e.g. NO_ACTIVE_DEVICE, PREMIUM_REQUIRED
  String message;  // error.message
};

// Any method. Without jsonBody, PUT/POST/DELETE send Content-Length: 0. doc/filter only for a
// 200; filter == nullptr parses the whole body (small replies such as [true]).
int apiRequest(const char* method, const String& path, JsonDocument* doc, const JsonDocument* filter, ApiResult& r,
               const String* jsonBody = nullptr);

// The one TLS client (plan §4: never two TLS sessions at once). stopTls() closes the API
// keep-alive, for a call to another host (art.cpp's HTTPS fallback).
void stopTls();
WiFiClientSecure& tlsClient();

// Wake the Spotify task now (a command was queued) instead of at its next 200 ms tick.
void wake();

}  // namespace spotify
