#!/bin/sh
# build.sh -- build the fuzz harnesses in tools/fuzz.
#
#   sh tools/fuzz/build.sh [OUT]           libFuzzer + ASan + UBSan (clang)
#   MODE=regress sh tools/fuzz/build.sh    ASan + UBSan, standalone.c main (clang, else cc)
#
# OUT defaults to /tmp/auros-fuzz. Each harness becomes OUT/fuzz_<name>
# (or OUT/fuzz_<name>-regress). Undefined behaviour aborts rather than
# printing and carrying on, so UBSan findings are crashes the same as
# ASan ones.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="${1:-${OUT:-/tmp/auros-fuzz}}"
MODE="${MODE:-fuzz}"
mkdir -p "$OUT"

# float-cast-overflow is named explicitly: clang's "undefined" group
# includes it, gcc's does not, and a NaN or 1e30 cast to int is the
# commonest UB a theme file can cause.
SAN="-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer"
CFLAGS="-g -O1 -std=gnu11 -Wall -Wno-unused-function $SAN"
if [ "$MODE" = fuzz ]; then
    CC="${CC:-clang}"
    LINK="-fsanitize=fuzzer"
    SFX=""
    MAIN=""
else
    # clang when there is one: gcc's UBSan misses some of what the
    # fuzzer (clang) found -- regress/aurwl/shm-misaligned is a
    # misaligned load gcc does not report. Plain cc still runs the rest.
    if [ -z "${CC:-}" ]; then CC=cc; command -v clang >/dev/null 2>&1 && CC=clang; fi
    LINK=""
    SFX="-regress"
    MAIN="$HERE/standalone.c"
fi

C="$ROOT/src/common"
S="$ROOT/src/aurshell"
W="$ROOT/src/aurwl"

build() {   # name, sources..., then -- extra link flags
    name=$1; shift
    srcs=""; libs=""; seen=0
    for a in "$@"; do
        if [ "$a" = "--" ]; then seen=1; continue; fi
        if [ $seen = 0 ]; then srcs="$srcs $a"; else libs="$libs $a"; fi
    done
    # shellcheck disable=SC2086
    $CC $CFLAGS $LINK -o "$OUT/fuzz_$name$SFX" $srcs $MAIN $libs
    echo "built $OUT/fuzz_$name$SFX"
}

build font    "$HERE/fuzz_font.c" "$C/font.c" -- -lm
build theme   "$HERE/fuzz_theme.c" "$C/theme.c" "$C/wall.c" "$S/draw.c" -- -lm
build desktop "$HERE/fuzz_desktop.c" -- -lm
# Stream A2's parsers: fuzzed here, fixed there (reproducers go to A2).
build inflate "$HERE/fuzz_inflate.c" "$ROOT/src/aurbridge/inflate.c"
build gpt     "$HERE/fuzz_gpt.c" "$ROOT/src/aurstage/gpt.c"
build journal "$HERE/fuzz_journal.c" "$ROOT/src/aurstage/journal.c"
# Stream W1's Wishes engine: cloud documents, socket lines, wishes.json.
A="$ROOT/src/aurwish"
build wish    "$HERE/fuzz_wish.c" "$A/json.c" "$A/wish.c" "$A/text.c" "$A/store.c" "$A/engine.c" "$A/cloud.c" "$A/answer.c" -- -lm
# Stream R's registry reader (runs as root on her Windows disk's hives).
build hive    "$HERE/fuzz_hive.c"
# Stream W1's zwp_text_input_v3 (src/aurwl/textinput.c): fuzz_aurwl.c with the
# manager in the registry; textinput.c and wishhook.c are included by the harness.
if pkg-config --exists wayland-server xkbcommon 2>/dev/null; then
    build textinput "$HERE/fuzz_textinput.c" "$S/draw.c" \
        "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
        "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
        "$W/gen/text-input-protocol.c" \
        -I"$W/gen" -I"$W" -- $(pkg-config --libs wayland-server xkbcommon) -lm
fi
# fuzz_xwm.c: the X window manager (src/aurwl/xwm.inc, included by aurwl.c) against
# an X server that lies; it includes aurwl.c whole, like fuzz_aurwl.c.
if pkg-config --exists wayland-server xkbcommon 2>/dev/null; then
    build xwm "$HERE/fuzz_xwm.c" "$S/draw.c" \
        "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
        "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
        -I"$W/gen" -I"$W" -- $(pkg-config --libs wayland-server xkbcommon) -lm
fi
# fuzz_aurwl.c includes aurwl.c itself (its end-of-input check reads the
# compositor's lists).
if pkg-config --exists wayland-server xkbcommon 2>/dev/null; then
    build aurwl "$HERE/fuzz_aurwl.c" "$S/draw.c" \
        "$W/gen/xdg-shell-protocol.c" "$W/gen/xdg-decoration-protocol.c" \
        "$W/gen/viewporter-protocol.c" "$W/gen/xdg-output-protocol.c" \
        -I"$W/gen" -I"$W" -- $(pkg-config --libs wayland-server xkbcommon) -lm
fi
