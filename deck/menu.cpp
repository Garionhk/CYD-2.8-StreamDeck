#include "menu.h"
#include "calibrate.h"
#include "portal.h"
#include "satellite.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"

static const uint32_t IDLE_CLOSE_MS = 10000;
static const uint32_t SETUP_IDLE_MS = 60000;   // the QR page: time to find a phone
static const int      STEP = 10;

static const Rect R_MINUS  = {   8, 58,  48, 44 };
static const Rect R_BAR    = {  66, 64, 188, 32 };
static const Rect R_PLUS   = { 264, 58,  48, 44 };
static const Rect R_RECAL  = {   8, 186, 71, 46 };
static const Rect R_FLIP   = {  85, 186, 71, 46 };
static const Rect R_WEB    = { 162, 186, 71, 46 };
static const Rect R_CLOSE  = { 239, 186, 73, 46 };
static const Rect R_CLEARPW = {  8, 196, 150, 38 };   // on the Web setup screen

enum Page : uint8_t { PAGE_MAIN, PAGE_SETUP };

static bool     menuOpen = false;
static Page     page = PAGE_MAIN;
static bool     armed = false;     // false until the finger that opened us lifts
static bool     dragging = false;
static uint32_t lastActivity = 0;
static bool     clearArmed = false;   // "Clear password" tapped once: tap again to do it

// ---------------------------------------------------------------------------
// Main page
// ---------------------------------------------------------------------------
static void drawBar() {
  const int pct = g_settings.brightness;
  const int fillW = (R_BAR.w - 4) * (pct - BRIGHTNESS_MIN) / (100 - BRIGHTNESS_MIN);
  tft.fillRoundRect(R_BAR.x, R_BAR.y, R_BAR.w, R_BAR.h, 6, ui::PANEL);
  tft.drawRoundRect(R_BAR.x, R_BAR.y, R_BAR.w, R_BAR.h, 6, ui::EDGE);
  if (fillW > 0) tft.fillRoundRect(R_BAR.x + 2, R_BAR.y + 2, fillW, R_BAR.h - 4, 5, ui::ACCENT);
  tft.fillRect(200, 32, 112, 20, ui::BG);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(ui::TEXT, ui::BG);
  tft.drawString(String(pct) + " %", 312, 36, 2);
}

static void drawInfo() {
  // Padding instead of a clear first: redrawn every second, so no flicker.
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::DIM, ui::BG);
  tft.setTextPadding(tft.width() - 12);
  const String url = portal_url();
  tft.drawString("Setup page  " + (url.length() ? url : String("(no network)")), 12, 116, 2);
  tft.drawString("Companion  " + (g_settings.host.length() ? g_settings.host : String("not set")) +
                 " - " + sat_stateText(), 12, 136, 2);
  tft.drawString(String("Panel ") + settings_panelName(g_settings.panel) + "  Layout " +
                 settings_layoutName() + "  " + settings_sdStateText(), 12, 156, 2);
  tft.setTextPadding(0);
}

static void drawMain() {
  tft.fillScreen(ui::BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::ACCENT, ui::BG);
  tft.drawString("Device menu", 12, 8, 4);
  tft.setTextColor(ui::DIM, ui::BG);
  tft.drawString("Brightness", 12, 36, 2);
  ui_button(R_MINUS, "-");
  ui_button(R_PLUS, "+");
  drawBar();
  drawInfo();
  ui_button(R_RECAL, "Calibrate");
  ui_button(R_FLIP, "Flip 180");
  ui_button(R_WEB, "Web setup");
  ui_button(R_CLOSE, "Close");
}

// ---------------------------------------------------------------------------
// Web setup page: a QR code to scan instead of typing an address
// ---------------------------------------------------------------------------
static void drawSetup() {
  tft.fillScreen(ui::BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::ACCENT, ui::BG);
  tft.drawString("Web setup", 12, 8, 4);

  // On the hotspot a phone has to join it first: the QR is the WiFi join code
  // (iOS and Android camera apps both read "WIFI:"), and the captive page then
  // opens by itself. On the LAN it is the page address.
  const bool hotspotOnly = portal_apActive() && !portal_wifiUp();
  const String url = portal_url();
  String qr, l1, l2, l3;
  if (hotspotOnly) {
    qr = "WIFI:T:nopass;S:" + portal_apName() + ";;";
    l1 = "1. Scan to join WiFi";
    l2 = "   " + portal_apName();
    l3 = "2. Open " + url;
  } else if (url.length()) {
    qr = url;
    l1 = "Scan, or open:";
    l2 = url;
    l3 = portal_mdnsUrl();
  }

  tft.setTextColor(ui::TEXT, ui::BG);
  if (qr.isEmpty()) {
    tft.drawString("No network yet.", 12, 60, 2);
    tft.setTextColor(ui::DIM, ui::BG);
    tft.drawString("Edit config.json on the SD card, or", 12, 84, 2);
    tft.drawString("wait for the setup hotspot.", 12, 102, 2);
  } else {
    ui_drawQr(qr, 240, 128, 150);
    tft.drawString(l1, 12, 56, 2);
    tft.setTextColor(ui::ACCENT, ui::BG);
    tft.drawString(l2, 12, 78, 2);
    tft.drawString(l3, 12, 100, 2);
    tft.setTextColor(ui::DIM, ui::BG);
    tft.drawString("Same WiFi as this deck.", 12, 134, 2);
    if (portal_hasPassword()) {
      tft.drawString("Login: admin + password", 12, 152, 2);
    } else {
      tft.setTextColor(ui::WARN, ui::BG);
      tft.drawString("No password set yet", 12, 152, 2);
    }
  }
  // Forgotten password: whoever is holding the deck is its owner.
  if (portal_hasPassword()) ui_button(R_CLEARPW, clearArmed ? "Tap again to clear" : "Clear password", clearArmed);
  tft.setTextColor(ui::DIM, ui::BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Tap elsewhere to go back", portal_hasPassword() ? 166 : 12, 216, 1);
}

// ---------------------------------------------------------------------------
static void setBrightness(int pct) {
  pct = constrain(pct, (int)BRIGHTNESS_MIN, 100);
  if (pct == g_settings.brightness) return;
  g_settings.brightness = pct;
  display_setBacklight(pct);
  drawBar();
  settings_saveSoon();     // written to NVS + SD once the finger settles
}

void menu_open() {
  menuOpen = true;
  page = PAGE_MAIN;
  armed = false;
  dragging = false;
  lastActivity = millis();
  drawMain();
}

bool menu_isOpen() { return menuOpen; }

void menu_tick() {
  if (!menuOpen) return;
  const uint32_t now = millis();

  int x, y;
  const TouchEvent ev = touch_poll(&x, &y);

  // Ignore the long press that opened the menu until that finger lifts.
  if (!armed) {
    if (!touch_isDown()) armed = true;
    return;
  }

  if (page == PAGE_SETUP) {
    if (ev != TOUCH_NONE) {
      lastActivity = now;
      if (portal_hasPassword() && R_CLEARPW.contains(x, y)) {
        if (clearArmed) {
          portal_clearPassword();
          clearArmed = false;
        } else {
          clearArmed = true;
        }
        drawSetup();
        return;
      }
      clearArmed = false;
      page = PAGE_MAIN;
      drawMain();
    } else if (now - lastActivity > SETUP_IDLE_MS) {
      menuOpen = false;
    }
    return;
  }

  if (touch_isDown()) {
    lastActivity = now;
    int lx, ly, px, py;
    touch_livePosition(lx, ly);
    touch_position(px, py);
    // A drag that started on the bar moves it; the bar's ends map to 10 / 100 %.
    if (dragging || R_BAR.contains(px, py)) {
      dragging = true;
      const int pct = BRIGHTNESS_MIN + (lx - R_BAR.x) * (100 - BRIGHTNESS_MIN) / R_BAR.w;
      setBrightness((pct + 2) / 5 * 5);
    }
    return;
  }

  if (ev != TOUCH_NONE) {
    lastActivity = now;
    if (dragging) {
      dragging = false;
    } else if (R_MINUS.contains(x, y)) {
      setBrightness(g_settings.brightness - STEP);
    } else if (R_PLUS.contains(x, y)) {
      setBrightness(g_settings.brightness + STEP);
    } else if (R_RECAL.contains(x, y)) {
      calibrate_run();
      drawMain();
    } else if (R_FLIP.contains(x, y)) {
      g_settings.rotation = g_settings.rotation == 1 ? 3 : 1;
      display_setRotation(g_settings.rotation);
      settings_save();
      drawMain();
    } else if (R_WEB.contains(x, y)) {
      page = PAGE_SETUP;
      clearArmed = false;
      drawSetup();
      return;
    } else if (R_CLOSE.contains(x, y)) {
      menuOpen = false;
      return;
    }
  }

  // The status lines change underneath (a reconnect, say): refresh each second.
  static uint32_t lastInfo = 0;
  if (now - lastInfo > 1000) {
    lastInfo = now;
    drawInfo();
  }

  if (now - lastActivity > IDLE_CLOSE_MS) menuOpen = false;
}
