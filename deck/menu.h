// ===========================================================================
// menu.h -- the device's own menu: brightness, orientation, touch calibration
// ===========================================================================
// Every key on the grid belongs to Companion, so the deck's own controls sit
// behind a hold: keep a finger down anywhere for 3 s. The key under it gets
// its release first, so Companion never sees a key stuck down.
//
// The satellite connection keeps running while the menu is up; key updates
// land in the key cache and show when the menu closes.
#pragma once

void menu_open();
bool menu_isOpen();
void menu_tick();      // call every loop() while open; closes itself after 10 s idle
