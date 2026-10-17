#include "status.h"
#include "portal.h"
#include "satellite.h"
#include "settings.h"
#include "ui.h"
#include <WiFi.h>

static String shown;   // what is on screen now, as one string

static int row(int y, const char* label, const String& value, uint16_t colour = ui::TEXT) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(ui::DIM, ui::BG);
  tft.drawString(label, 12, y, 2);
  tft.setTextColor(colour, ui::BG);
  // Long values (an error message) wrap onto a second line.
  const int x = 96, w = tft.width() - x - 8;
  String v = value;
  if (tft.textWidth(v, 2) <= w) {
    tft.drawString(v, x, y, 2);
    return y + 18;
  }
  int cut = v.length();
  while (cut > 0 && tft.textWidth(v.substring(0, cut), 2) > w) cut = v.lastIndexOf(' ', cut - 1);
  if (cut <= 0) cut = v.length() / 2;
  tft.drawString(v.substring(0, cut), x, y, 2);
  String rest = v.substring(cut);
  rest.trim();
  while (rest.length() && tft.textWidth(rest, 2) > w) rest.remove(rest.length() - 1);
  tft.drawString(rest, x, y + 16, 2);
  return y + 34;
}

void status_draw(bool force) {
  const Settings& s = g_settings;

  String wifi;
  uint16_t wifiColour = ui::TEXT;
  if (portal_wifiUp()) { wifi = s.wifiSsid + "  " + WiFi.localIP().toString(); wifiColour = ui::GOOD; }
  else if (s.wifiSsid.length()) { wifi = "joining " + s.wifiSsid + "..."; wifiColour = ui::WARN; }
  else { wifi = "not set up"; wifiColour = ui::WARN; }

  const String companion = s.host.length() ? s.host + ":" + String(s.port) : String("not set");
  String state = sat_stateText();
  if (sat_state() != SAT_READY && sat_lastError().length()) state += " - " + sat_lastError();
  const uint16_t stateColour = sat_state() == SAT_READY ? ui::GOOD
                             : sat_state() == SAT_OFF   ? ui::WARN : ui::TEXT;
  const String source = settings_sdStateText();
  const String url = portal_url();

  const String now = wifi + "|" + companion + "|" + state + "|" + source + "|" + url + "|" +
                     portal_mdnsUrl() + "|" +
                     (portal_apActive() ? "ap" : "");
  if (!force && now == shown) return;
  shown = now;

  tft.fillScreen(ui::BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(ui::ACCENT, ui::BG);
  tft.drawString("CYD Stream Deck", tft.width() / 2, 10, 4);

  int y = 46;
  y = row(y, "WiFi", wifi, wifiColour);
  y = row(y, "Companion", companion);
  y = row(y, "Status", state, stateColour);
  y = row(y, "Settings", source, settings_sdState() == SDCFG_BAD ? ui::WARN : ui::TEXT);
  y = row(y, "Device id", settings_deviceId());

  // How to reach the setup page: the hotspot first, it is what a new owner needs.
  const Rect box = { 8, 168, (int16_t)(tft.width() - 16), 50 };
  tft.fillRoundRect(box.x, box.y, box.w, box.h, 8, ui::PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ui::TEXT, ui::PANEL);
  if (portal_apActive() && !portal_wifiUp()) {
    tft.drawString("Join WiFi \"" + portal_apName() + "\"", box.x + box.w / 2, box.y + 15, 2);
    tft.drawString("then open " + url, box.x + box.w / 2, box.y + 35, 2);
  } else if (url.length()) {
    tft.drawString("Setup page  " + url, box.x + box.w / 2, box.y + 15, 2);
    const String name = portal_mdnsUrl();
    tft.drawString(name.length() ? name : String("(same WiFi as the deck)"), box.x + box.w / 2,
                   box.y + 35, 2);
  } else {
    tft.drawString("Or edit config.json on the SD card", box.x + box.w / 2, box.y + 25, 2);
  }
  tft.setTextColor(ui::DIM, ui::BG);
  tft.drawString("Hold the screen 3 s for the menu", tft.width() / 2, 230, 1);
}
