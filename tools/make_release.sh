#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# make_release.sh -- build the firmware and merge it into ONE file to flash
#
#   ./tools/make_release.sh 1.0.0
#
# Produces release/cyd-streamdeck-v1.0.0-4mb.bin and adds its line to
# release/SHA256SUMS, keeping the lines of earlier releases.
#
# The release is a full 4 MB image -- bootloader, partition table, OTA
# selector and the app at their offsets -- so the person flashing it needs one
# command and one address:
#   esptool write-flash 0x0 cyd-streamdeck-v1.0.0-4mb.bin
#
# Like any full image written at 0x0 it is a factory reset: the gaps are 0xFF,
# which covers NVS (settings, touch calibration, WiFi, web password). The SD
# card's config.json is untouched and is read again on the next boot, so a
# board with its card in comes back as it was. To update your own board
# without that, use ./tools/flash.sh instead.
#
# Same arrangement as the MP3 player's tools/make_release.sh, with one app.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# "v1.0.0" and "V1.0.0" mean 1.0.0: the file name adds its own "v".
VERSION="${1:-}"
VERSION="${VERSION#[vV]}"
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "usage: tools/make_release.sh <version>   e.g. 1.0.0" >&2
  exit 1
fi
# The version the firmware reports (boot log, setup page) must be the one on
# the file.
if ! grep -q "#define FIRMWARE_VERSION \"$VERSION\"" deck/version.h; then
  echo "deck/version.h says $(grep -o 'FIRMWARE_VERSION "[^"]*"' deck/version.h), not $VERSION" >&2
  exit 1
fi

# Default partition scheme: app0 at 0x10000 (1.25 MB), the same FQBN as
# tools/flash.sh, so a release is exactly what the developer flashes.
FQBN="esp32:esp32:esp32"

CLI="$(command -v arduino-cli || echo "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")"
ESPTOOL="$(ls -d "$HOME"/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | tail -1)"
[ -x "$ESPTOOL" ] || ESPTOOL="$(command -v esptool || command -v esptool.py)"
BOOT_APP0="$(ls "$HOME"/Library/Arduino15/packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin | tail -1)"

"$ROOT/tools/sync_shared.sh" >/dev/null

BUILD="$ROOT/deck/build/release"
echo "== firmware $VERSION"
"$CLI" compile --fqbn "$FQBN" --clean --output-dir "$BUILD" deck | grep -E "Sketch uses|Global"

mkdir -p release
OUT="release/cyd-streamdeck-v${VERSION}-4mb.bin"

# Offsets as arduino-cli's own upload uses them for this board and scheme
# (bootloader at 0x1000 on the classic ESP32). Flash mode and frequency are
# kept as the bootloader was built; only the size is stated.
"$ESPTOOL" --chip esp32 merge-bin -o "$OUT" \
    --flash-mode keep --flash-freq keep --flash-size 4MB --pad-to-size 4MB \
    0x1000  "$BUILD/deck.ino.bootloader.bin" \
    0x8000  "$BUILD/deck.ino.partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$BUILD/deck.ino.bin" >/dev/null

# The app slot must hold this program, at this version: both strings are in
# the boot log line, so they are in the binary.
SLOT="$(mktemp)"
trap 'rm -f "$SLOT"' EXIT
dd if="$OUT" of="$SLOT" bs=65536 skip=1 count=20 2>/dev/null     # app0: 0x10000, 1.25 MB
for mark in "CYD Stream Deck" "$VERSION"; do
  if ! grep -aqF "$mark" "$SLOT"; then
    echo "sanity check failed: \"$mark\" is not in the app slot at 0x10000" >&2
    rm -f "$OUT"
    exit 1
  fi
done

# One line per published release: replace this file's, keep the others.
NAME="$(basename "$OUT")"
touch release/SHA256SUMS
{ grep -vF "  $NAME" release/SHA256SUMS || true
  ( cd release && shasum -a 256 "$NAME" )
} > release/SHA256SUMS.new
mv release/SHA256SUMS.new release/SHA256SUMS
echo
ls -l "$OUT"
cat release/SHA256SUMS
