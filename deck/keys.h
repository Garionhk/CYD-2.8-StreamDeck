// ===========================================================================
// keys.h -- the button grid Companion draws into
// ===========================================================================
// The 320x240 landscape screen is split into cols x rows cells, set by the
// "layout" setting:
//
//   8x4  40x60 cells, 36x36 images -- the whole Companion page (default)
//   5x3  64x80 cells, 56x56 images -- an Elgato Stream Deck MK.2's shape
//   4x3  80x80 cells, 72x72 images -- biggest keys
//
// Companion renders each button itself and sends it as a square RGB888
// bitmap. Newer Companion versions send the size we ask for (ADD-DEVICE
// BITMAPS=n); 3.0 always sends 72x72. Either way it is box-filtered down to
// the cell's image size here, so both look the same.
//
// The bitmap is STREAMED in: satellite.cpp hands over the decoded bytes as
// they come off the socket (keys_stream*), and each finished row of output
// pixels goes straight into the key cache -- or, with no cache, straight to
// the panel. The ~21 KB line it arrives in is never held in memory.
//
// The image sits in its cell with a small border, and that border is where
// the local "pressed" outline goes, so it never overwrites Companion's pixels.
//
// Every key is kept as RGB565 at display size (32 x 2.6 KB for 8x4, 12 x 10 KB
// for 4x3) in one block taken at boot, so the grid can be put back after the
// device menu without asking Companion again. When the heap cannot spare that
// block, keys are drawn and forgotten, keys_complete() is false, and the
// caller reconnects so that Companion resends everything.
//
// keys_configure() allocates that block: call it in setup() BEFORE WiFi starts.
#pragma once

#include <Arduino.h>
#include "ui.h"

static const int KEY_MAX = 32;          // 8 x 4

// Set the grid. Call before anything else in this file; clears every key.
void keys_configure(uint8_t cols, uint8_t rows);
int  keys_cols();
int  keys_rows();
int  keys_count();
int  keys_imageSize();                  // the square each bitmap is scaled to

// A key's bitmap, side x side RGB888, in as many pieces as it arrives in.
// keys_streamBegin() returns false for a key or size it cannot take (the
// caller then skips the bitmap); keys_streamEnd() is true when exactly
// side*side*3 bytes came.
bool keys_streamBegin(int key, int side);
void keys_streamBytes(const uint8_t* bytes, size_t n);
bool keys_streamEnd();

// The rest of a KEY-STATE, once its line has ended. image: a bitmap for this
// key was just streamed in successfully; otherwise the key is drawn as a
// colour fill + text.
void keys_set(int key, bool image, uint16_t bg, uint16_t fg, const String& text);

void keys_clearAll();               // KEYS-CLEAR: every key black

// Whether key updates reach the panel. Off while another screen is up; the
// cache still takes every update so keys_drawAll() shows the latest.
void keys_setVisible(bool on);
bool keys_visible();
void keys_drawAll();
bool keys_complete();               // every key Companion sent is cached

int  keys_hit(int x, int y);        // screen point -> key index, -1 off the grid
void keys_highlight(int key, bool on);
