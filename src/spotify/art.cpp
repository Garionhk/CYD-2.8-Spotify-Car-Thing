#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <esp_task_wdt.h>
#include "art.h"
#include "api.h"
#include "../display/display.h"
#include "../util/heap.h"
#include "../util/log.h"

namespace art {

static const char* DL_PATH = "/art/dl.jpg";    // being downloaded
static const char* CUR_PATH = "/art/cur.jpg";  // the current cover, swapped in by rename
static const char* URL_PATH = "/art/cur.url";
static const char* NEW_URL_PATH = "/art/new.url";
static const size_t MAX_JPEG = 200 * 1024;

// Guards s_readyUrl and cur.jpg -- held only for the swap and while the UI draws, never during a
// download, so a redraw never waits on the network.
static SemaphoreHandle_t s_mutex = xSemaphoreCreateMutex();
static String s_readyUrl;
static volatile uint32_t s_version = 0;
static bool s_loaded = false;  // read URL_PATH once at start
static String s_failedUrl;     // do not retry a broken image every poll
static uint32_t s_failedAt = 0;

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

void begin() {
  if (s_loaded) return;
  s_loaded = true;
  LittleFS.mkdir("/art");
  LittleFS.remove("/art/cur.raw");  // left over from the first Stage 5 build
  LittleFS.remove("/art/new.raw");
  LittleFS.remove(DL_PATH);         // a download cut short by a power cut
  LittleFS.remove(NEW_URL_PATH);
  fs::File f = LittleFS.open(URL_PATH, "r");
  if (f && LittleFS.exists(CUR_PATH)) {
    Lock l;
    s_readyUrl = f.readString();
  }
  if (f) f.close();
}

String readyUrl() {
  Lock l;
  return s_readyUrl;
}
uint32_t version() { return s_version; }

// ---------------------------------------------------------------------------
// Download (Spotify task)
// ---------------------------------------------------------------------------
// Plain HTTP to the image CDN on its own client, kept alive between covers: no TLS handshake,
// no new TCP connection or DNS lookup per image, and the API's TLS keep-alive is left alone.
static WiFiClient s_plain;
static HTTPClient s_http;

// The body straight to the file in 4 KB writes (writeToStream() writes ~1.4 KB at a time,
// and small LittleFS writes are what made a 30 KB cover take ~0.9 s).
static int saveBody(HTTPClient& http, fs::File& f, int size) {
  if (size <= 0) return http.writeToStream(&f);  // chunked or unknown length: let it decode
  WiFiClient* in = http.getStreamPtr();
  static uint8_t buf[4096];
  int total = 0;
  uint32_t lastData = millis();
  while (total < size && in && (in->connected() || in->available())) {
    const int avail = in->available();
    if (avail <= 0) {
      if (millis() - lastData > 5000) break;  // stalled
      delay(2);
      continue;
    }
    const int n = in->read(buf, min<int>(min(avail, (int)sizeof(buf)), size - total));
    if (n <= 0) break;
    if (f.write(buf, n) != (size_t)n) return -1;
    total += n;
    lastData = millis();
    esp_task_wdt_reset();
  }
  return total;
}

static bool download(const String& url) {
  String httpUrl = url;
  if (httpUrl.startsWith("https://")) httpUrl = "http://" + httpUrl.substring(8);

  for (int attempt = 0; attempt < 2; attempt++) {
    const bool secure = attempt == 1;  // HTTPS fallback, if the CDN ever refuses plain HTTP
    HTTPClient once;
    HTTPClient& http = secure ? once : s_http;
    http.setTimeout(8000);
    http.setReuse(!secure);
    if (secure) spotify::stopTls();  // one TLS session at a time
    const bool ok = secure ? http.begin(spotify::tlsClient(), url) : http.begin(s_plain, httpUrl);
    if (!ok) continue;
    const uint32_t t0 = millis();
    const int status = http.GET();
    const uint32_t tHeaders = millis() - t0;
    if (status != 200) {
      LOGF("art: %s GET -> %d", secure ? "HTTPS" : "HTTP", status);
      http.end();
      if (status < 0) s_plain.stop();  // a dead keep-alive: start the next one fresh
      if (secure) spotify::stopTls();
      continue;
    }
    const int size = http.getSize();
    if (size > (int)MAX_JPEG) {
      LOGF("art: image too large (%d bytes)", size);
      http.end();
      return false;
    }
    fs::File f = LittleFS.open(DL_PATH, "w");
    if (!f) {
      http.end();
      return false;
    }
    const uint32_t t1 = millis();
    const int written = saveBody(http, f, size);
    f.close();
    http.end();
    if (secure) spotify::stopTls();  // the next API call opens its own session again
    LOGF("art: %s %d bytes: headers %lu ms, body %lu ms", secure ? "HTTPS" : "HTTP", written,
         (unsigned long)tHeaders, (unsigned long)(millis() - t1));
    if (written > 0 && (size < 0 || written == size)) return true;
    s_plain.stop();
  }
  return false;
}

// A JPEG TJpgDec can decode (baseline; it cannot do progressive).
static bool readable(const char* path) {
  uint16_t w = 0, h = 0;
  return TJpgDec.getFsJpgSize(&w, &h, path, LittleFS) == JDR_OK && w && h;
}

void tick(const String& url, const String& fallbackUrl) {
  begin();
  if (url.isEmpty()) return;
  {
    Lock l;
    if (url == s_readyUrl) return;
  }
  // A broken image is retried after a minute, not on every poll.
  if (url == s_failedUrl && millis() - s_failedAt < 60000) return;

  const uint32_t t0 = millis();
  String got = url;
  bool ok = download(url) && readable(DL_PATH);
  if (!ok && fallbackUrl.length() && fallbackUrl != url) {
    LOGF("art: trying the small image instead");
    got = fallbackUrl;
    ok = download(fallbackUrl) && readable(DL_PATH);
  }
  if (ok) {
    // Swap the new cover in so that a power cut at any point leaves either the old pair or no
    // cover -- never a new picture under the old URL: URL file first, old pair out, renames.
    fs::File f = LittleFS.open(NEW_URL_PATH, "w");
    ok = f && f.print(got) == got.length();
    if (f) f.close();
    Lock l;
    if (ok) {
      LittleFS.remove(URL_PATH);  // from here until the last rename, there is no cover at all
      LittleFS.remove(CUR_PATH);
      ok = LittleFS.rename(DL_PATH, CUR_PATH) && LittleFS.rename(NEW_URL_PATH, URL_PATH);
    }
    s_readyUrl = ok ? got : String();
  }
  LittleFS.remove(DL_PATH);
  if (ok) {
    s_version++;
    LOGF("art: ready in %lu ms", (unsigned long)(millis() - t0));
  } else {
    LOGF("art: no usable image");
    s_failedUrl = url;
    s_failedAt = millis();
  }
}

// ---------------------------------------------------------------------------
// Decode (UI task)
// ---------------------------------------------------------------------------
// ~0.2-0.4 s for the 300 px cover -- once per new cover, and again only when the screen is
// redrawn. Decoding to a file of pixels first cost ~0.75 s of flash writes per track instead.
bool decodedSize(int16_t maxW, int16_t maxH, int16_t& w, int16_t& h) {
  Lock l;
  w = h = 0;
  if (s_readyUrl.isEmpty()) return false;
  uint16_t jw = 0, jh = 0;
  if (TJpgDec.getFsJpgSize(&jw, &jh, CUR_PATH, LittleFS) != JDR_OK || !jw || !jh) return false;
  uint8_t scale = 1;
  while (scale < 8 && (jw / scale > maxW || jh / scale > maxH)) scale *= 2;
  w = jw / scale;
  h = jh / scale;
  return true;
}

bool decode(int16_t cx, int16_t cy, int16_t maxW, int16_t maxH, BlockFn fn) {
  Lock l;
  if (s_readyUrl.isEmpty()) return false;
  uint16_t jw = 0, jh = 0;
  if (TJpgDec.getFsJpgSize(&jw, &jh, CUR_PATH, LittleFS) != JDR_OK || !jw || !jh) return false;
  uint8_t scale = 1;
  while (scale < 8 && (jw / scale > maxW || jh / scale > maxH)) scale *= 2;
  const int16_t aw = jw / scale, ah = jh / scale;
  TJpgDec.setJpgScale(scale);
  TJpgDec.setSwapBytes(true);  // byte-swapped: what pushImage() sends as-is
  TJpgDec.setCallback(fn);
  const uint32_t t0 = millis();
  const JRESULT r = TJpgDec.drawFsJpg(cx - aw / 2, cy - ah / 2, CUR_PATH, LittleFS);
  LOGF("art: decoded %ux%u at 1/%u in %lu ms", jw, jh, scale, (unsigned long)(millis() - t0));
  return r == JDR_OK;
}

}  // namespace art
