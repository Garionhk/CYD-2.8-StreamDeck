#include "settings.h"
#include "log.h"
#include "storage.h"
#include "touch.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <esp_mac.h>

Settings g_settings;

static Preferences prefs;
static const char* NVS_NS = "deckcfg";

static const char* CFG_PATH = "/config.json";
static const char* CFG_TMP  = "/config.tmp";
static const char* CFG_BAD  = "/config.bad";

static SdConfig sdState = SDCFG_NO_CARD;
static uint32_t saveDueAt = 0;            // 0: nothing pending
static const uint32_t SAVE_DEBOUNCE_MS = 2000;

const char* settings_panelName(uint16_t panel) {
  return panel == CYD_PANEL_ILI9341 ? "ILI9341" : "ST7789";
}

static void sanitise(Settings& s) {
  if (s.panel != CYD_PANEL_ST7789 && s.panel != CYD_PANEL_ILI9341) s.panel = CYD_PANEL_DRIVER;
  // The 4x3 grid only fits landscape; anything else becomes the board default.
  if (s.rotation != 1 && s.rotation != 3) s.rotation = CYD_ROTATION;
  s.brightness = constrain(s.brightness, BRIGHTNESS_MIN, 100);
  if (s.port == 0) s.port = 16622;
  // The layouts keys.cpp is laid out for; anything else becomes the default.
  const bool known = (s.cols == 8 && s.rows == 4) || (s.cols == 5 && s.rows == 3) ||
                     (s.cols == 4 && s.rows == 3);
  if (!known) { s.cols = 8; s.rows = 4; }
  s.host.trim();
  if (s.name.isEmpty()) s.name = "CYD Stream Deck";
}

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------
void settings_begin() {
  // Opened read-write even though nothing is written: opening a namespace that
  // does not exist yet read-only logs an NVS error on every first boot.
  prefs.begin(NVS_NS, false);
  Settings& s = g_settings;
  s.panel      = prefs.getUShort("panel",  s.panel);
  s.invert     = prefs.getBool  ("invert", s.invert);
  s.rotation   = prefs.getUChar ("rot",    s.rotation);
  s.brightness = prefs.getUChar ("bright", s.brightness);
  s.cols       = prefs.getUChar ("cols",   s.cols);
  s.rows       = prefs.getUChar ("rows",   s.rows);
  s.wifiSsid   = prefs.getString("ssid",   s.wifiSsid);
  s.wifiPass   = prefs.getString("pass",   s.wifiPass);
  s.host       = prefs.getString("host",   s.host);
  s.port       = prefs.getUShort("port",   s.port);
  s.deviceId   = prefs.getString("devId",  s.deviceId);
  s.name       = prefs.getString("name",   s.name);
  s.webPass    = prefs.getString("webPass", s.webPass);
  prefs.end();
  sanitise(s);
}

static void saveNvs() {
  const Settings& s = g_settings;
  prefs.begin(NVS_NS, false);
  prefs.putUShort("panel",  s.panel);
  prefs.putBool  ("invert", s.invert);
  prefs.putUChar ("rot",    s.rotation);
  prefs.putUChar ("bright", s.brightness);
  prefs.putUChar ("cols",   s.cols);
  prefs.putUChar ("rows",   s.rows);
  prefs.putString("ssid",   s.wifiSsid);
  prefs.putString("pass",   s.wifiPass);
  prefs.putString("host",   s.host);
  prefs.putUShort("port",   s.port);
  prefs.putString("devId",  s.deviceId);
  prefs.putString("name",   s.name);
  prefs.putString("webPass", s.webPass);
  prefs.end();
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
static void toJson(JsonDocument& doc) {
  const Settings& s = g_settings;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"]     = s.wifiSsid;
  wifi["password"] = s.wifiPass;

  JsonObject comp = doc["companion"].to<JsonObject>();
  comp["host"]     = s.host;
  comp["port"]     = s.port;
  comp["deviceId"] = s.deviceId;
  comp["name"]     = s.name;

  // In the file as well, so that blanking it on a computer is the way back in
  // when it is forgotten (the device menu's Web setup screen is the other).
  JsonObject web = doc["web"].to<JsonObject>();
  web["password"] = s.webPass;

  JsonObject disp = doc["display"].to<JsonObject>();
  disp["panel"]      = settings_panelName(s.panel);
  disp["invert"]     = s.invert;
  disp["rotation"]   = s.rotation;
  disp["brightness"] = s.brightness;
  disp["layout"]     = settings_layoutName();

  // Only a measured calibration: the built-in fallback is not worth carrying
  // to another board, and writing it would make it look like a real one.
  const TouchCal c = touch_calibration();
  if (c.valid) {
    JsonObject t = doc["touch"].to<JsonObject>();
    t["xAt0"]       = c.xAt0;
    t["xAtW"]       = c.xAtW;
    t["yAt0"]       = c.yAt0;
    t["yAtH"]       = c.yAtH;
    t["swapXY"]     = c.swapXY;
    t["zThreshold"] = c.zThreshold;
  }
}

// Each field only if the file has it, with the right type: a hand-edited file
// that leaves something out keeps the current value for it.
template <typename T>
static void take(JsonVariantConst v, T& into) {
  if (v.is<T>()) into = v.as<T>();
}

static void takeString(JsonVariantConst v, String& into) {
  if (v.is<const char*>()) into = v.as<const char*>();
}

static void fromJson(const JsonDocument& doc) {
  Settings& s = g_settings;
  takeString(doc["wifi"]["ssid"], s.wifiSsid);
  takeString(doc["wifi"]["password"], s.wifiPass);

  takeString(doc["companion"]["host"], s.host);
  take(doc["companion"]["port"], s.port);
  takeString(doc["companion"]["deviceId"], s.deviceId);
  takeString(doc["companion"]["name"], s.name);
  takeString(doc["web"]["password"], s.webPass);

  // "ST7789" / "ILI9341" as written, or the bare number for the stubborn.
  JsonVariantConst panel = doc["display"]["panel"];
  if (panel.is<const char*>()) {
    String p = panel.as<const char*>();
    p.toUpperCase();
    if (p.indexOf("7789") >= 0) s.panel = CYD_PANEL_ST7789;
    else if (p.indexOf("9341") >= 0) s.panel = CYD_PANEL_ILI9341;
  } else if (panel.is<int>()) {
    s.panel = panel.as<int>();
  }
  take(doc["display"]["invert"], s.invert);
  take(doc["display"]["rotation"], s.rotation);
  take(doc["display"]["brightness"], s.brightness);
  JsonVariantConst layout = doc["display"]["layout"];
  if (layout.is<const char*>()) settings_parseLayout(layout.as<const char*>(), s.cols, s.rows);
  sanitise(s);

  // Touch calibration from the file, applied only when it is a real one and
  // differs from what NVS already has -- otherwise every boot rewrites NVS.
  JsonObjectConst t = doc["touch"];
  if (!t.isNull()) {
    TouchCal c = touch_calibration();
    take(t["xAt0"], c.xAt0);
    take(t["xAtW"], c.xAtW);
    take(t["yAt0"], c.yAt0);
    take(t["yAtH"], c.yAtH);
    take(t["swapXY"], c.swapXY);
    take(t["zThreshold"], c.zThreshold);
    const TouchCal now = touch_calibration();
    const bool sane = labs(c.xAtW - c.xAt0) > 500 && labs(c.yAtH - c.yAt0) > 500;
    const bool same = now.valid && c.xAt0 == now.xAt0 && c.xAtW == now.xAtW &&
                      c.yAt0 == now.yAt0 && c.yAtH == now.yAtH &&
                      c.swapXY == now.swapXY && c.zThreshold == now.zThreshold;
    if (sane && !same) {
      touch_setCalibration(c, true);
      Log.println("settings: touch calibration taken from config.json");
    }
  }
}

static String render() {
  JsonDocument doc;
  toJson(doc);
  String out;
  serializeJsonPretty(doc, out);
  out += '\n';
  return out;
}

// Write to a temp file, then swap it in: a power cut mid-write leaves the old
// file (or, at worst, the temp one) rather than half a config.
static bool writeFile(const String& text) {
  if (!storage_ok()) return false;
  SD.remove(CFG_TMP);
  File f = SD.open(CFG_TMP, FILE_WRITE);
  if (!f) {
    Log.println("settings: cannot create " + String(CFG_TMP));
    return false;
  }
  const size_t n = f.print(text);
  f.close();
  if (n != text.length()) {
    Log.println("settings: short write to the card");
    SD.remove(CFG_TMP);
    return false;
  }
  SD.remove(CFG_PATH);
  if (!SD.rename(CFG_TMP, CFG_PATH)) {
    Log.println("settings: rename to config.json failed");
    return false;
  }
  return true;
}

static String readFile() {
  File f = SD.open(CFG_PATH, FILE_READ);
  if (!f) return String();
  String text = f.readString();
  f.close();
  return text;
}

SdConfig settings_loadSd() {
  if (!storage_ok()) {
    sdState = SDCFG_NO_CARD;
  } else if (!SD.exists(CFG_PATH)) {
    // A card without the file: give the owner a complete one to edit.
    sdState = writeFile(render()) ? SDCFG_CREATED : SDCFG_NO_CARD;
  } else {
    const String text = readFile();
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, text);
    if (err) {
      // Keep what they wrote -- it is probably one stray comma away from right.
      Log.printf("settings: config.json does not parse (%s) -- moved to config.bad\n",
                    err.c_str());
      SD.remove(CFG_BAD);
      SD.rename(CFG_PATH, CFG_BAD);
      writeFile(render());
      sdState = SDCFG_BAD;
    } else {
      fromJson(doc);
      saveNvs();
      // Fill in anything the file left out, so it always shows every setting.
      // render() is deterministic, so a complete file is not rewritten.
      const String full = render();
      if (full != text) writeFile(full);
      sdState = SDCFG_LOADED;
    }
  }

  const Settings& s = g_settings;
  // The WiFi password is never printed: serial logs end up in bug reports.
  Log.printf("settings: source %s  wifi \"%s\"  companion %s:%u  id %s\n",
                settings_sdStateText(), s.wifiSsid.c_str(),
                s.host.length() ? s.host.c_str() : "(not set)", s.port,
                settings_deviceId().c_str());
  Log.printf("settings: panel %s invert %d rot %u bright %u%%  layout %s\n",
                settings_panelName(s.panel), s.invert, s.rotation, s.brightness,
                settings_layoutName().c_str());
  return sdState;
}

SdConfig settings_sdState() { return sdState; }

const char* settings_sdStateText() {
  switch (sdState) {
    case SDCFG_LOADED:  return "SD config.json";
    case SDCFG_CREATED: return "SD config.json (new)";
    case SDCFG_BAD:     return "SD config.json was invalid";
    default:            return "no SD card";
  }
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------
void settings_save() {
  sanitise(g_settings);
  saveDueAt = 0;
  saveNvs();
  bool sd = false;
  if (storage_ok()) {
    sd = writeFile(render());
    if (sd && sdState == SDCFG_NO_CARD) sdState = SDCFG_CREATED;
  }
  Log.printf("settings: saved -- layout %s, bright %u%%, panel %s, rot %u  (NVS%s)\n",
             settings_layoutName().c_str(), g_settings.brightness,
             settings_panelName(g_settings.panel), g_settings.rotation,
             sd ? " + SD config.json" : storage_ok() ? ", SD WRITE FAILED" : " only, no card");
}

void settings_saveSoon() {
  saveDueAt = millis() + SAVE_DEBOUNCE_MS;
  if (saveDueAt == 0) saveDueAt = 1;
}

void settings_tick() {
  if (saveDueAt && (int32_t)(millis() - saveDueAt) >= 0) settings_save();
}

void settings_savePanel() { settings_save(); }

String settings_deviceId() {
  if (g_settings.deviceId.length()) return g_settings.deviceId;
  // Read from eFuse: WiFi.macAddress() is all zeros before WiFi starts.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char id[16];
  snprintf(id, sizeof(id), "cyd-%02x%02x%02x", mac[3], mac[4], mac[5]);
  return id;
}

String settings_layoutName() {
  return String(g_settings.cols) + "x" + String(g_settings.rows);
}

// "8x4", "8X4", "8 x 4" -> 8, 4. Leaves the outputs alone on anything else.
bool settings_parseLayout(const String& text, uint8_t& cols, uint8_t& rows) {
  String t = text;
  t.toLowerCase();
  t.replace(" ", "");
  const int x = t.indexOf('x');
  if (x <= 0) return false;
  const int c = t.substring(0, x).toInt(), r = t.substring(x + 1).toInt();
  if (c <= 0 || r <= 0) return false;
  cols = c;
  rows = r;
  return true;
}
