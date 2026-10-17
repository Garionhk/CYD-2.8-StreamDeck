// ===========================================================================
// CYD Stream Deck -- a Bitfocus Companion surface on an ESP32-2432S028R
// ===========================================================================
// A grid of touch keys (8x4 = a whole Companion page by default, or 5x3 / 4x3)
// that show whatever Companion draws on them and press its buttons, over
// WiFi, through Companion's Satellite API.
//
//   Display  HSPI      TFT_eSPI, ST7789 or ILI9341 (runtime switch, display.h)
//   SD card  VSPI      /config.json -- quick setup, kept in step with NVS
//   Touch    bit-bang  XPT2046 (touch.h)
//   Network  WiFi      Satellite API client (satellite.h) + setup page (portal.h)
//
// The display, touch and calibration code is the MP3 player's, unchanged
// where possible; see that project's docs/decisions.md for why it is built so.
//
// Gestures
//   finger held from power-up, 8 s   switch panel controller ST7789 <-> ILI9341
//   hold anywhere 3 s                device menu (brightness, flip, calibrate,
//                                    web setup QR code)

#include <SD.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

#include "board.h"
#include "log.h"
#include "version.h"
#include "settings.h"
#include "storage.h"
#include "display.h"
#include "touch.h"
#include "calibrate.h"
#include "ui.h"
#include "keys.h"
#include "satellite.h"
#include "portal.h"
#include "status.h"
#include "menu.h"

// ---------------------------------------------------------------------------
// Compile-time cross-checks: board.h vs TFT_eSPI's User_Setup.h (as the MP3
// player). Each is a mistake with no runtime error -- a dead panel, or an SD
// card that will not mount -- so it is worth stopping the build for.
// ---------------------------------------------------------------------------
#ifndef USE_HSPI_PORT
  #error "User_Setup.h is missing '#define USE_HSPI_PORT' -- the display would take VSPI, which the SD card needs. Copy config/User_Setup_ST7789.h.template into the TFT_eSPI library."
#endif
#if CYD_PANEL_DRIVER == CYD_PANEL_ST7789
  #if !defined(ST7789_DRIVER) && !defined(ST7789_2_DRIVER)
    #error "board.h selects an ST7789 panel but TFT_eSPI's User_Setup.h does not define ST7789_DRIVER. Copy config/User_Setup_ST7789.h.template into the TFT_eSPI library."
  #endif
#elif CYD_PANEL_DRIVER == CYD_PANEL_ILI9341
  #if !defined(ILI9341_DRIVER) && !defined(ILI9341_2_DRIVER)
    #error "board.h selects an ILI9341 panel but TFT_eSPI's User_Setup.h does not define it. Copy config/User_Setup_ILI9341.h.template into the TFT_eSPI library."
  #endif
#endif
#if TFT_WIDTH != CYD_PANEL_W || TFT_HEIGHT != CYD_PANEL_H
  #error "User_Setup.h's TFT_WIDTH/TFT_HEIGHT disagree with board.h."
#endif
#if TFT_MOSI != CYD_TFT_MOSI_PIN || TFT_SCLK != CYD_TFT_SCLK_PIN || \
    TFT_CS != CYD_TFT_CS_PIN || TFT_DC != CYD_TFT_DC_PIN
  #error "User_Setup.h's TFT pins disagree with board.h."
#endif

static const uint32_t MENU_HOLD_MS     = 3000;

enum Screen : uint8_t { SCREEN_NONE, SCREEN_STATUS, SCREEN_KEYS, SCREEN_MENU };
static Screen screen = SCREEN_NONE;

// ---------------------------------------------------------------------------
// Satellite callbacks
// ---------------------------------------------------------------------------
static void onKeyState(int key, bool image, uint16_t bg, uint16_t fg, const String& text) {
  keys_set(key, image, bg, fg, text);
}

static void onClearKeys() { keys_clearAll(); }

static void onBrightness(int percent) {
  // Companion's surface brightness. Floored like every other route, so a 0
  // from Companion cannot leave a board that looks dead at the next boot.
  const uint8_t pct = constrain(percent, (int)BRIGHTNESS_MIN, 100);
  if (pct == g_settings.brightness) return;
  g_settings.brightness = pct;
  display_setBacklight(pct);
  settings_saveSoon();
  Log.printf("sat     : brightness %d%%\n", pct);
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
static void show(Screen s) {
  if (s == screen) return;
  screen = s;
  keys_setVisible(s == SCREEN_KEYS);
  if (s == SCREEN_KEYS) keys_drawAll();
  else if (s == SCREEN_STATUS) status_draw(true);
  else if (s == SCREEN_MENU) menu_open();
}

// ---------------------------------------------------------------------------
// Touch on the grid: finger down = key down, finger up = key up
// ---------------------------------------------------------------------------
static bool wasDown = false;
static bool holdUsed = false;   // this press already opened the menu
static int  heldKey = -1;

static void releaseKey() {
  if (heldKey < 0) return;
  sat_keyPress(heldKey, false);
  keys_highlight(heldKey, false);
  heldKey = -1;
}

static void handleTouch() {
  touch_poll();                 // events are classified on release; we want edges
  const bool down = touch_isDown();

  if (down && !wasDown) {
    holdUsed = false;
    if (screen == SCREEN_KEYS) {
      int x, y;
      touch_position(x, y);
      heldKey = keys_hit(x, y);
      if (heldKey >= 0) {
        sat_keyPress(heldKey, true);
        keys_highlight(heldKey, true);
      }
    }
  }

  if (down && !holdUsed && touch_heldMs() >= MENU_HOLD_MS) {
    holdUsed = true;
    releaseKey();               // before the menu: no key left stuck down in Companion
    show(SCREEN_MENU);
  }

  if (!down && wasDown) releaseKey();
  wasDown = down;
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(50);
  cydPrintBanner();
  Log.printf("firmware: CYD Stream Deck %s\n", FIRMWARE_VERSION);
  cydRgbLedOff();               // active LOW: it powers up glowing white

  settings_begin();             // NVS
  touch_begin();                // loads the NVS calibration, which the card may replace
  storage_mount();
  settings_loadSd();            // /config.json wins over NVS

  display_begin();              // backlight still off
  // Finger held from power-up for 8 s: switch ST7789 <-> ILI9341 and restart.
  // Before anything is drawn, because it exists for a screen showing nothing.
  display_bootGesture();

  tft.fillScreen(ui::BG);
  display_setBacklight(g_settings.brightness);

  if (!touch_hasStoredCalibration()) calibrate_run();

  if (settings_sdState() == SDCFG_BAD) {
    ui_message("config.json is invalid", "Moved to config.bad - using saved settings", ui::WARN);
    delay(3000);
  }

  keys_configure(g_settings.cols, g_settings.rows);
  portal_begin();
  SatCallbacks cb;
  cb.keyState = onKeyState;
  cb.clearKeys = onClearKeys;
  cb.brightness = onBrightness;
  sat_begin(cb);

  show(SCREEN_STATUS);
  Log.printf("boot    : ready, free heap %u\n", ESP.getFreeHeap());
}

void loop() {
  portal_tick();
  sat_tick();
  settings_tick();

  if (screen == SCREEN_MENU) {
    menu_tick();
    if (menu_isOpen()) return;
    // Closed: back to whatever the connection state calls for. A key that
    // could not be cached is fetched again by reconnecting.
    screen = SCREEN_NONE;
    if (sat_state() == SAT_READY && !keys_complete()) sat_refresh();
    wasDown = touch_isDown();
  }

  handleTouch();
  if (screen == SCREEN_MENU) return;

  if (sat_state() == SAT_READY) {
    show(SCREEN_KEYS);
  } else {
    if (screen == SCREEN_KEYS) releaseKey();
    show(SCREEN_STATUS);
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 250) {   // it rebuilds its strings to compare
      lastStatus = millis();
      status_draw();                     // repaints only on change
    }
  }
  if (!sat_busy()) delay(1);   // idle: give the CPU back; busy: keep reading
}
