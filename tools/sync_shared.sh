#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# sync_shared.sh -- copy the canonical config/board.h into the sketch folder.
#
# The Arduino IDE copies a sketch folder to a temp dir before building, so a
# sketch cannot #include "../config/board.h". deck/ carries its own copy and
# this keeps it honest: edit config/board.h, run this, rebuild.
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cp "$ROOT/config/board.h" "$ROOT/deck/board.h"
echo "board.h -> deck/"
