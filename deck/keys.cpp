#include "keys.h"
#include "log.h"
#include <esp_heap_caps.h>

struct KeySlot {
  bool      image  = false;     // its pixels are in the pool (or were lost)
  bool      known  = false;     // Companion has sent this key since the last clear
  bool      lost   = false;     // sent, but there was no memory to keep it
  uint16_t  bg     = ui::BG;    // fallback drawing when there is no bitmap
  uint16_t  fg     = ui::TEXT;
  String    text;
};

static KeySlot slots[KEY_MAX];
static bool    visible = false;
static int     lit     = -1;

// Geometry, from keys_configure().
static int cols = 4, rows = 3, cellW = 80, cellH = 80, img = 72, padX = 4, padY = 4;

// Largest image any layout uses (4x3): the row buffers are sized for it.
static const int IMG_MAX = 72;

// The key cache is ONE block, taken in setup() before WiFi starts, while the
// heap is still in one piece. Allocating a block per key as images arrived
// interleaved them with WiFi's buffers and left 46 KB free in nothing bigger
// than 4 KB -- the setup page then came back blank.
//
// It is only taken if this much heap is left after it, for WiFi (~50 KB once
// connected), the receive buffer, the web server and DNS. Otherwise there is
// no cache: the grid still works, and closing the device menu asks Companion
// to send the keys again (keys_complete() is false).
static const size_t HEAP_AFTER_POOL = 100 * 1024;
static uint16_t*    pool = nullptr;

static uint16_t* poolFor(int key) { return pool ? pool + (size_t)key * img * img : nullptr; }

void keys_configure(uint8_t c, uint8_t r) {
  keys_clearAll();
  free(pool);
  pool = nullptr;
  cols = constrain(c, 1, 8);
  rows = constrain(r, 1, 4);
  if (cols * rows > KEY_MAX) rows = KEY_MAX / cols;
  cellW = 320 / cols;
  cellH = 240 / rows;
  // A 2 px border on small cells, 4 px on big ones: room for the press outline.
  const int border = min(cellW, cellH) >= 64 ? 4 : 2;
  img = min(min(cellW, cellH) - 2 * border, IMG_MAX);
  padX = (cellW - img) / 2;
  padY = (cellH - img) / 2;

  const size_t bytes = (size_t)keys_count() * img * img * sizeof(uint16_t);
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= bytes &&
      ESP.getFreeHeap() >= bytes + HEAP_AFTER_POOL)
    pool = (uint16_t*)malloc(bytes);
  Log.printf("keys    : %dx%d, %d px images, cache %s (%u KB), heap left %u\n", cols, rows, img,
                pool ? "on" : "OFF", (unsigned)(bytes / 1024), ESP.getFreeHeap());
}

int keys_cols() { return cols; }
int keys_rows() { return rows; }
int keys_count() { return cols * rows; }
int keys_imageSize() { return img; }

static void cellOrigin(int key, int& x, int& y) {
  x = (key % cols) * cellW;
  y = (key / cols) * cellH;
}

// Text-only key: Companion's colours over the whole cell, its label wrapped.
static void drawFallback(int key, const KeySlot& s) {
  int x, y;
  cellOrigin(key, x, y);
  const int inset = padX < padY ? padX : padY;
  const int w = cellW - 2 * inset, h = cellH - 2 * inset;
  x += inset;
  y += inset;
  tft.fillRect(x, y, w, h, s.bg);
  if (s.text.isEmpty()) return;
  const int font = w >= 60 ? 2 : 1;         // font 1 (8 px) on the 8x4 grid
  const int lineH = font == 2 ? 16 : 10;
  const int maxLines = max(1, h / lineH);
  tft.setTextColor(s.fg, s.bg);
  tft.setTextDatum(MC_DATUM);
  // Word-wrap to the cell, keeping Companion's own "\n" breaks.
  String lines[6];
  int n = 0;
  String word, line;
  auto flush = [&]() {
    if (n < maxLines && n < 6) lines[n++] = line;
    line = "";
  };
  for (size_t i = 0; i <= s.text.length(); i++) {
    const char c = i < s.text.length() ? s.text[i] : '\n';
    if (c == ' ' || c == '\n') {
      if (line.length() && tft.textWidth(line + " " + word, font) > w - 2) flush();
      if (line.length()) line += ' ';
      line += word;
      word = "";
      if (c == '\n') flush();
    } else {
      word += c;
    }
  }
  const int top = y + h / 2 - (n * lineH) / 2 + lineH / 2;
  for (int i = 0; i < n; i++) tft.drawString(lines[i], x + w / 2, top + i * lineH, font);
}

static void pushPixels(int key, const uint16_t* px) {
  int x, y;
  cellOrigin(key, x, y);
  tft.setSwapBytes(true);   // the buffer holds native uint16 values
  tft.pushImage(x + padX, y + padY, img, img, px);
  tft.setSwapBytes(false);
}

static void drawKey(int key) {
  const KeySlot& s = slots[key];
  if (s.image && !s.lost) pushPixels(key, poolFor(key));
  else drawFallback(key, s);
}

static void drawOutline(int key, bool on) {
  int x, y;
  cellOrigin(key, x, y);
  const uint16_t c = on ? ui::TEXT : ui::BG;
  // In the border round the image: never touches Companion's pixels.
  tft.drawRoundRect(x, y, cellW, cellH, 5, c);
  tft.drawRoundRect(x + 1, y + 1, cellW - 2, cellH - 2, 4, c);
}

// ---------------------------------------------------------------------------
// Streaming a bitmap in
// ---------------------------------------------------------------------------
// side x side RGB888 -> img x img RGB565 with a box filter (72 -> 36 averages
// 2x2 blocks, so text stays smooth instead of losing strokes), one source
// pixel at a time. Each output row is summed in acc[] while its source rows
// arrive and written out when the last of them is complete.
static const int SIDE_MAX = 144;            // bigger than any Companion sends

static int      stKey = -1, stSide = 0;
static size_t   stBytes = 0;
static uint8_t  stPix[3];                   // a pixel split across two pieces
static int      stPixN = 0;
static int      stX = 0, stY = 0;           // next source pixel
static uint8_t  colOf[SIDE_MAX];            // source column -> output column
static uint8_t  rowOf[SIDE_MAX + 1];        // source row -> output row (+ end marker)
static uint8_t  colN[IMG_MAX], rowN[IMG_MAX];   // source pixels per output column / row
static uint32_t acc[IMG_MAX * 3];
static uint16_t rowOut[IMG_MAX];

bool keys_streamBegin(int key, int side) {
  stKey = -1;
  // Shrinking only: Companion sends the size asked for, or 72 (bigger).
  if (key < 0 || key >= keys_count() || side < img || side > SIDE_MAX) return false;
  stKey = key;
  stSide = side;
  stBytes = 0;
  stPixN = stX = stY = 0;
  memset(colN, 0, sizeof(colN));
  memset(rowN, 0, sizeof(rowN));
  for (int o = 0; o < img; o++) {
    const int from = o * side / img, to = (o + 1) * side / img;
    for (int i = from; i < to; i++) {
      colOf[i] = rowOf[i] = o;
      colN[o]++;
      rowN[o]++;
    }
  }
  rowOf[side] = 0xFF;                       // past the last row
  memset(acc, 0, sizeof(acc));
  return true;
}

static void emitRow(int oy) {
  const uint32_t rn = rowN[oy];
  for (int ox = 0; ox < img; ox++) {
    const uint32_t n = rn * colN[ox];
    const uint32_t r = acc[ox * 3] / n, g = acc[ox * 3 + 1] / n, b = acc[ox * 3 + 2] / n;
    rowOut[ox] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }
  memset(acc, 0, sizeof(uint32_t) * img * 3);
  if (pool) {
    memcpy(poolFor(stKey) + (size_t)oy * img, rowOut, img * sizeof(uint16_t));
  } else if (visible) {
    // No cache: the row goes straight to the panel.
    int x, y;
    cellOrigin(stKey, x, y);
    tft.setSwapBytes(true);
    tft.pushImage(x + padX, y + padY + oy, img, 1, rowOut);
    tft.setSwapBytes(false);
  }
}

static void addPixel(uint8_t r, uint8_t g, uint8_t b) {
  if (stY >= stSide) return;                // more data than a square holds
  uint32_t* a = acc + colOf[stX] * 3;
  a[0] += r; a[1] += g; a[2] += b;
  if (++stX == stSide) {
    stX = 0;
    // Last source row of this output row: write the row out.
    if (rowOf[stY + 1] != rowOf[stY]) emitRow(rowOf[stY]);
    stY++;
  }
}

void keys_streamBytes(const uint8_t* bytes, size_t n) {
  if (stKey < 0) return;
  stBytes += n;
  for (size_t i = 0; i < n; i++) {
    stPix[stPixN++] = bytes[i];
    if (stPixN == 3) {
      addPixel(stPix[0], stPix[1], stPix[2]);
      stPixN = 0;
    }
  }
}

bool keys_streamEnd() {
  const bool ok = stKey >= 0 && stBytes == (size_t)stSide * stSide * 3;
  if (stKey >= 0 && !ok)
    Log.printf("keys    : key %d bitmap %u bytes, expected %d -- dropped\n", stKey,
                  (unsigned)stBytes, stSide * stSide * 3);
  stKey = -1;
  return ok;
}

void keys_set(int key, bool image, uint16_t bg, uint16_t fg, const String& text) {
  if (key < 0 || key >= keys_count()) return;
  KeySlot& s = slots[key];
  s.known = true;
  s.bg = bg;
  s.fg = fg;
  s.text = text;
  s.image = image;
  // Without a cache the rows were drawn as they came in, and are gone now.
  s.lost = image && !pool;
  if (!visible) return;
  if (!image) drawFallback(key, s);
  else if (pool) pushPixels(key, poolFor(key));
}

void keys_clearAll() {
  for (KeySlot& s : slots) s = KeySlot();
  lit = -1;
  if (visible) tft.fillScreen(ui::BG);
}

void keys_setVisible(bool on) { visible = on; }
bool keys_visible() { return visible; }

void keys_drawAll() {
  tft.fillScreen(ui::BG);
  for (int k = 0; k < keys_count(); k++)
    if (slots[k].known) drawKey(k);
  if (lit >= 0) drawOutline(lit, true);
}

bool keys_complete() {
  for (const KeySlot& s : slots)
    if (s.lost) return false;
  return true;
}

int keys_hit(int x, int y) {
  if (x < 0 || y < 0 || x >= cols * cellW || y >= rows * cellH) return -1;
  // The whole cell counts: on a resistive panel a press that lands in the
  // border was meant for the key, not for nothing.
  return (y / cellH) * cols + (x / cellW);
}

void keys_highlight(int key, bool on) {
  if (key < 0 || key >= keys_count()) return;
  lit = on ? key : (lit == key ? -1 : lit);
  if (visible) drawOutline(key, on);
}

