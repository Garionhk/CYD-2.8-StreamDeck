#include "ui.h"
#include <qrcode.h>   // ESP-IDF's encoder, shipped with the esp32 core

void ui_message(const String& line1, const String& line2, uint16_t colour) {
  tft.fillScreen(ui::BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colour, ui::BG);
  const int cx = tft.width() / 2, cy = tft.height() / 2;
  tft.drawString(line1, cx, line2.length() ? cy - 14 : cy, 4);
  if (line2.length()) {
    tft.setTextColor(ui::DIM, ui::BG);
    tft.drawString(line2, cx, cy + 18, 2);
  }
}

void ui_button(const Rect& r, const char* label, bool lit) {
  const uint16_t fill = lit ? ui::ACCENT : ui::PANEL;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, fill);
  tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, lit ? ui::TEXT : ui::EDGE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ui::TEXT, fill);
  tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
}

uint16_t ui_parseColour(const char* s, uint16_t fallback) {
  if (!s) return fallback;
  unsigned r, g, b;
  if (s[0] == '#' && strlen(s) >= 7) {
    const uint32_t v = strtoul(s + 1, nullptr, 16);
    r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
  } else if (sscanf(s, "rgb(%u,%u,%u)", &r, &g, &b) != 3) {
    return fallback;
  }
  return tft.color565(r, g, b);
}

// esp_qrcode_generate() hands the result to a callback with no user pointer,
// so the drawing target travels in these.
static int  qrCx, qrCy, qrMax;
static bool qrDrawn;

static void drawQrCallback(esp_qrcode_handle_t qr) {
  const int n = esp_qrcode_get_size(qr);
  const int quiet = 2;                              // modules of white round it
  const int scale = max(1, qrMax / (n + 2 * quiet));
  const int px = (n + 2 * quiet) * scale;
  const int x0 = qrCx - px / 2, y0 = qrCy - px / 2;
  tft.fillRect(x0, y0, px, px, TFT_WHITE);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(qr, x, y))
        tft.fillRect(x0 + (x + quiet) * scale, y0 + (y + quiet) * scale, scale, scale, TFT_BLACK);
  qrDrawn = true;
}

bool ui_drawQr(const String& text, int cx, int cy, int maxPx) {
  qrCx = cx;
  qrCy = cy;
  qrMax = maxPx;
  qrDrawn = false;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = drawQrCallback;
  cfg.max_qrcode_version = 6;                       // plenty for a URL or WiFi string
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  return esp_qrcode_generate(&cfg, text.c_str()) == ESP_OK && qrDrawn;
}
