#!/bin/sh
# wltest-x.sh -- build and run tools/wltest-x.c (the X window manager, against a pretend X server).
#
#   sh tools/wltest-x.sh [-asan] [scenario]
#
#   AURWL_C / XWM_INC are not overridable here: the test includes aurwl.c whole,
#   so tools/wltest-sub-mutants.py points it at a mutated copy through -I.
# Needs libwayland-dev, libxkbcommon-dev, cc. Linux only.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
W="$ROOT/src/aurwl"
OUT="${OUT:-${TMPDIR:-/tmp}/wltest-x.$$}"
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT INT TERM
CFLAGS="-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function"
if [ "${1:-}" = "-asan" ]; then
    CFLAGS="$CFLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
    shift
fi
# MUTANT_DIR: a directory holding a changed aurwl.c / xwm.inc, searched first.
INC=""
[ -n "${MUTANT_DIR:-}" ] && INC="-I$MUTANT_DIR"
cc $CFLAGS -o "$OUT/wltest-x" "$HERE/wltest-x.c" "$ROOT/src/aurshell/draw.c" \
   "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
   "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
   $INC -I"$W/gen" -I"$W" $(pkg-config --cflags --libs wayland-server wayland-client xkbcommon) -lm
XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-$OUT}" "$OUT/wltest-x" "$@"
