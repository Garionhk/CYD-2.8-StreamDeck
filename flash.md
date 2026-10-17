# Building and distributing firmware

How to flash your own board, how to make a single `.bin` other people can
flash, and how they flash it.

Nothing user-specific is compiled in. Settings, touch calibration, the WiFi
network and the setup page password live in NVS on the device, and are mirrored
to `config.json` on the SD card. That is what makes one binary shareable.

## 1. Flashing your own board (keeps settings)

```bash
./tools/flash.sh
```

This compiles the firmware, uploads it with arduino-cli and opens the serial
monitor. It only writes the app, so NVS (settings, calibration) survives.

It uploads at **115200 baud**: this board's CH340 USB chip fails the switch to
921600. A failed fast upload writes nothing, so it is harmless, just slow.

## 2. Making a release

Set `FIRMWARE_VERSION` in `deck/version.h` first. The script refuses a version
that does not match it.

```bash
./tools/make_release.sh 1.0.0
```

This produces `release/cyd-streamdeck-v1.0.0-4mb.bin` and adds its line to
`release/SHA256SUMS`, keeping earlier releases' lines. The `.bin` is a full
4 MB image: bootloader at `0x1000`, partition table at `0x8000`, OTA selector at
`0xe000` and the app at `0x10000`. The person flashing it needs one command and
one address.

The script refuses to finish unless the app slot holds this program at this
version. It checks for "CYD Stream Deck" and the version number, both of which
are in the boot log line.

**The merged image at `0x0` is a factory reset.** Its gaps are `0xFF`, and those
cover NVS: settings, touch calibration, WiFi and the setup page password are
erased. The SD card is untouched. If `config.json` is on it, the next boot reads
it back: WiFi, Companion address, layout, brightness and even the touch
calibration return, so the board comes back as it was.

## 3. Instructions for the person flashing

They need [esptool](https://github.com/espressif/esptool), not the Arduino
toolchain:

```bash
pip install esptool
```

Find the port. The board is a CH340: `/dev/cu.usbserial-*` on macOS,
`/dev/ttyUSB0` on Linux, something like `COM3` on Windows. Use **one** USB cable:
the 2.8" board has micro-USB and USB-C wired to the same chip, and both at once
breaks uploads.

Erase, then write:

```bash
esptool --port /dev/cu.usbserial-1420 erase-flash
```

```bash
esptool --port /dev/cu.usbserial-1420 --baud 115200 write-flash 0x0 cyd-streamdeck-v1.0.0-4mb.bin
```

At 115200 a 4 MB image takes a few minutes. Older esptool (v4) spells the
commands `erase_flash` / `write_flash`.

Check the download first:

```bash
shasum -a 256 -c SHA256SUMS
```

Then follow the README from **Configure it**: put `config.json` on the SD card,
or join the `CYD-Deck-XXXX` hotspot.

## 4. Notes

- **Wipe settings without reflashing:** erase just NVS. Settings come back from
  the SD card's `config.json` if there is one. Remove the card or the file
  first for a truly clean start.

  ```bash
  esptool --port /dev/cu.usbserial-1420 erase-region 0x9000 0x5000
  ```

- **Wrong panel after flashing** (blank, white or garbled screen): hold a
  finger on the screen from power-up for 8 s to switch between ST7789 and
  ILI9341.
- **Reading the log:** the board restarts whenever its serial port is opened.
  Use `http://<deck>/log` over WiFi for anything that happened before you
  connected.
