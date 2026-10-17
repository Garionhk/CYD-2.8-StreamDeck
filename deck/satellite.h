// ===========================================================================
// satellite.h -- Bitfocus Companion's Satellite API, as a TCP client
// ===========================================================================
// Companion listens on TCP 16622 for "satellite" surfaces: anything that can
// show buttons and report presses. The protocol is plain text, one message
// per line, parameters as KEY=VALUE (strings in double quotes, booleans 1/0):
//
//   Companion -> BEGIN CompanionVersion="4.x" ApiVersion="1.14.0"
//   deck      -> ADD-DEVICE DEVICEID="cyd-a1b2c3" PRODUCT_NAME="CYD Stream Deck"
//                KEYS_TOTAL=32 KEYS_PER_ROW=8 BITMAPS=36 COLORS=hex TEXT=1 BRIGHTNESS=1
//   Companion -> ADD-DEVICE OK DEVICEID="cyd-a1b2c3"
//   Companion -> KEY-STATE DEVICEID=".." KEY=0 TYPE="BUTTON" PRESSED=0
//                BITMAP="<base64 of n*n*3 bytes RGB888>" COLOR="#rrggbb"
//                TEXTCOLOR="#rrggbb" TEXT="<base64 label>"
//   deck      -> KEY-PRESS DEVICEID=".." KEY=0 PRESSED=1      (finger down)
//   deck      -> KEY-PRESS DEVICEID=".." KEY=0 PRESSED=0      (finger up)
//   Companion -> KEYS-CLEAR / BRIGHTNESS VALUE=n / PING x  (answered PONG x)
//
// Checked against bitfocus/companion lib/Service/Satellite/SatelliteApi.ts
// (API 1.14) and 3.0 (API 1.4: BITMAPS is a yes/no there and images are
// always 72x72). A KEY-STATE line is up to ~21 KB because of the bitmap, but
// only its short fields are buffered (2 KB): the BITMAP value is base64-decoded
// byte by byte as it arrives and streamed into keys.cpp, which scales it into
// place. The whole line is never in memory.
//
// Everything runs from sat_tick() in loop(): no task, no locks.
#pragma once

#include <Arduino.h>

enum SatState : uint8_t {
  SAT_OFF = 0,       // not started, or no host configured
  SAT_WAIT_WIFI,     // started, WiFi not up yet
  SAT_CONNECTING,    // waiting out the backoff before the next attempt
  SAT_HANDSHAKE,     // connected, waiting for BEGIN / ADD-DEVICE OK
  SAT_READY          // registered: KEY-STATEs flow, presses are sent
};

struct SatCallbacks {
  // image: this key's bitmap was just streamed into keys.cpp successfully.
  void (*keyState)(int key, bool image, uint16_t bg, uint16_t fg, const String& text) = nullptr;
  void (*clearKeys)() = nullptr;
  void (*brightness)(int percent) = nullptr;
};

void        sat_begin(const SatCallbacks& cb);
void        sat_tick();
SatState    sat_state();
const char* sat_stateText();       // for the status screen
const String& sat_lastError();     // Companion's ERROR MESSAGE, or a socket error

void sat_keyPress(int key, bool down);

// Data is waiting: loop() skips its idle delay so a page of images drains fast.
bool sat_busy();

// Drop the connection and reconnect now: Companion resends every key.
void sat_refresh();
