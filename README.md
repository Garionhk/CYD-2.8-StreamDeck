# CYD Stream Deck

Firmware that turns a **"cheap yellow display"** ESP32 board into a WiFi
**Stream Deck for [Bitfocus Companion](https://bitfocus.io/companion)**. The
screen shows a grid of touch keys. By default it is **8×4, a whole Companion
page**; 5×3 and 4×3 give bigger keys. Each key shows the button Companion draws
(icon, colours, text, live feedback), and tapping it presses that button in
Companion.

- **Companion's Satellite API** (TCP 16622): the deck registers as a 32-key
  (or 15- or 12-key) surface. Companion renders every button and pushes it to
  the deck, which scales it to the key.
  Finger down and finger up are sent as press and release, so hold actions
  work too.
- **Quick setup from the SD card:** put `config.json` on the card (WiFi,
  Companion IP, display settings) and boot. The deck reads it on every boot
  and rewrites it whenever a setting changes on the device.
- **Setup hotspot** when there is no card: `CYD-Deck-XXXX` with a captive setup
  page. The same page stays available on your network afterwards at
  `http://<device id>.local` (for example `http://cyd-cae118.local`). Menu ▸
  *Web setup* shows a QR code that opens it.
- **Both 2.8" panels from one binary** (ST7789 and ILI9341), switched with the
  same power-up gesture as the CYD MP3 player.
- **Brightness** from the device menu, the web page, or Companion's own
  surface brightness setting.

**Board: ESP32-2432S028R** (2.8", 240×320, resistive touch). The display, touch,
SD and calibration code comes from the CYD MP3 player project
(`../ESP32 2.8 LCD MP3 Player - Internal Speaker`).

---

## Quick start

### 1. Flash

**Ready-made firmware:** `release/` has a single file to flash with
[esptool](https://github.com/espressif/esptool). No Arduino setup is needed:

```bash
esptool --port /dev/cu.usbserial-1420 --baud 115200 write-flash 0x0 release/cyd-streamdeck-v1.0.0-4mb.bin
```

[flash.md](flash.md) covers finding the port, checking the download and making
a release. The release notes are in `release/RELEASE-v*.md`.

**Or build it yourself.** You need arduino-cli (or the Arduino IDE) with the **esp32 core 3.x** and these
libraries: **TFT_eSPI** and **ArduinoJson 7**.

TFT_eSPI is configured by replacing its `User_Setup.h`. Use the ST7789 file,
whichever panel your board has, because the binary switches to ILI9341 at runtime:

```bash
cp config/User_Setup_ST7789.h.template ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```

```bash
./tools/flash.sh
```

The build stops with a clear `#error` if `User_Setup.h` does not match
`config/board.h`.

### 2. Configure it: SD card (fastest)

Copy `sdcard/config.example.json` to the root of a FAT32 card as
**`config.json`**, fill in your WiFi and the IP of the computer running
Companion, insert the card, and power up.

```json
{
  "wifi":      { "ssid": "MyWiFi", "password": "my-wifi-password" },
  "companion": { "host": "192.168.1.50", "port": 16622, "deviceId": "", "name": "CYD Stream Deck" },
  "display":   { "panel": "ST7789", "invert": true, "rotation": 1, "brightness": 80, "layout": "8x4" }
}
```

| Key | Meaning |
|---|---|
| `wifi.ssid` / `wifi.password` | the network to join |
| `companion.host` / `port` | the Companion computer; 16622 is Companion's Satellite port |
| `companion.deviceId` | empty means `cyd-` plus the last 6 digits of the MAC. Set it to keep the same Companion surface when you swap boards |
| `companion.name` | the name shown in Companion ▸ Surfaces |
| `web.password` | setup page password, login user `admin`. Empty means the page is open. Blank it here to get back in if you forget it |
| `display.panel` | `ST7789` or `ILI9341` |
| `display.invert` | flip it if every colour shows as its opposite |
| `display.rotation` | `1` normal, `3` upside down |
| `display.brightness` | 10–100 |
| `display.layout` | `8x4` (the whole Companion page, 40×60 keys), `5x3` (64×80) or `4x3` (80×80) |
| `touch.*` | written by the deck after calibration. Copy it to another card to skip calibrating again |

How the file is used:

- It is read **on every boot**, and its values override what the deck had saved.
  Keys you leave out keep the deck's saved values, and the deck rewrites the file
  with every key filled in.
- Changes made **on the device** (menu, web page, Companion brightness, panel
  gesture, calibration) are written back to it. The write is atomic (the deck
  writes `config.tmp`, then renames it).
- A card **without** the file gets a complete one written, ready to edit.
- A file that is **not valid JSON** is renamed to `config.bad` (nothing is lost),
  the screen tells you, and the deck uses its saved settings.
- The WiFi password is stored in plain text on the card. Keep the card somewhere safe.

### 3. Or configure it: hotspot

With no WiFi configured, the deck opens the hotspot **`CYD-Deck-XXXX`**. Join
it from a phone; the setup page opens by itself (or browse to
`http://192.168.4.1`). Enter WiFi and the Companion IP, then Save. The deck
restarts, and the settings are written to the SD card too if one is inserted.

If the saved network cannot be joined within 20 s, the hotspot comes up as
well, so you can always get back in.

### 4. Companion

1. In Companion, make sure the **Satellite** listener is enabled (Settings; on
   by default on TCP 16622).
2. The deck shows up under **Surfaces** as "CYD Stream Deck". With the 8×4
   layout it shows the whole page, the same grid as Companion's web button
   editor. With 5×3 or 4×3 it shows the top-left part of the page; move that
   area, change the page, or set the brightness in that surface's settings.

If you have no Companion yet, `python3 tools/fake_companion.py` stands in for
it. See [Testing](#testing).

---

## Using it

| Touch | Does |
|---|---|
| tap / hold a key | press / release that Companion button (a white outline shows the press) |
| hold anywhere for **3 s** | **device menu**. The held key is released first |
| finger on the screen **from power-up, 8 s** | switch panel controller ST7789 ⇄ ILI9341 and restart |

**Device menu:** brightness (drag the bar or use − / +), *Calibrate* (touch
wizard), *Flip 180* (turns the screen upside down), *Web setup*, *Close*.
*Web setup* shows a QR code. On your network, scan it with a phone to open the
setup page; on the hotspot, scanning it joins the hotspot's WiFi. It also shows the
setup-page address, the Companion connection and where the settings came from.
It closes on its own after 10 s without a touch.

**Status screen:** whenever the deck is not connected to Companion, it shows
WiFi, Companion address and state (with the reason when it is failing),
settings source, device id, and how to reach the setup page.

### Panel type (ST7789 / ILI9341)

Boards sold as ESP32-2432S028R have one of two display controllers behind
identical glass. The firmware is built for ST7789 and drives an ILI9341 by
sending that controller's init sequence itself. To switch:

- **Blank or garbled screen:** power up with a finger held on the screen. The
  blue LED blinks from 4 s; at 8 s the deck switches and restarts. On a
  never-calibrated board the blue LED lights for 3 s at power-up to show when
  to press.
- **Screen works:** set *Panel controller* on the setup page, or `display.panel`
  in `config.json`.

The choice is saved in NVS and in `config.json`.

---

## How it works

```
deck/
  deck.ino       setup order, screens, finger down/up -> KEY-PRESS
  satellite.*    Companion Satellite API client (TCP, line protocol)
  keys.*         key grid (8x4 / 5x3 / 4x3): RGB888 -> scaled RGB565, key cache, press outline
  portal.*       WiFi STA / hotspot, captive DNS, setup web page
  settings.*     NVS + SD config.json (read on boot, atomic write-back)
  storage.*      SD mount on VSPI
  status.*       the "not connected" screen
  menu.*         brightness / flip / calibrate / web-setup QR overlay
  ui.*           palette, small drawing helpers, QR code (ESP-IDF's encoder)
  log.*          Log.printf: serial + a 4 KB in-memory copy served at /log
  display.*      panel init for both controllers, PWM backlight, boot gesture   (from the MP3 player)
  touch.*        bit-banged XPT2046 + calibration                               (from the MP3 player)
  calibrate.*    four-corner touch wizard                                       (adapted from the MP3 player)
  board.h        pin map, a copy of config/board.h (tools/sync_shared.sh)
```

**Buses:** display on HSPI (`USE_HSPI_PORT`), SD card on VSPI, touch bit-banged
on its own pins. This is the same layout as the MP3 player and is why
`USE_HSPI_PORT` is required.

**Satellite protocol:** plain text lines over TCP with `KEY=VALUE` parameters.
Strings are in double quotes and booleans are `1`/`0`.

```
Companion -> BEGIN CompanionVersion="..." ApiVersion="1.14.0"
deck      -> ADD-DEVICE DEVICEID="cyd-a1b2c3" PRODUCT_NAME="CYD Stream Deck"
             KEYS_TOTAL=12 KEYS_PER_ROW=4 BITMAPS=72 COLORS=hex TEXT=1 BRIGHTNESS=1
Companion -> ADD-DEVICE OK DEVICEID="cyd-a1b2c3"
Companion -> KEY-STATE DEVICEID=".." KEY=5 TYPE="BUTTON" PRESSED=0
             BITMAP="<base64: 72*72*3 bytes RGB888>" COLOR="#rrggbb" TEXTCOLOR="#rrggbb" TEXT="<base64>"
deck      -> KEY-PRESS DEVICEID=".." KEY=5 PRESSED=1      (finger down)
deck      -> KEY-PRESS DEVICEID=".." KEY=5 PRESSED=0      (finger up)
Companion -> BRIGHTNESS DEVICEID=".." VALUE=60  |  KEYS-CLEAR  |  PING x  (deck answers PONG x)
```

The deck asks for bitmaps at its key size (`BITMAPS=36` on 8×4). Companion 3.0
(API 1.4) ignores the size and always sends 72×72, about a 21 KB line; the deck
reads the API version from `BEGIN` to know which size is coming.

That line is never held in memory. The receive buffer is only 2 KB, for the
short fields. The `BITMAP` value is base64-decoded byte by byte as it arrives,
box-filtered on the fly (72 → 36 averages 2×2 blocks, so text stays smooth),
and each finished row goes straight into the key's slot in the image store.
The image store is one block (81 KB on 8×4) taken at boot, before WiFi starts,
so it cannot fragment the heap. Closing the menu redraws from it without
asking Companion again. If it does not fit (the 4×3 layout needs 121 KB), rows
go straight to the panel instead, and closing the menu reconnects so that
Companion resends the keys.

Every press and Companion's reply are logged:
`sat : -> KEY-PRESS key 9 (row 1, col 1) down`, then `sat : <- KEY-PRESS OK`.

**Reading the log:** the recent log (the last 4 KB, with seconds since boot) is
at **`http://<deck>/log`**, linked at the bottom of the setup page. Use that
rather than the serial port: this board restarts whenever its USB serial port is
opened, so a serial monitor only shows what happens after it connects. The deck
pings every 2 s, treats 6 s of silence as a dead link, and reconnects with
backoff (1 s up to 10 s).

---

## Testing

Without Companion:

```bash
python3 tools/fake_companion.py
```

Set the deck's Companion IP to your computer's address. The deck shows numbered,
coloured keys (as many as its layout has). Each tap is logged and toggles that key between its
colour and white. Type `50` and Enter to send `BRIGHTNESS VALUE=50`, or `c` to
clear the keys. `--text-only` sends no bitmaps, to test the colour + text
fallback. `--legacy` behaves like Companion 3.0 (API 1.4: unquoted values,
always 72×72); without it the fake behaves like newer Companion and sends the
size the deck asks for.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Blank / white / garbled screen | the other panel controller: hold a finger on the screen from power-up for 8 s |
| Every colour is its opposite | *Colours* on the setup page, or `display.invert` |
| Touch lands in the wrong place | menu ▸ *Calibrate* |
| Status says "cannot reach …" | wrong Companion IP, Companion not running, or a firewall blocking TCP 16622 on the Companion computer |
| Status says "no answer -- is this Companion…" | something else is listening on that port, or the Satellite API is disabled |
| `config.json` ignored | check the status screen's *Settings* line. "was invalid" means the file is now `config.bad`. Fix the JSON and rename it back |
| Upload fails | one USB cable only, a data (not charge-only) cable, close other serial monitors |
| Keys take seconds to update after a page change | check `/log`: each page logs `N key images, X KB in Y s`. If it is slow while `longest loop gap` stays small, the network is the bottleneck, not the deck. Usually it's the Companion computer's WiFi: put it on Ethernet. On a Mac, AirDrop/Handoff (`awdl0`) adds 100–300 ms spikes on 2.4 GHz. Companion 3.0–3.1 also always sends 72×72 images (4× more data on 8×4); 3.2 and later (tested with 5.0) send the size the deck asks for |

### Setup page password

There is **no password at first**. The page shows an orange warning until you
set one, and the first-boot hotspot page has the same field, so you can set it
during setup. Set it under *Setup page password*. After that the page and
everything behind it (`/log`, the live brightness slider) ask for user
**`admin`** and the password, the same login as the WOL-NTFY project.

- Blank password fields keep the current one. Two entries that don't match,
  or fewer than 4 characters, reject the whole form, so a typo can't lock you out.
- *Remove the password* makes the page open again.
- After 5 wrong passwords the page locks for 5 s, doubling with each further
  wrong one up to 5 minutes. Every wrong attempt is logged in `/log`.
- **Forgot it?** On the deck: hold 3 s ▸ *Web setup* ▸ *Clear password*, then
  tap again to confirm. Whoever is holding the deck counts as the owner. Or blank
  `web.password` in `config.json` on the SD card.

The login is HTTP Basic over plain HTTP. It keeps other people on your network
out of the settings, but someone capturing WiFi traffic could read it. The page
never sends a stored password (WiFi or its own) back to the browser.
