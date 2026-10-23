#!/bin/sh
# wltest-sub.sh -- build and run tools/wltest-sub.c (subsurface scenarios).
#
#   sh tools/wltest-sub.sh [-asan] [scenario]
#
# Needs libwayland-dev, wayland-protocols' scanner output (checked in under
# src/aurwl/gen), libxkbcommon-dev, cc. Linux only.
#
#   AURWL_C=/path/to/aurwl.c   test a different compositor source -- how
#                              the test is shown to fail on the code
#                              from before subsurfaces were composited:
#                                git show <old>:src/aurwl/aurwl.c > /tmp/old.c
#                                AURWL_C=/tmp/old.c sh tools/wltest-sub.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
W="$ROOT/src/aurwl"
OUT="${OUT:-${TMPDIR:-/tmp}/wltest-sub.$$}"
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT INT TERM

CFLAGS="-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter"
if [ "${1:-}" = "-asan" ]; then
    CFLAGS="$CFLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
    shift
fi
AURWL_C="${AURWL_C:-$W/aurwl.c}"
# aurwl.c includes its protocol headers by bare name.
cc $CFLAGS -o "$OUT/wltest-sub" "$HERE/wltest-sub.c" "$AURWL_C" "$ROOT/src/aurshell/draw.c" \
   "$W/textinput.c" "$W/gen/text-input-protocol.c" "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
   "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
   -I"$W/gen" -I"$W" $(pkg-config --cflags --libs wayland-server wayland-client xkbcommon) -lm
"$OUT/wltest-sub" "$@"
