#!/bin/sh
# wltest-deco.sh -- build and run tools/wltest-deco.c (which windows draw their
# own title bar, and what it can ask).
#
#   sh tools/wltest-deco.sh
#
# Needs libwayland-dev, libxkbcommon-dev, libdbus-1-dev, pkg-config, cc (the
# protocol code is checked in under src/aurwl/gen). Linux only.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
W="$ROOT/src/aurwl"
OUT="${TMPDIR:-/tmp}/wltest-deco.$$"
mkdir -p "$OUT/run"
chmod 700 "$OUT/run"
trap 'rm -rf "$OUT"' EXIT INT TERM
cc -O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -o "$OUT/wltest-deco" \
   "$HERE/wltest-deco.c" "$W/aurwl.c" "$W/textinput.c" \
   "$ROOT/src/aurshell/draw.c" "$ROOT/src/aurshell/session.c" "$ROOT/src/aurshell/run.c" \
   "$ROOT/src/aurshell/shellcommon.c" "$ROOT/src/aurshell/anim.c" "$ROOT/src/common/theme.c" \
   "$ROOT/src/common/font.c" "$ROOT"/src/aurshell/layouts/*.c "$ROOT/src/aurshell/foot.c" \
   "$W/gen/text-input-protocol.c" "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
   "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
   -I"$W/gen" -I"$W" -I"$ROOT/src/aurshell" -I"$ROOT/src/common" $(pkg-config --cflags --libs wayland-server wayland-client xkbcommon dbus-1) -lm
XDG_RUNTIME_DIR="$OUT/run" "$OUT/wltest-deco"
