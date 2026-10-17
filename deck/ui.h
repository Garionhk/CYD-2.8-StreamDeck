// ===========================================================================
// ui.h -- the few drawing helpers every screen shares
// ===========================================================================
// The MP3 player has themes, skins and a translated label system; the deck's
// screens are the status page, the device menu and the calibration wizard, so
// one fixed dark palette and plain TFT_eSPI fonts are enough.
#pragma once

#include <Arduino.h>
#include "display.h"

namespace ui {
static const uint16_t BG     = TFT_BLACK;
static const uint16_t PANEL  = 0x2124;   // dark grey
static const uint16_t EDGE   = 0x4208;   // mid grey
static const uint16_t TEXT   = TFT_WHITE;
static const uint16_t DIM    = 0x9CF3;   // light grey
static const uint16_t ACCENT = 0x05FF;   // azure
static const uint16_t GOOD   = 0x07E8;   // green
static const uint16_t WARN   = 0xFD20;   // orange
static const uint16_t BAD    = 0xF8A2;   // red
}

struct Rect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Clear the screen and centre one or two lines on it.
void ui_message(const String& line1, const String& line2 = String(), uint16_t colour = ui::TEXT);

// A rounded button with a centred label (font 2). lit = drawn as pressed.
void ui_button(const Rect& r, const char* label, bool lit = false);

// A QR code for text, black on white with its quiet zone, as large as fits in
// a maxPx square centred on (cx, cy). Returns false if it would not encode.
bool ui_drawQr(const String& text, int cx, int cy, int maxPx);

// "#rrggbb" or "rgb(r,g,b)" -> RGB565. Returns fallback when it parses as neither.
uint16_t ui_parseColour(const char* s, uint16_t fallback);
