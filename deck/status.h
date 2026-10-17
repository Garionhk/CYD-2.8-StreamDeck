// ===========================================================================
// status.h -- the screen shown whenever the key grid is not
// ===========================================================================
// WiFi, Companion and settings state, plus how to reach the setup page.
// status_draw() repaints only when something on it changed, so loop() can
// call it freely.
#pragma once

void status_draw(bool force = false);
