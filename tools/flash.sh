#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# flash.sh -- compile the deck, upload it, open the serial monitor.
#
#   ./tools/flash.sh                       auto-detect the board's port
#   ./tools/flash.sh /dev/tty.usbserial-1234
#
# Adapted from the MP3 player's tools/flash.sh: one image only (no uploader),
# default partition scheme. Settings live in NVS and on the SD card, so a
# re-flash keeps them.
#
# NOTE on the 2.8" board: it has both micro-USB and USB-C, wired to the same
# CH340 serial chip. Use one at a time.
# ---------------------------------------------------------------------------
set -o pipefail   # not -u: bash 3.2 (macOS stock) trips over empty arrays

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# UploadSpeed=115200: this board's CH340 fails the switch to 921600.
FQBN="${FQBN:-esp32:esp32:esp32:UploadSpeed=115200}"
BAUD="${BAUD:-115200}"
PORT="${1:-}"

if command -v arduino-cli >/dev/null 2>&1; then
  CLI="$(command -v arduino-cli)"
elif [ -x "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" ]; then
  CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
else
  echo "arduino-cli not found (not on PATH, not inside Arduino IDE.app)." >&2
  exit 1
fi

if [ -z "$PORT" ]; then
  PORTS=()
  for p in /dev/tty.usbserial* /dev/tty.wchusbserial* /dev/ttyUSB*; do
    [ -e "$p" ] && PORTS+=("$p")
  done
  if [ "${#PORTS[@]}" -eq 0 ]; then
    echo "No board found. Plug in ONE USB cable (micro-USB or USB-C, not both)." >&2
    echo "If the port never appears, you likely need the CH340 driver." >&2
    exit 1
  elif [ "${#PORTS[@]}" -gt 1 ]; then
    echo "More than one serial port present -- pass the one you want:" >&2
    printf '  %s\n' "${PORTS[@]}" >&2
    exit 1
  fi
  PORT="${PORTS[0]}"
fi

"$ROOT/tools/sync_shared.sh" >/dev/null

echo "port   : $PORT"
"$CLI" compile --fqbn "$FQBN" deck || exit 1
"$CLI" upload  --fqbn "$FQBN" -p "$PORT" deck || {
  echo
  echo "Upload failed. Things that cause this on a CYD:" >&2
  echo "  * both USB connectors plugged in at once" >&2
  echo "  * a charge-only cable (very common -- try another cable)" >&2
  echo "  * the serial monitor still open in the Arduino IDE" >&2
  exit 1
}

echo
echo "--- monitor @ $BAUD (Ctrl-C to exit) ---"
exec "$CLI" monitor -p "$PORT" -c "baudrate=$BAUD"
