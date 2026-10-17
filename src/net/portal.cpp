// WebServer.h first (as the MP3 uploader and Stream Deck): TFT_eSPI defines FS_NO_GLOBALS
// before including FS.h, which hides the plain `FS` name WebServer.h uses.
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "portal.h"
#include "../config.h"
#include "../display/display.h"
#include "../spotify/auth.h"
#include "../spotify/control.h"
#include "../storage/settings.h"
#include "../util/clock.h"
#include "../util/log.h"

namespace portal {

static const char* HOSTNAME = "cyd-spotify";
// Per network, then the next one. Joins took 0.1-0.3 s in every log; 15 s per absent network
// made a car start (home Wi-Fi tried first) take ~50 s.
static const uint32_t JOIN_TIMEOUT_MS = 8000;
static const uint32_t RESCAN_WAIT_MS = 3000;    // none in range: look again after this
static const uint32_t JOIN_SETTLE_MS = 3000;    // ignore failure statuses this long after begin()

static WebServer server(80);
static DNSServer* dns = nullptr;
static String s_apName;
static bool s_apUp = false;
static bool s_mdnsUp = false;
static uint32_t s_restartAt = 0;
static uint32_t s_changes = 0;

static WifiState s_state = WifiState::NoNetworks;
static uint32_t s_stateAt = 0;
static int s_slot = -1;                        // saved slot being joined / joined
static bool s_inRange[MAX_NETWORKS] = {};      // from the last scan
static bool s_tried[MAX_NETWORKS] = {};        // joins already attempted since that scan
static bool s_blind = false;                   // this round joins by name, unseen by the scan
static volatile uint8_t s_discReason = 0;      // ESP-IDF's reason for the last disconnect
static String s_problem;                       // e.g. "Wrong password for X" (until a join works)

// Login, as the WOL-NTFY settings page: HTTP Basic, user "admin", the password from settings.
// No password set = the page is open (and says so).
static const char* WEB_USER = "admin";
static const char* WEB_REALM = "CYD Spotify";
static const int FREE_TRIES = 5;  // wrong passwords before a lockout
static uint8_t s_failures = 0;
static uint32_t s_lockedUntil = 0;

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Spotify</title>
<style>
:root{color-scheme:light dark;--bg:#f4f5f7;--card:#fff;--ink:#1c1e21;--dim:#65676b;--line:#d8dadf;--acc:#1db954}
@media(prefers-color-scheme:dark){:root{--bg:#18191a;--card:#242526;--ink:#e4e6eb;--dim:#a8abaf;--line:#3a3b3c}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 12px}h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin:0 0 8px}
h3{font-size:14px;margin:12px 0 0}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
label{display:block;font-size:13px;color:var(--dim);margin:8px 0 4px}
input,select{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink);font:inherit}
.row{display:flex;gap:8px}.row>*{flex:1}
button{padding:10px 14px;border:0;border-radius:8px;background:var(--acc);color:#fff;font:inherit;font-weight:600;cursor:pointer}
button.ghost{background:transparent;color:var(--acc);border:1px solid var(--line)}
.status{font-size:13px;color:var(--dim)}.status b{color:var(--ink);font-weight:600}
.warn{background:#fff4e5;border-color:#ffb74d;color:#663c00}
@media(prefers-color-scheme:dark){.warn{background:#3a2a10;border-color:#8a5a14;color:#ffd699}}
.chk{display:flex;gap:8px;align-items:center;margin-top:10px;font-size:14px;color:var(--ink)}.chk input{width:auto}
input[type=range]{padding:0}
ol{padding-left:20px;margin:6px 0}li{margin:8px 0}code{font-size:13px;user-select:all}
a.btn{display:inline-block;padding:10px 14px;border-radius:8px;background:var(--acc);color:#fff;font-weight:600;text-decoration:none}
.err{color:#d93025}
</style></head><body><main>
<h1>CYD Spotify</h1>
<section class="status" id="st">%STATUS%</section>
%NOPASS%
%SPOTIFY%
<form method="post" action="/save">
<section><h2>Wi-Fi</h2>
<p class="status" style="margin:0">Tried in this order. Add your home Wi-Fi and your phone's hotspot for the car. Clear a name to remove it.</p>
<div class="row" style="margin-top:8px"><span></span>
<button type="button" class="ghost" style="flex:0 0 auto" onclick="scan(this)">Scan</button></div>
<datalist id="nets"></datalist>
<h3>Network 1</h3>
<div class="row"><div><label for="ssid0">Name</label><input id="ssid0" name="ssid0" value="%SSID0%" list="nets" autocomplete="off"></div>
<div><label for="pass0">Password</label><input id="pass0" name="pass0" type="password" placeholder="%PASSHINT0%" autocomplete="new-password"></div></div>
<h3>Network 2</h3>
<div class="row"><div><label for="ssid1">Name</label><input id="ssid1" name="ssid1" value="%SSID1%" list="nets" autocomplete="off"></div>
<div><label for="pass1">Password</label><input id="pass1" name="pass1" type="password" placeholder="%PASSHINT1%" autocomplete="new-password"></div></div>
<h3>Network 3</h3>
<div class="row"><div><label for="ssid2">Name</label><input id="ssid2" name="ssid2" value="%SSID2%" list="nets" autocomplete="off"></div>
<div><label for="pass2">Password</label><input id="pass2" name="pass2" type="password" placeholder="%PASSHINT2%" autocomplete="new-password"></div></div>
</section>
<section><h2>Display</h2>
<label for="bright">Brightness <span id="bv">%BRIGHT%</span>%</label>
<input id="bright" name="bright" type="range" min="10" max="100" step="5" value="%BRIGHT%">
<div class="row"><div><label for="panel">Panel controller</label>
<select id="panel" name="panel"><option value="7789"%P7789%>ST7789</option><option value="9341"%P9341%>ILI9341</option></select></div>
<div><label for="invert">Colours</label>
<select id="invert" name="invert"><option value="0"%INV0%>Normal</option><option value="1"%INV1%>Inverted</option></select></div>
<div><label for="rot">Orientation</label>
<select id="rot" name="rot"><option value="1"%ROT1%>Normal</option><option value="3"%ROT3%>Upside down</option></select></div></div>
<div class="row"><div><label for="scroff">Screen off when idle</label>
<select id="scroff" name="scroff"><option value="0"%OFF0%>Never</option><option value="1"%OFF1%>1 min</option><option value="5"%OFF5%>5 min</option><option value="15"%OFF15%>15 min</option><option value="30"%OFF30%>30 min</option></select></div>
<div><label for="tz">Time zone (idle clock)</label>
<select id="tz" name="tz">%TZOPTS%</select></div></div>
<div class="row"><div><label for="poll">Spotify updates</label>
<select id="poll" name="poll"><option value="0"%POLL0%>Normal (every 4 s)</option><option value="1"%POLL1%>Data saver (every 8 s)</option></select></div></div>
</section>
<section><h2>Setup page password</h2>
<p class="status" style="margin:0 0 4px">%PASSSTATE%</p>
<div class="row"><div><label for="wp1">New password</label>
<input id="wp1" name="wp1" type="password" autocomplete="new-password" placeholder="%WPHINT%"></div>
<div><label for="wp2">Again</label>
<input id="wp2" name="wp2" type="password" autocomplete="new-password"></div></div>
%WPCLEAR%
</section>
<button type="submit" style="width:100%">Save</button>
</form>
<p class="status">Hold the BOOT button for 5 s to open the setup hotspot again. &middot; <a href="/log">Recent log</a></p>
</main><script>
const b=document.getElementById('bright'),bv=document.getElementById('bv');let t;
b.oninput=()=>{bv.textContent=b.value;clearTimeout(t);t=setTimeout(()=>fetch('/bright?v='+b.value),120)};
document.getElementById('invert').onchange=e=>fetch('/invert?v='+e.target.value);
document.getElementById('scroff').onchange=e=>fetch('/screenoff?v='+e.target.value);
async function scan(btn){btn.disabled=true;btn.textContent='…';
try{const r=await fetch('/scan');const l=await r.json();const d=document.getElementById('nets');d.innerHTML='';
l.forEach(n=>{const o=document.createElement('option');o.value=n;d.appendChild(o)});
btn.textContent=l.length+' found'}catch(e){btn.textContent='Scan'}btn.disabled=false}
</script></body></html>)HTML";

static String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

static const char* stateText() {
  switch (s_state) {
    case WifiState::NoNetworks: return "no network saved";
    case WifiState::Scanning: return "looking for a saved network";
    case WifiState::Joining: return "joining";
    case WifiState::Waiting: return "no saved network in range, retrying";
    case WifiState::Up: return "connected";
  }
  return "";
}

static String statusHtml() {
  String s = "Wi-Fi: <b>";
  if (wifiUp()) s += esc(ssid()) + "</b> (" + WiFi.localIP().toString() + ", " + String(WiFi.RSSI()) + " dBm)";
  else if (s_state == WifiState::Joining) s += "joining " + esc(ssid()) + "</b>";
  else s += String(stateText()) + "</b>";
  if (s_apUp) s += " &middot; hotspot <b>" + esc(s_apName) + "</b>";
  if (s_mdnsUp && wifiUp()) s += "<br>Address: <b>" + mdnsUrl() + "</b>";
  s += "<br>Firmware: <b>" FW_VERSION "</b> &middot; memory <b>" + String(ESP.getFreeHeap() / 1024) +
       " KB</b> free, largest block " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB";
  return s;
}

static String spotifyHtml();

// The value for one %TOKEN% in PAGE.
static String pageValue(const String& key) {
  const Settings& s = settings::get();
  if (key == "STATUS") return statusHtml();
  if (key == "SPOTIFY") return spotifyHtml();
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (key == "SSID" + String(i)) return esc(s.ssid[i]);
    if (key == "PASSHINT" + String(i)) return s.pass[i].length() ? "(unchanged)" : "(none)";
  }
  if (key == "BRIGHT") return String(s.brightness);
  const bool pw = s.webPass.length();
  if (key == "NOPASS")
    return pw ? ""
              : "<section class=\"warn\"><b>No password set.</b> Anyone on this network "
                "(or on the setup hotspot) can open this page and change these settings. "
                "Set one below.</section>";
  if (key == "PASSSTATE")
    return pw ? "Set &ndash; this page asks for user <b>admin</b> and the password." : "None &ndash; the page is open.";
  if (key == "WPHINT") return pw ? "(unchanged)" : "(none)";
  if (key == "WPCLEAR")
    return pw ? "<label class=\"chk\"><input type=\"checkbox\" name=\"wpclear\" value=\"1\">"
                "Remove the password (the page will be open)</label>"
              : "";
  const char* sel = " selected";
  if (key == "P7789") return s.panel == PANEL_ST7789 ? sel : "";
  if (key == "P9341") return s.panel == PANEL_ILI9341 ? sel : "";
  if (key == "INV1") return s.invert ? sel : "";
  if (key == "INV0") return s.invert ? "" : sel;
  if (key == "ROT1") return s.rotation == 1 ? sel : "";
  if (key == "ROT3") return s.rotation == 3 ? sel : "";
  if (key == "POLL0") return s.pollPreset == 0 ? sel : "";
  if (key == "POLL1") return s.pollPreset == 1 ? sel : "";
  if (key.startsWith("OFF")) return key.substring(3).toInt() == s.screenOffMin ? sel : "";
  if (key == "TZOPTS") {
    String o;
    bool found = false;
    for (int i = 0; i < clockutil::ZONE_COUNT; i++) {
      const bool on = s.tz == clockutil::ZONES[i].tz;
      found |= on;
      o += "<option value=\"" + String(clockutil::ZONES[i].tz) + "\"" + (on ? sel : "") + ">" +
           clockutil::ZONES[i].label + "</option>";
    }
    if (!found) o += "<option value=\"" + esc(s.tz) + "\" selected>" + esc(s.tz) + "</option>";
    return o;
  }
  return String();
}

static bool tokenChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// Streamed in ~512-byte chunks straight from flash, filling in %TOKEN%s on the way, so the
// page never needs one big block of heap (Stream Deck: a 6 KB String sometimes came out empty).
static void sendPage() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  String chunk;
  chunk.reserve(600);
  const char* p = PAGE;
  for (char c = pgm_read_byte(p); c; c = pgm_read_byte(p)) {
    p++;
    if (c == '%') {
      // A token is %NAME% in capitals/digits; any other '%' (CSS "100%") is text.
      String key;
      const char* q = p;
      for (char k = pgm_read_byte(q); tokenChar(k) && key.length() < 12; k = pgm_read_byte(++q)) key += k;
      if (key.length() && pgm_read_byte(q) == '%') {
        chunk += pageValue(key);
        p = q + 1;
      } else {
        chunk += c;
      }
    } else {
      chunk += c;
    }
    if (chunk.length() >= 512) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  if (chunk.length()) server.sendContent(chunk);
  server.sendContent("");  // end of the chunked reply
}

static void sendMessagePage(const char* title, const char* body) {
  String html = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                  "<style>body{font:16px system-ui,sans-serif;max-width:480px;margin:40px auto;padding:0 16px}"
                  "@media(prefers-color-scheme:dark){body{background:#18191a;color:#e4e6eb}}</style><h2>");
  html += title;
  html += "</h2><p>";
  html += body;
  html += "</p><p><a href='/'>Back</a></p>";
  server.send(200, "text/html; charset=utf-8", html);
}

// The Spotify section: setup steps, login, status. Its forms sit outside the main form.
static String spotifyHtml() {
  const spotify::AuthState st = spotify::state();
  const String cid = spotify::clientId();
  const String err = spotify::lastError();
  const bool loggedIn = st == spotify::AuthState::Authorized;
  String h = "<section id=\"spotify\"><h2>Spotify</h2><p class=\"status\" style=\"margin:0\">Status: <b>";
  h += spotify::stateText();
  h += "</b>";
  if (loggedIn && spotify::lastApiStatus())
    h += " &middot; last API call: <b>HTTP " + String(spotify::lastApiStatus()) + "</b>";
  h += "</p>";
  if (err.length()) h += "<p class=\"err\">" + esc(err) + "</p>";
  if (loggedIn && !spotify::hasScope("playlist-read-private"))
    h += "<p class=\"warn\" style=\"padding:8px;border-radius:8px\"><b>Log in again to use playlists.</b> "
         "This firmware asks Spotify for one more permission (your playlists). Open Setup steps below and tap "
         "<b>Log in with Spotify</b> once more.</p>";
  if (s_apUp && !wifiUp()) {
    h += "<p class=\"status\">Spotify login needs internet. Save your Wi-Fi below first; after the restart, "
         "open <b>http://cyd-spotify.local</b> and come back here.</p></section>";
    return h;
  }
  if (loggedIn) {
    h += "<form method=\"post\" action=\"/spotify/logout\" style=\"margin-top:10px\">"
         "<button class=\"ghost\" type=\"submit\">Log out of Spotify</button></form>"
         "<details style=\"margin-top:10px\"><summary class=\"status\">Setup steps</summary>";
  }
  h += "<ol class=\"status\">"
       "<li>On <a href=\"https://developer.spotify.com/dashboard\" target=\"_blank\">developer.spotify.com/dashboard</a> "
       "tap <b>Create app</b> (any name). Redirect URI: <code>";
  h += spotify::REDIRECT_URI;
  h += "</code>. Tick <b>Web API</b>. The app owner needs Spotify Premium.</li>"
       "<li>Paste the app's <b>Client ID</b>:"
       "<form method=\"post\" action=\"/spotify/client\" class=\"row\" style=\"margin-top:4px\">"
       "<input name=\"clientid\" value=\"" + esc(cid) + "\" autocomplete=\"off\" placeholder=\"32 letters and digits\">"
       "<button type=\"submit\" style=\"flex:0 0 auto\">Save</button></form></li>";
  if (cid.length()) {
    h += "<li><a class=\"btn\" href=\"/spotify/login\" target=\"_blank\">Log in with Spotify</a> "
         "(opens a new tab)</li>"
         "<li>After you agree, that tab shows an error page at <b>127.0.0.1</b>. That is expected. Copy the "
         "<b>whole address</b> from its address bar and paste it here:"
         "<form method=\"post\" action=\"/spotify/code\" class=\"row\" style=\"margin-top:4px\">"
         "<input name=\"url\" autocomplete=\"off\" placeholder=\"http://127.0.0.1:8888/callback?code=...\">"
         "<button type=\"submit\" style=\"flex:0 0 auto\">Finish</button></form></li>";
  }
  h += "</ol>";
  if (loggedIn) h += "</details>";
  h += "</section>";
  return h;
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
static void redirectHome(const char* anchor = "") {
  server.sendHeader("Location", String("/") + anchor, true);
  server.send(303, "text/plain", "");
}

static void handleSpotifyClient() {
  spotify::setClientId(server.arg("clientid"));
  redirectHome("#spotify");
}

static void handleSpotifyLogin() {
  if (spotify::clientId().isEmpty()) {
    sendMessagePage("No Client ID", "Paste your Spotify app's Client ID on the setup page first.");
    return;
  }
  server.sendHeader("Location", spotify::startLogin(), true);
  server.send(302, "text/plain", "");
}

static void handleSpotifyCode() {
  String reason;
  if (!spotify::submitRedirect(server.arg("url"), reason)) {
    sendMessagePage("Not logged in", esc(reason).c_str());
    return;
  }
  // The exchange runs in the Spotify task; this page waits for it.
  server.send(200, "text/html; charset=utf-8", F(
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
      "<style>body{font:16px system-ui,sans-serif;max-width:480px;margin:40px auto;padding:0 16px}"
      "@media(prefers-color-scheme:dark){body{background:#18191a;color:#e4e6eb}}</style>"
      "<h2 id=h>Logging in&hellip;</h2><p id=p>Talking to Spotify.</p><p><a href='/#spotify'>Back</a></p>"
      "<script>async function poll(){try{const s=await (await fetch('/spotify/status')).json();"
      "if(s.state!='logging in...'){document.getElementById('h').textContent=s.state=='logged in'?'Logged in':'Not logged in';"
      "document.getElementById('p').textContent=s.error||(s.state=='logged in'?'The device is connected to your Spotify account.':'');return}}"
      "catch(e){}setTimeout(poll,700)}poll()</script>"));
}

static void handleSpotifyStatus() {
  String json = "{\"state\":\"" + String(spotify::stateText()) + "\",\"api\":" + String(spotify::lastApiStatus()) +
                ",\"error\":\"";
  String e = spotify::lastError();
  e.replace("\\", "\\\\");
  e.replace("\"", "\\\"");
  json += e + "\"}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void handleSpotifyLogout() {
  spotify::logout();
  redirectHome("#spotify");
}

static void handleSave() {
  Settings& s = settings::get();
  const Settings before = s;
  LOGF("web: settings form from %s", server.client().remoteIP().toString().c_str());

  // The page password first: a mistyped one rejects the whole form, so the owner is never
  // locked out by a typo they cannot see.
  const String wp1 = server.arg("wp1"), wp2 = server.arg("wp2");
  const bool clearPass = server.arg("wpclear") == "1";
  if (!clearPass && wp1 != wp2) {
    sendMessagePage("Not saved", "The two new passwords are different. Nothing was changed.");
    return;
  }
  if (!clearPass && wp1.length() && wp1.length() < 4) {
    sendMessagePage("Not saved", "The password needs at least 4 characters. Nothing was changed.");
    return;
  }
  bool passChanged = false;
  if (clearPass && s.webPass.length()) {
    s.webPass = "";
    passChanged = true;
  } else if (!clearPass && wp1.length()) {
    s.webPass = wp1;
    passChanged = true;
  }
  if (passChanged) LOGF("web: setup page password %s", s.webPass.length() ? "set" : "removed");

  for (int i = 0; i < MAX_NETWORKS; i++) {
    String ss = server.arg("ssid" + String(i));
    ss.trim();
    const String pw = server.arg("pass" + String(i));
    // An empty password box means "keep it" -- unless the network changed, when it means the
    // new network is open.
    if (ss != s.ssid[i] || pw.length()) s.pass[i] = pw;
    s.ssid[i] = ss;
  }
  s.brightness = server.arg("bright").toInt();
  s.panel = server.arg("panel").toInt();
  s.invert = server.arg("invert") == "1";
  s.rotation = server.arg("rot").toInt();
  s.pollPreset = server.arg("poll").toInt();
  s.screenOffMin = server.arg("scroff").toInt();
  if (server.hasArg("tz") && server.arg("tz").length() && server.arg("tz").length() < 48) s.tz = server.arg("tz");
  settings::save();  // also sanitises

  display::setBrightness(s.brightness);
  display::setInvert(s.invert);
  clockutil::setZone(s.tz);

  bool restart = s.panel != before.panel || s.rotation != before.rotation;
  for (int i = 0; i < MAX_NETWORKS; i++)
    restart |= s.ssid[i] != before.ssid[i] || s.pass[i] != before.pass[i];
  if (restart) {
    sendMessagePage("Saved",
                    "The device is restarting with the new settings. "
                    "If you changed its Wi-Fi, reconnect your phone to that network.");
    s_restartAt = millis() + 1500;
  } else if (passChanged && s.webPass.length()) {
    sendMessagePage("Saved",
                    "Settings saved. The page now asks for a login: user <b>admin</b> and your new password.");
  } else {
    sendMessagePage("Saved", "Settings saved.");
  }
}

static void handleBright() {
  Settings& s = settings::get();
  s.brightness = constrain((int)server.arg("v").toInt(), BRIGHTNESS_MIN, 100);
  display::setBrightness(s.brightness);
  settings::saveSoon();
  server.send(200, "text/plain", "ok");
}

static void handleInvert() {
  Settings& s = settings::get();
  s.invert = server.arg("v") == "1";
  display::setInvert(s.invert);
  settings::saveSoon();
  server.send(200, "text/plain", "ok");
}

static void handleScreenOff() {
  Settings& s = settings::get();
  s.screenOffMin = constrain((int)server.arg("v").toInt(), 0, 120);
  settings::saveSoon();
  LOGF("web: screen off when idle -> %u min", s.screenOffMin);
  server.send(200, "text/plain", "ok");
}

static void handleScan() {
  // The background scan may be running; wait for it rather than getting WIFI_SCAN_RUNNING.
  const uint32_t start = millis();
  while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && millis() - start < 6000) delay(50);
  esp_task_wdt_reset();
  const int n = WiFi.scanNetworks();
  String json = "[";
  int listed = 0;
  for (int i = 0; i < n; i++) {
    const String ss = WiFi.SSID(i);
    if (ss.isEmpty()) continue;
    // Strongest first is how the scan comes back; drop repeats of a name.
    bool seen = false;
    for (int j = 0; j < i; j++)
      if (WiFi.SSID(j) == ss) {
        seen = true;
        break;
      }
    if (seen) continue;
    String e = ss;
    e.replace("\\", "\\\\");
    e.replace("\"", "\\\"");
    json += (listed++ ? ",\"" : "\"") + e + "\"";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

static void handleLog() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", Log.recent());
}

// True when the request may proceed. Otherwise it has been answered: a login prompt, or
// "too many tries" while locked out.
static bool authorized() {
  const String& pw = settings::get().webPass;
  if (pw.isEmpty()) return true;
  const uint32_t now = millis();
  if (s_lockedUntil && (int32_t)(s_lockedUntil - now) > 0) {
    server.send(429, "text/plain",
                "Too many wrong passwords. Try again in " + String((s_lockedUntil - now + 999) / 1000) + " s.");
    return false;
  }
  if (server.authenticate(WEB_USER, pw.c_str())) {
    s_failures = 0;
    return true;
  }
  // A browser's first request carries no login: that is not a wrong guess.
  if (server.hasHeader("Authorization")) {
    if (s_failures < 255) s_failures++;
    const String ip = server.client().remoteIP().toString();
    if (s_failures >= FREE_TRIES) {
      // 5 s, then doubling with every further miss, up to 5 minutes.
      const uint32_t wait = min<uint32_t>(5000UL << min(s_failures - FREE_TRIES, 6), 300000UL);
      s_lockedUntil = now + wait;
      LOGF("web: wrong password from %s (%u in a row) -- locked for %lu s", ip.c_str(), s_failures,
           (unsigned long)(wait / 1000));
    } else {
      LOGF("web: wrong password from %s (%u in a row)", ip.c_str(), s_failures);
    }
  }
  server.requestAuthentication(BASIC_AUTH, WEB_REALM);
  return false;
}

static void handleNotFound() {
  // Captive portal: on the hotspot, every unknown URL (a phone's connectivity check) goes to
  // the setup page, which is what makes the phone show it.
  if (s_apUp) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "not found");
  }
}

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
// No modem sleep (Stream Deck measurement: with it, pings took 130-560 ms). WiFi.setSleep()
// before Wi-Fi has started does not stick, so this is applied after every start and every join.
static void noModemSleep() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

static void setState(WifiState st) {
  if (st == s_state) return;
  s_state = st;
  s_stateAt = millis();
  s_changes++;
}

static void startScan() {
  s_blind = false;
  WiFi.scanDelete();
  WiFi.scanNetworks(true /* async */);
  setState(WifiState::Scanning);
}

// The highest-priority saved network that the last scan saw and that has not been tried yet.
// In a blind round, any saved network not tried yet.
// The network that worked last time goes first (in the car that is the hotspot, at home the
// home Wi-Fi); then the saved order.
static int nextCandidate() {
  const Settings& s = settings::get();
  auto eligible = [&](int i) { return (s_inRange[i] || (s_blind && s.ssid[i].length())) && !s_tried[i]; };
  for (int i = 0; i < MAX_NETWORKS; i++)
    if (s.lastSsid.length() && s.ssid[i] == s.lastSsid && eligible(i)) return i;
  for (int i = 0; i < MAX_NETWORKS; i++)
    if (eligible(i)) return i;
  return -1;
}

static void joinSlot(int i) {
  const Settings& s = settings::get();
  s_slot = i;
  s_tried[i] = true;
  s_discReason = 0;
  WiFi.begin(s.ssid[i].c_str(), s.pass[i].c_str());
  noModemSleep();
  setState(WifiState::Joining);
  // Every attempt gets its own timer: setState() leaves it alone when the state does not
  // change (joining -> joining), and network 2 then inherited network 1's expired 15 s.
  s_stateAt = millis();
  s_changes++;
  LOGF("wifi: joining \"%s\" (network %d)", s.ssid[i].c_str(), i + 1);
}

static void afterScan(int n) {
  const Settings& s = settings::get();
  for (int i = 0; i < MAX_NETWORKS; i++) {
    s_inRange[i] = false;
    s_tried[i] = false;
    if (s.ssid[i].isEmpty()) continue;
    for (int j = 0; j < n; j++)
      if (WiFi.SSID(j) == s.ssid[i]) {
        s_inRange[i] = true;
        break;
      }
  }
  WiFi.scanDelete();
  int next = nextCandidate();
  if (next < 0 && !s_blind) {
    // Seen on this board: scans that come back empty while the router is right there. Hidden
    // networks and a phone hotspot that is still starting do not show up either. So when the
    // scan sees none of them, try each saved network by name before waiting.
    LOGF("wifi: scan saw no saved network (%d seen) -- trying them by name", n);
    s_blind = true;
    next = nextCandidate();
  }
  if (next >= 0) {
    joinSlot(next);
  } else {
    setState(WifiState::Waiting);
  }
}

static void stationTick() {
  const uint32_t now = millis();
  switch (s_state) {
    case WifiState::NoNetworks:
      break;
    case WifiState::Scanning: {
      const int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) break;
      if (n == WIFI_SCAN_FAILED) {  // also: results taken by the page's Scan button
        if (now - s_stateAt > 500) startScan();
        break;
      }
      afterScan(n);
      break;
    }
    case WifiState::Joining: {
      const wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) {
        setState(WifiState::Up);
        s_problem = "";
        if (settings::get().lastSsid != ssid()) {  // written only when it changes
          settings::get().lastSsid = ssid();
          settings::save();
        }
        noModemSleep();
        LOGF("wifi: joined \"%s\", http://%s  (signal %d dBm)", ssid().c_str(), WiFi.localIP().toString().c_str(),
             WiFi.RSSI());
        clockutil::sync();  // the idle clock
        // Started once Wi-Fi is up: MDNS.begin() before that has no interface to answer on.
        if (!s_mdnsUp && MDNS.begin(HOSTNAME)) {
          MDNS.addService("http", "tcp", 80);
          s_mdnsUp = true;
          LOGF("wifi: setup page also at %s", mdnsUrl().c_str());
        }
        break;
      }
      // A rejected password: the network is there and said no. Give up on it at once (waiting
      // out the timeout only to retry it forever looked like "joining for too long"). If the
      // device has never joined this network, it is most likely a typo from setup: open the
      // setup hotspot so it can be fixed. (A network simply not there -- the car before the
      // phone's hotspot starts -- never opens it.)
      const uint8_t why = s_discReason;
      const bool wrongPassword = why == WIFI_REASON_AUTH_EXPIRE || why == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                 why == WIFI_REASON_AUTH_FAIL || why == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                                 why == WIFI_REASON_MIC_FAILURE;
      if (wrongPassword && now - s_stateAt > 300) {  // not a late event from the last attempt
        s_problem = "Wrong password for " + ssid();
        s_changes++;
        LOGF("wifi: \"%s\" rejected the password (reason %u)", ssid().c_str(), why);
        WiFi.disconnect();
        if (settings::get().lastSsid != ssid()) {
          LOGF("wifi: never joined it before -- opening the setup hotspot to fix it");
          setState(WifiState::Waiting);
          startHotspot();
          break;
        }
        const int next = nextCandidate();
        if (next >= 0) joinSlot(next);
        else setState(WifiState::Waiting);
        break;
      }
      // Right after WiFi.begin() the status can still be the previous attempt's failure; only
      // believe a failure once this attempt has had JOIN_SETTLE_MS (both networks were being
      // "refused" within 5 ms, never really tried).
      const bool failed = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) && now - s_stateAt > JOIN_SETTLE_MS;
      if (failed || now - s_stateAt > JOIN_TIMEOUT_MS) {
        LOGF("wifi: could not join \"%s\" (%s)", ssid().c_str(), failed ? "refused" : "timed out");
        WiFi.disconnect();
        const int next = nextCandidate();
        if (next >= 0) joinSlot(next);
        else {
          if (s_blind) LOGF("wifi: no saved network could be joined -- retrying");
          setState(WifiState::Waiting);
        }
      }
      break;
    }
    case WifiState::Waiting:
      if (now - s_stateAt > RESCAN_WAIT_MS) startScan();
      break;
    case WifiState::Up:
      if (WiFi.status() != WL_CONNECTED) {
        // The phone hotspot switched off, the car drove away... look again straight away.
        LOGF("wifi: lost \"%s\"", ssid().c_str());
        WiFi.disconnect();
        startScan();
      }
      break;
  }
}

void startHotspot() {
  if (s_apUp) return;
  WiFi.scanDelete();
  if (s_state != WifiState::Up) WiFi.disconnect();
  // AP + station: the station half is what lets the page scan for networks.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(s_apName.c_str());  // open: the page never shows a password
  noModemSleep();
  dns = new DNSServer();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", WiFi.softAPIP());
  s_apUp = true;
  s_changes++;
  LOGF("wifi: hotspot \"%s\" -> http://%s", s_apName.c_str(), WiFi.softAPIP().toString().c_str());
}

void begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);  // WiFi.macAddress() is zeros until Wi-Fi starts
  char name[24];
  snprintf(name, sizeof(name), "CYD-Spotify-%02X%02X", mac[4], mac[5]);
  s_apName = name;

  // Why a join failed: wrong password vs. not there (stationTick() tells them apart).
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { s_discReason = info.wifi_sta_disconnected.reason; },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.persistent(false);  // our settings live in NVS, not the Wi-Fi driver's copy
  WiFi.setAutoReconnect(false);  // stationTick() picks the network and reconnects
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  noModemSleep();

  if (settings::networkCount() == 0) {
    setState(WifiState::NoNetworks);
    startHotspot();
  } else {
    startScan();
  }

  // Every page behind the login; the captive-portal redirect (onNotFound) only points at "/".
  const char* headers[] = {"Authorization"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, [] { if (authorized()) sendPage(); });
  server.on("/save", HTTP_POST, [] { if (authorized()) handleSave(); });
  server.on("/bright", HTTP_GET, [] { if (authorized()) handleBright(); });
  server.on("/invert", HTTP_GET, [] { if (authorized()) handleInvert(); });
  server.on("/screenoff", HTTP_GET, [] { if (authorized()) handleScreenOff(); });
  server.on("/scan", HTTP_GET, [] { if (authorized()) handleScan(); });
  server.on("/log", HTTP_GET, [] { if (authorized()) handleLog(); });
  server.on("/spotify/client", HTTP_POST, [] { if (authorized()) handleSpotifyClient(); });
  server.on("/spotify/login", HTTP_GET, [] { if (authorized()) handleSpotifyLogin(); });
  server.on("/spotify/code", HTTP_POST, [] { if (authorized()) handleSpotifyCode(); });
  server.on("/spotify/status", HTTP_GET, [] { if (authorized()) handleSpotifyStatus(); });
  server.on("/spotify/logout", HTTP_POST, [] { if (authorized()) handleSpotifyLogout(); });
#ifdef CYD_TEST_HOOKS  // acceptance-test endpoints (Stages 2 and 5); never in a release build
  server.on("/spotify/test-skips", HTTP_POST, [] {
    if (!authorized()) return;
    control::testSkips(server.arg("n").toInt(), server.hasArg("ms") ? server.arg("ms").toInt() : 6000);
    server.send(200, "text/plain", "ok");
  });
  server.on("/spotify/test-expiry", HTTP_POST, [] {
    if (!authorized()) return;
    spotify::testExpiry();
    server.send(200, "text/plain", "ok");
  });
#endif
  server.onNotFound(handleNotFound);
  server.begin();
}

void tick() {
  // While the hotspot is up, the station stops looking: background scans hop channels and
  // would keep dropping the phone that is on the setup page. Saving restarts the device.
  if (!s_apUp) stationTick();
  if (dns) dns->processNextRequest();
  server.handleClient();
  if (s_restartAt && (int32_t)(millis() - s_restartAt) >= 0) {
    delay(100);
    ESP.restart();
  }
}

WifiState wifiState() { return s_state; }
bool wifiUp() { return s_state == WifiState::Up && WiFi.status() == WL_CONNECTED; }
bool apActive() { return s_apUp; }
String problem() { return s_problem; }
String apName() { return s_apName; }
String ssid() { return s_slot >= 0 ? settings::get().ssid[s_slot] : String(); }
int8_t rssi() { return wifiUp() ? WiFi.RSSI() : 0; }
uint32_t stateChanges() { return s_changes; }

String url() {
  if (wifiUp()) return "http://" + WiFi.localIP().toString();
  if (s_apUp) return "http://" + WiFi.softAPIP().toString();
  return String();
}

String mdnsUrl() { return s_mdnsUp && wifiUp() ? String("http://") + HOSTNAME + ".local" : String(); }

bool hasPassword() { return settings::get().webPass.length() > 0; }

void clearPassword() {
  settings::get().webPass = "";
  s_failures = 0;
  s_lockedUntil = 0;
  settings::save();
  LOGF("web: setup page password removed on the device");
}

}  // namespace portal
