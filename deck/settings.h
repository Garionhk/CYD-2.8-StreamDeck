// ===========================================================================
// settings.h -- one struct, kept in NVS and mirrored to the SD card
// ===========================================================================
// Same arrangement as the MP3 player's settings: one struct, loaded once at
// boot, written back whenever something changes. What is new here is the SD
// card's /config.json, which exists so a device can be set up by editing one
// file on a computer instead of going through the hotspot page:
//
//   boot   NVS is read first (settings_begin), then the card (settings_loadSd).
//          Whatever the file says wins and is copied into NVS. Keys the file
//          leaves out keep their NVS value, and the file is rewritten complete.
//   change settings_save() writes NVS and rewrites the file atomically.
//          settings_saveSoon() does the same ~2 s after the LAST call, for a
//          brightness slider that would otherwise write the card per pixel.
//
// No card, or no file on it, and the device runs from NVS alone -- a card
// inserted later gets a complete file written on the next boot.
#pragma once

#include <Arduino.h>
#include "board.h"

struct Settings {
  // ---- display (the panel fields are the ones display.cpp reads) ----
  uint16_t panel      = CYD_PANEL_DRIVER;   // CYD_PANEL_ST7789 / CYD_PANEL_ILI9341
  bool     invert     = CYD_TFT_INVERT;
  uint8_t  rotation   = CYD_ROTATION;       // 1 or 3: the grid is landscape only
  uint8_t  brightness = 80;                 // backlight %, BRIGHTNESS_MIN-100
  uint8_t  cols       = 8;                  // key grid: 8x4 (Companion's page),
  uint8_t  rows       = 4;                  //   5x3 or 4x3 -- see keys.h

  // ---- network ----
  String   wifiSsid;
  String   wifiPass;

  // ---- Companion ----
  String   host;                            // Companion's IP or hostname
  uint16_t port       = 16622;              // Satellite API, TCP
  String   deviceId;                        // empty: "cyd-" + last 3 MAC bytes
  String   name       = "CYD Stream Deck";  // PRODUCT_NAME shown in Companion

  // ---- setup web page ----
  String   webPass;                         // login "admin"; empty = page is open
};

extern Settings g_settings;

static const uint8_t BRIGHTNESS_MIN = 10;   // never dark enough to look dead

// Where the running settings came from, for the status screen and the log.
enum SdConfig : uint8_t {
  SDCFG_NO_CARD = 0,   // no card: NVS only
  SDCFG_LOADED,        // /config.json read
  SDCFG_CREATED,       // card present, no file: wrote one from NVS
  SDCFG_BAD            // file did not parse: moved to /config.bad, wrote a fresh one
};

void        settings_begin();             // NVS -> g_settings
SdConfig    settings_loadSd();            // /config.json -> g_settings (+ NVS, touch cal)
SdConfig    settings_sdState();
const char* settings_sdStateText();

void settings_save();                     // g_settings -> NVS + /config.json
void settings_saveSoon();                 // the same, debounced
void settings_tick();                     // call every loop(): runs a pending save
void settings_savePanel();                // display.cpp's boot gesture calls this

String settings_deviceId();               // the configured id, or the MAC-derived one
String settings_layoutName();             // "8x4"
bool   settings_parseLayout(const String& s, uint8_t& cols, uint8_t& rows);
const char* settings_panelName(uint16_t panel);
