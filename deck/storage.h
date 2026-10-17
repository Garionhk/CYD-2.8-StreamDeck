// ===========================================================================
// storage.h -- the SD card, on the hardware VSPI bus
// ===========================================================================
// Trimmed from the MP3 player's storage.cpp: the mount only. The card holds
// one file this firmware cares about, /config.json (settings.h).
//
// SPI layout as in board.h: the display has HSPI (USE_HSPI_PORT), the card
// gets VSPI, the touch controller is bit-banged on its own pins.
#pragma once

#include <Arduino.h>

bool storage_mount();      // false with no card (or an unreadable one)
bool storage_ok();
