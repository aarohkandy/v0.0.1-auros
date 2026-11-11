#!/bin/sh
# wltest-spawn.sh -- build and run tools/wltest-spawn.c (a program the shell starts begins in her home
# folder, and the shell can tell running from handed off from failed).
#
#   sh tools/wltest-spawn.sh [-asan]
#   AURWL_C=/path/aurwl.c       test a different compositor source (to show a check can fail)
#
# Needs libwayland-dev, libxkbcommon-dev, libdbus-1-dev, pkg-config, cc (the
# protocol code is checked in under src/aurwl/gen). Linux only.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
W="$ROOT/src/aurwl"
OUT="${TMPDIR:-/tmp}/wltest-spawn.$$"
mkdir -p "$OUT/run"
chmod 700 "$OUT/run"
trap 'rm -rf "$OUT"' EXIT INT TERM
CFLAGS="-O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter"
if [ "${1:-}" = "-asan" ]; then
    CFLAGS="$CFLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
fi
cc $CFLAGS -o "$OUT/wltest-spawn" \
   "$HERE/wltest-spawn.c" "${AURWL_C:-$W/aurwl.c}" "$W/textinput.c" \
   "$ROOT/src/aurshell/draw.c" "$ROOT/src/aurshell/session.c" "$ROOT/src/aurshell/cursor.c" "$ROOT/src/aurshell/run.c" \
   "$ROOT/src/aurshell/shellcommon.c" "$ROOT/src/aurshell/anim.c" "$ROOT/src/common/theme.c" \
   "$ROOT/src/common/font.c" "$ROOT"/src/aurshell/layouts/*.c "$ROOT/src/aurshell/foot.c" \
   "$W/gen/text-input-protocol.c" "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
   "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
   -I"$W/gen" -I"$W" -I"$ROOT/src/aurshell" -I"$ROOT/src/common" $(pkg-config --cflags --libs wayland-server wayland-client xkbcommon dbus-1) -lm
XDG_RUNTIME_DIR="$OUT/run" "$OUT/wltest-spawn"
