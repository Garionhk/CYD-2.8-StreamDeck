## CYD Stream Deck v1.0.0

**First release.** Turns a 2.8" "cheap yellow display" (ESP32-2432S028R) into a
WiFi touch surface for [Bitfocus Companion](https://bitfocus.io/companion). Its
keys show the buttons Companion draws, and pressing one presses that button.

File: `cyd-streamdeck-v1.0.0-4mb.bin`, a full 4 MB image flashed at `0x0`. See
[flash.md](../flash.md).

### ✨ Features

**Companion Satellite API.** The deck registers as a surface over TCP 16622 and
works with Companion 3.0 and later. Tested with 3.0 and 5.0.
- **Live images:** every key shows Companion's live button image.
- **Presses:** finger down and finger up go to Companion as press and release,
  so hold actions work too.
- **Brightness:** Companion's surface brightness setting controls the
  backlight.

**Three layouts.**

| Layout | Keys | Key size |
|---|---|---|
| **8×4** (default) | 32 | the whole Companion page, as in its web button editor |
| **5×3** | 15 | the Stream Deck MK.2's shape |
| **4×3** | 12 | the biggest keys |

From Companion 3.2 the images arrive at the key's size. Companion 3.0–3.1
always sends 72×72 images, which the deck scales down smoothly.

**Quick setup from the SD card.** Put `config.json` on the card: WiFi, Companion
address, layout, display. The deck reads it on every boot and writes every
change made on the device back to it. That includes the touch calibration, so
a card moved to a fresh board skips calibrating.

**Setup page in the browser.**
- **Where:** `http://<device id>.local` or the IP address. The first boot opens a
  `CYD-Deck-XXXX` hotspot with a captive page, so a phone opens it by itself.
- **What it sets:** WiFi, with a network scan; Companion address; layout;
  panel; colours; orientation; and a live brightness slider.
- **Password:** none by default, with a warning until you set one. Then it asks
  for user `admin`. Wrong passwords lock it, the wait doubling up to 5 minutes.
  The device menu or the SD card resets it.
- **Log:** `/log` shows the recent log over WiFi, including every key press and
  every saved setting.

**Device menu** (hold the screen 3 s): brightness, flip 180°, touch
calibration, and a **Web setup** QR code that opens the setup page from a
phone.

**Both 2.8" panels from one file.** ST7789 and ILI9341 run the same binary. Hold
a finger on the screen from power-up for 8 s to switch, the same gesture as the
CYD MP3 player.

### ⚡ Performance

- **Memory:** a ~21 KB key-image line from Companion is decoded as it arrives
  and never held in memory. The receive buffer is 2 KB, and the key images live
  in one block taken at boot, so WiFi and the setup page keep about 90 KB free.
- **Speed:** a full page of 32 keys arrives in about 0.3 s from Companion 5. Key
  presses are acknowledged in about 10 ms.
- **WiFi power saving is off**, so the radio does not doze between packets.

### ⚠️ Notes

- **Flashing the full image is a factory reset of NVS.** With `config.json` on
  the SD card, the next boot restores the settings from it.
- **The setup page login is HTTP Basic over plain HTTP.** It keeps others on
  your network out of the settings, but it is not encryption.
