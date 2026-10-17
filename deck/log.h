// ===========================================================================
// log.h -- the serial log, also kept in memory and served at /log
// ===========================================================================
// This board restarts whenever its USB serial port is opened (the CH340's
// DTR/RTS lines drive EN and IO0), so anything printed before you connect is
// lost. Log.printf() writes to Serial as usual AND to a 4 KB ring buffer, which
// the setup web server returns at http://<deck>/log -- the recent log, read
// over WiFi, without touching the board.
//
// The deck's own modules log through this. The drivers copied from the MP3
// player (display, touch, calibrate, board.h) still print to Serial only.
#pragma once

#include <Arduino.h>

class DeckLog : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  String recent() const;     // oldest first, each line prefixed with uptime
};

extern DeckLog Log;
