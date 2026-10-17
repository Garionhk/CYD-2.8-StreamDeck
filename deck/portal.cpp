// WebServer.h first (as the MP3 uploader): TFT_eSPI defines FS_NO_GLOBALS
// before including FS.h, which hides the plain `FS` name WebServer.h uses.
#include <WiFi.h>
#include "log.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include "portal.h"
#include "settings.h"
#include "display.h"
#include "satellite.h"
#include "version.h"

static WebServer  server(80);
static DNSServer* dns = nullptr;
static String     apName;
static bool       apUp = false;
static uint32_t   joinStartedAt = 0;
static uint32_t   restartAt = 0;
static bool       mdnsUp = false;

static const uint32_t JOIN_GIVE_UP_MS = 20000;   // then the hotspot joins in

// Login, as the WOL-NTFY settings page: HTTP Basic, user "admin", the
// password from settings. No password set = the page is open (and says so).
static const char* WEB_USER    = "admin";
static const char* WEB_REALM   = "CYD Stream Deck";
static const int   FREE_TRIES  = 5;           // wrong passwords before a lockout
static uint8_t     failures    = 0;
static uint32_t    lockedUntil = 0;

// ---------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Stream Deck</title>
<style>
:root{color-scheme:light dark;--bg:#f4f5f7;--card:#fff;--ink:#1c1e21;--dim:#65676b;--line:#d8dadf;--acc:#0a7cff}
@media(prefers-color-scheme:dark){:root{--bg:#18191a;--card:#242526;--ink:#e4e6eb;--dim:#a8abaf;--line:#3a3b3c}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 12px}h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin:0 0 8px}
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
</style></head><body><main>
<h1>CYD Stream Deck</h1>
<section class="status" id="st">%STATUS%</section>
%NOPASS%
<form method="post" action="/save">
<section><h2>WiFi</h2>
<label for="ssid">Network</label>
<div class="row"><input id="ssid" name="ssid" value="%SSID%" list="nets" autocomplete="off">
<button type="button" class="ghost" style="flex:0 0 auto" onclick="scan(this)">Scan</button></div>
<datalist id="nets"></datalist>
<label for="pass">Password</label>
<input id="pass" name="pass" type="password" placeholder="%PASSHINT%" autocomplete="new-password">
</section>
<section><h2>Companion</h2>
<div class="row"><div style="flex:2"><label for="host">Companion IP or hostname</label>
<input id="host" name="host" value="%HOST%" placeholder="192.168.1.50"></div>
<div><label for="port">Port</label><input id="port" name="port" type="number" value="%PORT%"></div></div>
<label for="name">Name shown in Companion</label>
<input id="name" name="name" value="%NAME%">
</section>
<section><h2>Display</h2>
<label for="bright">Brightness <span id="bv">%BRIGHT%</span>%</label>
<input id="bright" name="bright" type="range" min="10" max="100" step="5" value="%BRIGHT%">
<div class="row"><div><label for="panel">Panel controller</label>
<select id="panel" name="panel"><option value="7789"%P7789%>ST7789</option><option value="9341"%P9341%>ILI9341</option></select></div>
<div><label for="invert">Colours</label>
<select id="invert" name="invert"><option value="1"%INV1%>Inverted</option><option value="0"%INV0%>Normal</option></select></div>
<div><label for="rot">Orientation</label>
<select id="rot" name="rot"><option value="1"%ROT1%>Normal</option><option value="3"%ROT3%>Upside down</option></select></div></div>
<label for="layout">Key layout</label>
<select id="layout" name="layout">
<option value="8x4"%L8x4%>8 &times; 4 &ndash; the whole Companion page (32 keys)</option>
<option value="5x3"%L5x3%>5 &times; 3 &ndash; Stream Deck MK.2 shape (15 keys)</option>
<option value="4x3"%L4x3%>4 &times; 3 &ndash; biggest keys (12 keys)</option></select>
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
<p class="status">Settings are also written to <b>config.json</b> on the SD card when one is inserted. &middot; <a href="/log">Recent log</a></p>
</main><script>
const b=document.getElementById('bright'),bv=document.getElementById('bv');let t;
b.oninput=()=>{bv.textContent=b.value;clearTimeout(t);t=setTimeout(()=>fetch('/bright?v='+b.value),120)};
document.getElementById('invert').onchange=e=>fetch('/invert?v='+e.target.value);
async function scan(btn){btn.disabled=true;btn.textContent='…';
try{const r=await fetch('/scan');const l=await r.json();const d=document.getElementById('nets');d.innerHTML='';
l.forEach(n=>{const o=document.createElement('option');o.value=n;d.appendChild(o)});
document.getElementById('ssid').focus();btn.textContent=l.length+' found'}catch(e){btn.textContent='Scan'}btn.disabled=false}
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
      default:  o += c;
    }
  }
  return o;
}

static String statusHtml() {
  String s = "WiFi: <b>";
  if (portal_wifiUp()) s += esc(g_settings.wifiSsid) + "</b> (" + WiFi.localIP().toString() + ")";
  else if (g_settings.wifiSsid.length()) s += "joining " + esc(g_settings.wifiSsid) + "</b>";
  else s += "not set</b>";
  if (apUp) s += " &middot; hotspot <b>" + esc(apName) + "</b>";
  s += "<br>Companion: <b>" + String(sat_stateText()) + "</b>";
  if (sat_state() != SAT_READY && sat_lastError().length()) s += " &ndash; " + esc(sat_lastError());
  if (mdnsUp) s += "<br>Address: <b>" + portal_mdnsUrl() + "</b>";
  s += "<br>Device id: <b>" + esc(settings_deviceId()) + "</b> &middot; settings from <b>" +
       settings_sdStateText() + "</b>";
  s += "<br>Firmware: <b>" FIRMWARE_VERSION "</b> &middot; Memory: <b>" + String(ESP.getFreeHeap() / 1024) + " KB</b> free, largest block " +
       String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024) + " KB";
  return s;
}

// The value for one %TOKEN% in PAGE.
static String pageValue(const String& key) {
  const Settings& s = g_settings;
  if (key == "STATUS")   return statusHtml();
  if (key == "SSID")     return esc(s.wifiSsid);
  if (key == "PASSHINT") return s.wifiPass.length() ? "(unchanged)" : "(none)";
  if (key == "HOST")     return esc(s.host);
  if (key == "PORT")     return String(s.port);
  if (key == "NAME")     return esc(s.name);
  if (key == "BRIGHT")   return String(s.brightness);
  const bool pw = s.webPass.length();
  if (key == "NOPASS")
    return pw ? "" : "<section class=\"warn\"><b>No password set.</b> Anyone on this network "
                     "(or on the deck's hotspot) can open this page and change these settings. "
                     "Set one below.</section>";
  if (key == "PASSSTATE")
    return pw ? "Set &ndash; this page asks for user <b>admin</b> and the password."
              : "None &ndash; the page is open.";
  if (key == "WPHINT")   return pw ? "(unchanged)" : "(none)";
  if (key == "WPCLEAR")
    return pw ? "<label class=\"chk\"><input type=\"checkbox\" name=\"wpclear\" value=\"1\">"
                "Remove the password (the page will be open)</label>" : "";
  const char* sel = " selected";
  if (key == "P7789")    return s.panel == CYD_PANEL_ST7789 ? sel : "";
  if (key == "P9341")    return s.panel == CYD_PANEL_ILI9341 ? sel : "";
  if (key == "INV1")     return s.invert ? sel : "";
  if (key == "INV0")     return s.invert ? "" : sel;
  if (key == "ROT1")     return s.rotation == 1 ? sel : "";
  if (key == "ROT3")     return s.rotation == 3 ? sel : "";
  if (key.startsWith("L")) return key.substring(1) == settings_layoutName() ? sel : "";
  return String();
}

static bool tokenChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == 'x';
}

// Streamed in ~512-byte chunks straight from flash, filling in %TOKEN%s on
// the way. Building the whole page as one String first needs a single ~6 KB
// block of heap; with the key images cached that block is not always there,
// the String quietly comes out empty, and the browser gets a blank page.
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
      for (char k = pgm_read_byte(q); tokenChar(k) && key.length() < 12; k = pgm_read_byte(++q))
        key += k;
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
  server.sendContent("");   // end of the chunked reply
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

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
static void handleSave() {
  Settings& s = g_settings;
  const Settings before = s;
  Log.printf("web     : settings form from %s (layout %s)\n",
             server.client().remoteIP().toString().c_str(), server.arg("layout").c_str());

  // The page password first: a mistyped one rejects the whole form, so the
  // owner is never locked out by a typo they cannot see.
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
  if (clearPass && s.webPass.length()) { s.webPass = ""; passChanged = true; }
  else if (!clearPass && wp1.length()) { s.webPass = wp1; passChanged = true; }
  if (passChanged)
    Log.printf("web     : setup page password %s\n", s.webPass.length() ? "set" : "removed");

  String ssid = server.arg("ssid");
  ssid.trim();
  const String pass = server.arg("pass");
  // An empty password box means "keep it" -- unless the network changed, when
  // it means the new network is open.
  if (ssid != s.wifiSsid || pass.length()) s.wifiPass = pass;
  s.wifiSsid = ssid;
  s.host = server.arg("host");
  s.port = server.arg("port").toInt();
  s.name = server.arg("name");
  s.brightness = server.arg("bright").toInt();
  s.panel = server.arg("panel").toInt();
  s.invert = server.arg("invert") == "1";
  s.rotation = server.arg("rot").toInt();
  settings_parseLayout(server.arg("layout"), s.cols, s.rows);
  settings_save();   // also sanitises

  display_setBacklight(s.brightness);
  tft.invertDisplay(s.invert);

  const bool restart = s.wifiSsid != before.wifiSsid || s.wifiPass != before.wifiPass ||
                       s.host != before.host || s.port != before.port || s.name != before.name ||
                       s.panel != before.panel || s.rotation != before.rotation ||
                       s.cols != before.cols || s.rows != before.rows;
  if (restart) {
    sendMessagePage("Saved", "The deck is restarting with the new settings. "
                             "If you changed its WiFi network, reconnect your phone to that network.");
    restartAt = millis() + 1500;
  } else if (passChanged && s.webPass.length()) {
    sendMessagePage("Saved", "Settings saved. The page now asks for a login: user <b>admin</b> and "
                             "your new password.");
  } else {
    sendMessagePage("Saved", "Settings saved.");
  }
}

static void handleBright() {
  g_settings.brightness = constrain((int)server.arg("v").toInt(), BRIGHTNESS_MIN, 100);
  display_setBacklight(g_settings.brightness);
  settings_saveSoon();
  server.send(200, "text/plain", "ok");
}

static void handleInvert() {
  g_settings.invert = server.arg("v") == "1";
  tft.invertDisplay(g_settings.invert);
  settings_saveSoon();
  server.send(200, "text/plain", "ok");
}

static void handleScan() {
  const int n = WiFi.scanNetworks();
  String json = "[";
  int listed = 0;
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    // Strongest first is how the scan comes back; drop repeats of a name.
    bool seen = false;
    for (int j = 0; j < i; j++)
      if (WiFi.SSID(j) == ssid) { seen = true; break; }
    if (seen) continue;
    String e = ssid;
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

// True when the request may proceed. Otherwise it has been answered: a login
// prompt, or "too many tries" while locked out.
static bool authorized() {
  if (g_settings.webPass.isEmpty()) return true;
  const uint32_t now = millis();
  if (lockedUntil && (int32_t)(lockedUntil - now) > 0) {
    server.send(429, "text/plain",
                "Too many wrong passwords. Try again in " + String((lockedUntil - now + 999) / 1000) +
                    " s.");
    return false;
  }
  if (server.authenticate(WEB_USER, g_settings.webPass.c_str())) {
    failures = 0;
    return true;
  }
  // A browser's first request carries no login: that is not a wrong guess.
  if (server.hasHeader("Authorization")) {
    if (failures < 255) failures++;
    const String ip = server.client().remoteIP().toString();
    if (failures >= FREE_TRIES) {
      // 5 s, then doubling with every further miss, up to 5 minutes.
      const uint32_t wait = min<uint32_t>(5000UL << min(failures - FREE_TRIES, 6), 300000UL);
      lockedUntil = now + wait;
      Log.printf("web     : wrong password from %s (%u in a row) -- locked for %lu s\n", ip.c_str(),
                 failures, (unsigned long)(wait / 1000));
    } else {
      Log.printf("web     : wrong password from %s (%u in a row)\n", ip.c_str(), failures);
    }
  }
  server.requestAuthentication(BASIC_AUTH, WEB_REALM);
  return false;
}

static void handleNotFound() {
  // Captive portal: on the hotspot, every unknown URL (a phone's connectivity
  // check) goes to the setup page, which is what makes the phone show it.
  if (apUp) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "not found");
  }
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
// No modem sleep. It is on by default, and with it the radio dozes between
// beacons: pings to the deck took 130-560 ms, and a page of key images
// trickled in over 9 s instead of well under one. WiFi.setSleep() called
// before WiFi has started does not stick on this core, so this is applied
// after every start and again on every join.
static void noModemSleep() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

static void startHotspot() {
  if (apUp) return;
  // AP + station: the station half is what lets the page scan for networks,
  // and it keeps trying the saved network in the background.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str());   // open: the page never shows the password
  noModemSleep();
  dns = new DNSServer();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", WiFi.softAPIP());
  apUp = true;
  Log.printf("wifi    : hotspot \"%s\" -> http://%s\n", apName.c_str(),
                WiFi.softAPIP().toString().c_str());
}

void portal_begin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);   // WiFi.macAddress() is zeros until WiFi starts
  char name[24];
  snprintf(name, sizeof(name), "CYD-Deck-%02X%02X", mac[4], mac[5]);
  apName = name;

  WiFi.persistent(false);         // our settings live in NVS/SD, not the WiFi driver's copy
  WiFi.setAutoReconnect(true);
  if (g_settings.wifiSsid.isEmpty()) {
    startHotspot();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(settings_deviceId().c_str());
    WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPass.c_str());
    noModemSleep();
    joinStartedAt = millis();
    Log.printf("wifi    : joining \"%s\"\n", g_settings.wifiSsid.c_str());
  }

  // Every page behind the login; the captive-portal redirect (onNotFound)
  // only ever points at "/".
  server.on("/", HTTP_GET, [] { if (authorized()) sendPage(); });
  server.on("/save", HTTP_POST, [] { if (authorized()) handleSave(); });
  server.on("/bright", HTTP_GET, [] { if (authorized()) handleBright(); });
  server.on("/invert", HTTP_GET, [] { if (authorized()) handleInvert(); });
  server.on("/scan", HTTP_GET, [] { if (authorized()) handleScan(); });
  server.on("/log", HTTP_GET, [] { if (authorized()) handleLog(); });
  server.onNotFound(handleNotFound);
  server.begin();
}

void portal_tick() {
  static bool wasUp = false;
  const bool up = portal_wifiUp();
  if (up != wasUp) {
    wasUp = up;
    if (up) {
      noModemSleep();
      wifi_ps_type_t ps;
      esp_wifi_get_ps(&ps);
      Log.printf("wifi    : joined, http://%s  (power save %s, signal %d dBm)\n",
                 WiFi.localIP().toString().c_str(), ps == WIFI_PS_NONE ? "off" : "ON", WiFi.RSSI());
      // Started once WiFi is up: MDNS.begin() before that has no interface to answer on.
      if (!mdnsUp && MDNS.begin(settings_deviceId().c_str())) {
        MDNS.addService("http", "tcp", 80);
        mdnsUp = true;
        Log.printf("wifi    : setup page also at %s\n", portal_mdnsUrl().c_str());
      }
    } else {
      Log.println("wifi    : disconnected");
    }
  }
  if (!up && !apUp && joinStartedAt && millis() - joinStartedAt > JOIN_GIVE_UP_MS) {
    Log.println("wifi    : could not join -- opening the hotspot as well");
    startHotspot();
  }
  if (dns) dns->processNextRequest();
  server.handleClient();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
    delay(100);
    ESP.restart();
  }
}

bool portal_apActive() { return apUp; }
bool portal_wifiUp() { return g_settings.wifiSsid.length() && WiFi.status() == WL_CONNECTED; }
String portal_apName() { return apName; }

String portal_url() {
  if (portal_wifiUp()) return "http://" + WiFi.localIP().toString();
  if (apUp) return "http://" + WiFi.softAPIP().toString();
  return String();
}

String portal_mdnsUrl() {
  return mdnsUp && portal_wifiUp() ? "http://" + settings_deviceId() + ".local" : String();
}

bool portal_hasPassword() { return g_settings.webPass.length() > 0; }

void portal_clearPassword() {
  g_settings.webPass = "";
  failures = 0;
  lockedUntil = 0;
  settings_save();
  Log.println("web     : setup page password removed on the device");
}
