#!/bin/sh
# cursortest.sh -- build and run tools/cursortest.c (the pointer's pictures).
#
#   sh tools/cursortest.sh [sheet.png]
#
#   CURSOR_C=/path/to/cursor.c    test a different source (tools/wltest-sub-mutants.py)
# Needs only cc and libm; runs anywhere.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT="${OUT:-${TMPDIR:-/tmp}/cursortest.$$}"
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT INT TERM
cc -O1 -g -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
   -I"$ROOT/src/aurshell" -o "$OUT/cursortest" "$HERE/cursortest.c" "${CURSOR_C:-$ROOT/src/aurshell/cursor.c}" \
   "$ROOT/src/aurshell/draw.c" "$ROOT/src/common/png.c" -lm
"$OUT/cursortest" "$@"
