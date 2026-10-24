#!/bin/sh
# wltest-xlife.sh -- build and run tools/wltest-xlife.c (Xwayland's launch, reap, re-arm and give-up, with a pretend Xwayland).
#
#   sh tools/wltest-xlife.sh [-asan]
#
#   AURWL_C=/path/aurwl.c   test a different compositor source (its xwm.inc is the one beside it): how
#                           tools/wltest-sub-mutants.py shows each check can fail.
# Needs libwayland-dev, libxkbcommon-dev, cc. Linux only.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
W="$ROOT/src/aurwl"
OUT="${OUT:-${TMPDIR:-/tmp}/wltest-xlife.$$}"
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT INT TERM
CFLAGS="-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter"
if [ "${1:-}" = "-asan" ]; then
    CFLAGS="$CFLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
    shift
fi
AURWL_C="${AURWL_C:-$W/aurwl.c}"
cc $CFLAGS -o "$OUT/wltest-xlife" "$HERE/wltest-xlife.c" "$AURWL_C" "$ROOT/src/aurshell/draw.c" \
   "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
   "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
   -I"$W/gen" -I"$W" $(pkg-config --cflags --libs wayland-server xkbcommon) -lm
# A hang must end by itself (one once sat in connect() for hours): the test has its own alarm, and this is the belt.
if command -v timeout >/dev/null 2>&1; then timeout 300 "$OUT/wltest-xlife"; else "$OUT/wltest-xlife"; fi
