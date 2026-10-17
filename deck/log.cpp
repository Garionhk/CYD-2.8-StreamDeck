#include "log.h"

DeckLog Log;

static const size_t RING = 4096;
static char   ring[RING];
static size_t head = 0;        // next write position
static bool   wrapped = false;
static bool   lineStart = true;

static void put(char c) {
  ring[head++] = c;
  if (head == RING) { head = 0; wrapped = true; }
}

size_t DeckLog::write(uint8_t c) {
  Serial.write(c);
  if (lineStart && c != '\n') {
    // "[  123.456] " -- seconds since boot, so a log read later has a timeline.
    char stamp[16];
    const uint32_t ms = millis();
    snprintf(stamp, sizeof(stamp), "[%6lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    for (const char* p = stamp; *p; p++) put(*p);
    lineStart = false;
  }
  put((char)c);
  if (c == '\n') lineStart = true;
  return 1;
}

size_t DeckLog::write(const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) write(buf[i]);
  return n;
}

String DeckLog::recent() const {
  String out;
  out.reserve(RING + 64);
  if (wrapped) {
    // Start after the first newline: the oldest line is cut off at the front.
    size_t i = head;
    while (i < RING && ring[i] != '\n') i++;
    for (i = i + 1; i < RING; i++) out += ring[i];
  }
  for (size_t i = 0; i < head; i++) out += ring[i];
  return out;
}
