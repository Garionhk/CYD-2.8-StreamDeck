#include "satellite.h"
#include "log.h"
#include "keys.h"
#include "settings.h"
#include "ui.h"
#include <WiFi.h>

// Everything in a line except the BITMAP value, which is streamed (see pump()).
// What is left is ~200 bytes of fields plus a base64 label: 2 KB is plenty.
static const size_t   BUF_SIZE        = 2 * 1024;
static const uint32_t CONNECT_TIMEOUT = 3000;    // ms, per attempt
static const uint32_t HANDSHAKE_MS    = 5000;    // BEGIN + ADD-DEVICE OK must arrive by then
static const uint32_t PING_EVERY_MS   = 2000;
static const uint32_t SILENCE_MS      = 6000;    // no bytes at all for this long: dead link
static const uint32_t BACKOFF_MIN_MS  = 1000;
static const uint32_t BACKOFF_MAX_MS  = 10000;

static WiFiClient   client;
static SatCallbacks cb;
static SatState     state = SAT_OFF;
static String       lastError;
static String       devId;

static char     buf[BUF_SIZE];
static size_t   len = 0;
static bool     overflow = false;     // discarding the rest of a line too long to hold

// Inside a BITMAP value: its characters bypass buf and go to the decoder.
static bool     inBitmap = false;
static bool     bitmapQuoted = false; // newer Companion quotes it, 3.0 does not
static bool     bitmapStarted = false;// past the opening quote (if any)
static bool     bitmapSkip = false;   // not ours to draw: read past it
static uint32_t b64acc = 0;
static int      b64bits = 0;
static int      bitmapSide = 72;      // what Companion will send, from its API version

static uint32_t nextAttemptAt = 0, backoffMs = BACKOFF_MIN_MS;

// A burst of KEY-STATEs (a page change, a reconnect): timed and logged as one
// line, "32 key images, 674 KB in 2.9 s", once it has been quiet for 400 ms.
static uint32_t burstStart = 0, burstLast = 0, burstBytes = 0;
static uint32_t lastPumpAt = 0, worstGap = 0;   // loop() stalls between reads
static int      burstKeys = 0;
static uint32_t connectedAt = 0, lastRxAt = 0, lastPingAt = 0;

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------
static void sendLine(const String& line) {
  if (!client.connected()) return;
  client.print(line);
  client.print('\n');
}

static void sendAddDevice() {
  String m = "ADD-DEVICE DEVICEID=\"" + devId + "\"";
  m += " PRODUCT_NAME=\"" + g_settings.name + "\"";
  m += " KEYS_TOTAL=" + String(keys_count());
  m += " KEYS_PER_ROW=" + String(keys_cols());
  // Raw RGB888 at the size we draw. Companion 3.0 reads this as a plain "yes"
  // and sends 72x72; keys.cpp scales whatever arrives.
  m += " BITMAPS=" + String(keys_imageSize());
  m += " COLORS=hex TEXT=1 BRIGHTNESS=1";
  sendLine(m);
  Log.printf("sat     : -> ADD-DEVICE %s (%d keys, %d per row, %d px)\n", devId.c_str(),
                keys_count(), keys_cols(), keys_imageSize());
}

void sat_keyPress(int key, bool down) {
  if (state != SAT_READY || key < 0 || key >= keys_count()) return;
  sendLine("KEY-PRESS DEVICEID=\"" + devId + "\" KEY=" + String(key) + " PRESSED=" + (down ? "1" : "0"));
  Log.printf("sat     : -> KEY-PRESS key %d (row %d, col %d) %s\n", key, key / keys_cols(),
                key % keys_cols(), down ? "down" : "up");
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------
static void resetLine() {
  len = 0;
  overflow = false;
  if (inBitmap && !bitmapSkip) keys_streamEnd();
  inBitmap = false;
}

static void drop(const String& why, uint32_t retryInMs) {
  client.stop();
  resetLine();
  state = SAT_CONNECTING;
  nextAttemptAt = millis() + retryInMs;
  if (why.length()) {
    lastError = why;
    Log.printf("sat     : %s -- retry in %lu ms\n", why.c_str(), (unsigned long)retryInMs);
  }
}

static void backoff(const String& why) {
  drop(why, backoffMs);
  backoffMs = min(backoffMs * 2, BACKOFF_MAX_MS);
}

static void tryConnect() {
  const String& host = g_settings.host;
  Log.printf("sat     : connecting to %s:%u\n", host.c_str(), g_settings.port);
  if (!client.connect(host.c_str(), g_settings.port, CONNECT_TIMEOUT)) {
    backoff("cannot reach " + host + ":" + String(g_settings.port));
    return;
  }
  client.setNoDelay(true);       // a key press is one short line: send it now
  resetLine();
  state = SAT_HANDSHAKE;
  connectedAt = lastRxAt = lastPingAt = millis();
}

void sat_refresh() {
  if (state == SAT_HANDSHAKE || state == SAT_READY) drop("", 0);
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
// "KEY=VALUE KEY="quoted value" WORD" -- split in place. Companion never puts
// a quote inside a quoted value (text and bitmaps are base64), so the first
// closing quote ends it.
struct Params {
  static const int MAX = 16;
  const char* key[MAX];
  char*       val[MAX];
  const char* word[4];     // bare tokens after the command: OK / ERROR
  int n = 0, words = 0;

  char* get(const char* k) const {
    for (int i = 0; i < n; i++)
      if (strcmp(key[i], k) == 0) return val[i];
    return nullptr;
  }
  bool has(const char* w) const {
    for (int i = 0; i < words; i++)
      if (strcmp(word[i], w) == 0) return true;
    return false;
  }
};

static void parseParams(char* p, Params& out) {
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    char* tok = p;
    while (*p && *p != ' ' && *p != '=') p++;
    if (*p != '=') {                         // a bare word
      if (*p) *p++ = 0;
      if (out.words < 4) out.word[out.words++] = tok;
      continue;
    }
    *p++ = 0;                                // end of the key
    char* v = p;
    if (*p == '"') {
      v = ++p;
      while (*p && *p != '"') p++;
    } else {
      while (*p && *p != ' ') p++;
    }
    if (*p) *p++ = 0;
    if (out.n < Params::MAX) {
      out.key[out.n] = tok;
      out.val[out.n] = v;
      out.n++;
    }
  }
}

// Base64 -> bytes. In place for the label: safe because every 4 characters
// read produce at most 3 bytes written, so writing never overtakes reading.
static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static size_t b64decodeInPlace(char* s) {
  uint8_t* out = (uint8_t*)s;
  uint32_t acc = 0;
  int bits = 0;
  size_t n = 0;
  for (const char* p = s; *p && *p != '='; p++) {
    const int v = b64val(*p);
    if (v < 0) continue;
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[n++] = (acc >> bits) & 0xFF;
    }
  }
  return n;
}

static void onKeyState(const Params& p) {
  const char* keyStr = p.get("KEY");
  if (!keyStr || !cb.keyState) return;
  const int key = atoi(keyStr);

  const uint16_t bg = ui_parseColour(p.get("COLOR"), ui::BG);
  const uint16_t fg = ui_parseColour(p.get("TEXTCOLOR"), ui::TEXT);

  String text;
  if (char* t = p.get("TEXT")) {
    const size_t n = b64decodeInPlace(t);
    text = String(t, n);
  }

  // pump() streamed the bitmap already and left "BITMAP=@" behind if it was good.
  const char* b = p.get("BITMAP");
  const bool image = b && b[0] == '@';
  cb.keyState(key, image, bg, fg, text);
}

static void handleLine(char* line) {
  // Command word.
  char* rest = line;
  while (*rest && *rest != ' ') rest++;
  if (*rest) *rest++ = 0;
  const char* cmd = line;

  if (strcmp(cmd, "PING") == 0) {
    // Echo the payload untouched (before parsing splits it up).
    sendLine(String("PONG ") + rest);
    return;
  }

  Params p;
  parseParams(rest, p);

  if (strcmp(cmd, "PONG") == 0) {
    // Our own keep-alive answered; lastRxAt is already updated.
  } else if (strcmp(cmd, "KEY-STATE") == 0) {
    if (state == SAT_READY) onKeyState(p);
    burstKeys++;
    burstLast = millis();
  } else if (strcmp(cmd, "KEYS-CLEAR") == 0) {
    if (cb.clearKeys) cb.clearKeys();
  } else if (strcmp(cmd, "BRIGHTNESS") == 0) {
    if (const char* v = p.get("VALUE"))
      if (cb.brightness) cb.brightness(atoi(v));
  } else if (strcmp(cmd, "BEGIN") == 0) {
    const char* ver = p.get("CompanionVersion");
    const char* api = p.get("ApiVersion");
    Log.printf("sat     : <- BEGIN Companion %s, API %s\n", ver ? ver : "?", api ? api : "?");
    // API 1.5 added BITMAPS=<size>; before that every bitmap is 72x72.
    int major = 0, minor = 0;
    if (api) sscanf(api, "%d.%d", &major, &minor);
    bitmapSide = (major > 1 || (major == 1 && minor >= 5)) ? keys_imageSize() : 72;
    sendAddDevice();
  } else if (strcmp(cmd, "ADD-DEVICE") == 0) {
    if (p.has("OK")) {
      state = SAT_READY;
      backoffMs = BACKOFF_MIN_MS;
      lastError = "";
      Log.printf("sat     : <- ADD-DEVICE OK -- registered (free heap %u)\n", ESP.getFreeHeap());
      // Companion sends every key fresh after this; start from a clean slate.
      if (cb.clearKeys) cb.clearKeys();
    } else {
      const char* msg = p.get("MESSAGE");
      // "Device exists elsewhere" after a reboot is our own previous
      // connection, which Companion only lets go after its 5 s socket timeout:
      // worth retrying just after that. Other refusals will not change soon.
      const bool stale = msg && (strstr(msg, "exists") || strstr(msg, "already"));
      drop(String("Companion refused: ") + (msg ? msg : "unknown error"),
           stale ? 6000 : BACKOFF_MAX_MS);
    }
  } else if (strcmp(cmd, "KEY-PRESS") == 0) {
    // Companion acknowledges every press (seen from 3.0 and 5.0).
    if (p.has("ERROR")) {
      const char* msg = p.get("MESSAGE");
      Log.printf("sat     : <- KEY-PRESS ERROR %s\n", msg ? msg : "");
    } else {
      Log.println("sat     : <- KEY-PRESS OK");
    }
  } else if (strcmp(cmd, "ERROR") == 0) {
    const char* msg = p.get("MESSAGE");
    Log.printf("sat     : <- ERROR %s\n", msg ? msg : "");
  }
  // CAPS, LOCKED-STATE, VARIABLE-VALUE, DEVICE-CONFIG ...: not used by a plain key grid.
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------
// The line is " BITMAP=" so far: start streaming its value.
static void beginBitmap() {
  inBitmap = true;
  bitmapStarted = false;
  bitmapQuoted = false;
  b64acc = 0;
  b64bits = 0;
  // Only a KEY-STATE we are registered for, with its KEY already seen (it
  // always comes first). Anything else is read past.
  bitmapSkip = true;
  if (state == SAT_READY && len > 10 && strncmp(buf, "KEY-STATE ", 10) == 0) {
    buf[len] = 0;
    if (const char* k = strstr(buf, " KEY="))
      bitmapSkip = !keys_streamBegin(atoi(k + 5), bitmapSide);
  }
}

// One character of a BITMAP value. Returns false at the character that ends
// it, which the caller then handles as ordinary line text.
static bool bitmapChar(char c) {
  if (!bitmapStarted) {
    bitmapStarted = true;
    if (c == '"') { bitmapQuoted = true; return true; }
  }
  const bool end = bitmapQuoted ? (c == '"' || c == '\n') : (c == ' ' || c == '\n' || c == '\r');
  if (end) {
    bool ok = false;
    if (!bitmapSkip) ok = keys_streamEnd();
    inBitmap = false;
    // Leave a marker where the value was, for onKeyState().
    if (ok && len < BUF_SIZE - 1) buf[len++] = '@';
    return c == '"';                       // a closing quote is consumed; ' ' / '\n' are line text
  }
  if (bitmapSkip) return true;
  if (c == ':') {
    // "data:image/png;base64,..." -- we asked for raw RGB, so this is not
    // something we can draw. Read past it; the key gets colour + text.
    keys_streamEnd();
    bitmapSkip = true;
    return true;
  }
  const int v = b64val(c);
  if (v < 0) return true;                  // '=' padding
  b64acc = (b64acc << 6) | v;
  b64bits += 6;
  if (b64bits >= 8) {
    b64bits -= 8;
    const uint8_t byte = (b64acc >> b64bits) & 0xFF;
    keys_streamBytes(&byte, 1);
  }
  return true;
}

static void pump() {
  const uint32_t t = millis();
  if (burstBytes && t - lastPumpAt > worstGap) worstGap = t - lastPumpAt;
  lastPumpAt = t;
  // Read for up to 20 ms per call: long enough to keep the TCP window drained
  // (lwIP's is only ~5.7 KB, so a reader that pauses throttles the sender),
  // short enough that touch keeps responding during a page of images.
  const uint32_t until = millis() + 20;
  uint8_t chunk[1024];
  while ((int32_t)(millis() - until) < 0) {
    const int avail = client.available();
    if (avail <= 0) break;
    const int got = client.read(chunk, min((size_t)avail, sizeof(chunk)));
    if (got <= 0) break;
    lastRxAt = millis();
    if (burstKeys == 0 && burstBytes == 0) burstStart = lastRxAt;
    burstBytes += got;

    for (int i = 0; i < got; i++) {
      const char c = (char)chunk[i];
      if (inBitmap && bitmapChar(c)) continue;

      if (c == '\n') {
        if (len && buf[len - 1] == '\r') len--;
        buf[len] = 0;
        const bool skip = overflow;
        resetLine();
        if (skip) continue;
        handleLine(buf);
        if (state < SAT_HANDSHAKE) return;     // handleLine dropped the connection
        continue;
      }
      if (overflow) continue;
      if (len >= BUF_SIZE - 1) {
        // A line longer than the buffer (a huge label): skip to its end.
        Log.println("sat     : line too long -- skipped");
        overflow = true;
        continue;
      }
      buf[len++] = c;
      if (c == '=' && len >= 8 && memcmp(buf + len - 8, " BITMAP=", 8) == 0) beginBitmap();
    }
  }
}

// ---------------------------------------------------------------------------
void sat_begin(const SatCallbacks& callbacks) {
  cb = callbacks;
  devId = settings_deviceId();
  state = g_settings.host.length() ? SAT_WAIT_WIFI : SAT_OFF;
  Log.printf("sat     : device id %s, Companion %s:%u\n", devId.c_str(),
                g_settings.host.length() ? g_settings.host.c_str() : "(not set)", g_settings.port);
}

void sat_tick() {
  if (state == SAT_OFF) return;

  if (WiFi.status() != WL_CONNECTED) {
    if (state == SAT_HANDSHAKE || state == SAT_READY) {
      client.stop();
      Log.println("sat     : WiFi lost");
    }
    state = SAT_WAIT_WIFI;
    return;
  }

  uint32_t now = millis();
  switch (state) {
    case SAT_WAIT_WIFI:
      state = SAT_CONNECTING;
      nextAttemptAt = now;
      break;

    case SAT_CONNECTING:
      if ((int32_t)(now - nextAttemptAt) >= 0) tryConnect();
      break;

    case SAT_HANDSHAKE:
    case SAT_READY:
      if (!client.connected()) {
        backoff("Companion closed the connection");
        break;
      }
      pump();
      if (state < SAT_HANDSHAKE) break;
      // Fresh time: pump() stamps lastRxAt AFTER `now` was taken above, and
      // `now - lastRxAt` would wrap to ~49 days and read as a dead link.
      now = millis();
      if (burstKeys && now - burstLast > 400) {
        const uint32_t ms = max<uint32_t>(1, burstLast - burstStart);
        Log.printf("sat     : %d key images, %lu KB in %lu.%01lu s (%lu KB/s), longest loop gap %lu ms\n",
                   burstKeys, (unsigned long)(burstBytes / 1024), (unsigned long)(ms / 1000),
                   (unsigned long)(ms % 1000 / 100), (unsigned long)(burstBytes / ms),
                   (unsigned long)worstGap);
        burstKeys = 0;
        burstBytes = 0;
        worstGap = 0;
      } else if (!burstKeys && now - lastRxAt > 400) {
        burstBytes = 0;   // pings and acks between bursts are not counted
        worstGap = 0;
      }
      if (state == SAT_HANDSHAKE && now - connectedAt > HANDSHAKE_MS) {
        backoff("no answer -- is this Companion, with the Satellite API on?");
      } else if (now - lastRxAt > SILENCE_MS) {
        backoff("Companion stopped answering");
      } else if (now - lastPingAt >= PING_EVERY_MS) {
        lastPingAt = now;
        sendLine("PING deck");
      }
      break;

    default:
      break;
  }
}

SatState sat_state() { return state; }
const String& sat_lastError() { return lastError; }

const char* sat_stateText() {
  switch (state) {
    case SAT_OFF:        return g_settings.host.length() ? "stopped" : "Companion IP not set";
    case SAT_WAIT_WIFI:  return "waiting for WiFi";
    case SAT_CONNECTING: return "connecting";
    case SAT_HANDSHAKE:  return "registering";
    case SAT_READY:      return "connected";
  }
  return "";
}

bool sat_busy() {
  return (state == SAT_HANDSHAKE || state == SAT_READY) && client.available() > 0;
}
