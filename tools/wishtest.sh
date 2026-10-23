#!/bin/sh
# wishtest.sh -- every Wishes test, then proof that each check can fail.
#
#   sh tools/wishtest.sh            tests (ASan + UBSan) with every compiler found
#   sh tools/wishtest.sh --mutants  ... and the mutation proofs (about a minute)
#
# Runs on a Mac (the core and the engine) and on Linux (also aurwish
# itself, when libdbus-1-dev is there). Exits non-zero on any failure.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/wishtest.$$
mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT
W="$ROOT/src/aurwish"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
fail=0

for cc in gcc clang cc; do
    command -v $cc >/dev/null 2>&1 || continue
    # macOS's gcc is clang; one run is enough there.
    [ "$cc" = cc ] && [ -n "${ran:-}" ] && continue
    ran=1
    for t in wishtest wishtest-engine; do
        [ -f "$ROOT/tools/$t.c" ] || continue
        srcs="$W/json.c $W/text.c $W/wish.c $W/store.c"
        [ $t = wishtest-engine ] && srcs="$srcs $W/engine.c"
        # shellcheck disable=SC2086
        if ! $cc -std=gnu11 -g -O1 -Wall -Wextra -Werror $SAN -I "$W" \
                "$ROOT/tools/$t.c" $srcs -lm -o "$OUT/$t-$cc" 2>"$OUT/err"; then
            echo "FAIL build $t with $cc"; cat "$OUT/err"; fail=1; continue
        fi
        if TMPDIR="$OUT" "$OUT/$t-$cc" >"$OUT/log" 2>&1; then
            echo "ok   $t ($cc): $(tail -1 "$OUT/log")"
        else
            echo "FAIL $t ($cc)"; grep -E 'FAIL|ERROR|runtime error' "$OUT/log" | head -20; fail=1
        fi
    done
done

# The compositor's typed-word matcher (the only code that sees what she types).
if [ -f "$ROOT/tools/wishtest-hook.c" ]; then
    # shellcheck disable=SC2086
    if ${CC:-cc} -std=gnu11 -g -O1 -Wall -Wextra -Werror $SAN -I "$ROOT/src/aurwl" \
            "$ROOT/tools/wishtest-hook.c" "$ROOT/src/aurwl/wishhook.c" -o "$OUT/hook" 2>"$OUT/err" &&
       "$OUT/hook" >"$OUT/log" 2>&1; then
        echo "ok   wishtest-hook: $(tail -1 "$OUT/log")"
    else
        echo "FAIL wishtest-hook"; cat "$OUT/err"; grep -E 'FAIL|ERROR' "$OUT/log" | head; fail=1
    fi
fi

# The compositor really telling a password field from any other: the REAL aurwl.c
# and a real Wayland client speaking text-input-v3 (Linux with libwayland and xkbcommon).
if [ "$(uname)" = Linux ] && pkg-config --exists wayland-server wayland-client xkbcommon 2>/dev/null &&
   [ -f "$ROOT/tools/wishtest-textinput.c" ]; then
    G="$ROOT/src/aurwl/gen"
    # shellcheck disable=SC2046,SC2086
    if ${CC:-cc} -std=gnu11 -g -O1 -Wall -Wextra $SAN -I "$G" -I "$ROOT/src/aurwl" -o "$OUT/textinput" \
            "$ROOT/tools/wishtest-textinput.c" "$ROOT/src/aurwl/aurwl.c" "$ROOT/src/aurwl/textinput.c" \
            "$ROOT/src/aurwl/wishhook.c" "$ROOT/src/aurshell/draw.c" "$G/xdg-shell-protocol.c" \
            "$G/xdg-decoration-protocol.c" "$G/viewporter-protocol.c" "$G/xdg-output-protocol.c" \
            "$G/text-input-protocol.c" $(pkg-config --cflags --libs wayland-server wayland-client xkbcommon) -lm \
            2>"$OUT/err" &&
       ASAN_OPTIONS=detect_leaks=0 timeout 120 "$OUT/textinput" >"$OUT/log" 2>&1; then
        echo "ok   wishtest-textinput: $(tail -1 "$OUT/log")"
    else
        echo "FAIL wishtest-textinput"; grep -E 'error' "$OUT/err" | head; grep -E 'FAIL|ERROR' "$OUT/log" | head; fail=1
    fi
else
    echo "skip wishtest-textinput (needs Linux with libwayland-dev and libxkbcommon-dev)"
fi

# What a REAL GTK3 program says about its fields (needs python3-gi with GTK 3).
if [ "$(uname)" = Linux ] && pkg-config --exists wayland-server xkbcommon 2>/dev/null &&
   python3 -c 'import gi; gi.require_version("Gtk","3.0"); from gi.repository import Gtk' >/dev/null 2>&1; then
    G="$ROOT/src/aurwl/gen"
    # shellcheck disable=SC2046,SC2086
    if ${CC:-cc} -std=gnu11 -g -O1 -Wall -Wextra $SAN -I "$G" -I "$ROOT/src/aurwl" -o "$OUT/gtk" \
            "$ROOT/tools/wishtest-gtk.c" "$ROOT/src/aurwl/aurwl.c" "$ROOT/src/aurwl/textinput.c" \
            "$ROOT/src/aurwl/wishhook.c" "$ROOT/src/aurshell/draw.c" "$G/xdg-shell-protocol.c" \
            "$G/xdg-decoration-protocol.c" "$G/viewporter-protocol.c" "$G/xdg-output-protocol.c" \
            "$G/text-input-protocol.c" $(pkg-config --cflags --libs wayland-server xkbcommon) -lm \
            2>"$OUT/err" &&
       WISHTEST_GTK_PY="$ROOT/tools/wishtest-gtk.py" ASAN_OPTIONS=detect_leaks=0 timeout 120 "$OUT/gtk" -t 60 >"$OUT/log" 2>&1; then
        echo "ok   wishtest-gtk: $(tail -1 "$OUT/log")"
    else
        echo "FAIL wishtest-gtk"; grep -E 'error' "$OUT/err" | head; grep -E 'FAIL' "$OUT/log" | head; fail=1
    fi
else
    echo "skip wishtest-gtk (needs Linux with GTK3 for Python)"
fi

# The five sounds a wish may play are made from nothing but sines and noise.
if command -v python3 >/dev/null 2>&1 && [ -f "$ROOT/tools/wishtest-sounds.py" ]; then
    if python3 "$ROOT/tools/wishtest-sounds.py" >"$OUT/snd" 2>&1; then echo "ok   sounds: $(tail -1 "$OUT/snd")"
    else echo "FAIL sounds"; cat "$OUT/snd"; fail=1; fi
fi

# The engine and the cloud must agree on the document (docs/wish-v1.schema.json).
if [ -f "$ROOT/docs/wish-v1.schema.json" ] && command -v python3 >/dev/null 2>&1; then
    if python3 "$ROOT/tools/wishtest-schema.py" >"$OUT/schema" 2>&1; then
        echo "ok   schema: $(tail -1 "$OUT/schema")"
    else
        echo "FAIL schema agreement"; grep -E 'FAIL|Error' "$OUT/schema" | head -20; fail=1
    fi
fi

# The daemon itself, whole, against fakes: a Wishes service on 127.0.0.1, a
# private D-Bus with a stand-in iio-sensor-proxy, fake sysfs and fake players.
# Linux with libdbus-1-dev, dbus-daemon and curl; elsewhere it says so.
if [ "$(uname)" = Linux ] && pkg-config --exists dbus-1 2>/dev/null; then
    if sh "$ROOT/src/aurwish/build.sh" "$OUT/aurwish" >"$OUT/err" 2>&1; then
        echo "ok   aurwish builds (hardened flags, no warnings as errors)"
    else
        echo "FAIL aurwish build"; cat "$OUT/err"; fail=1
    fi
    if python3 "$ROOT/tools/wishtest-daemon.py" >"$OUT/daemon" 2>&1; then
        echo "ok   daemon: $(tail -1 "$OUT/daemon")"
    else
        echo "FAIL daemon"; grep -vE '^\s+(File|\^)' "$OUT/daemon" | tail -40; fail=1
    fi
else
    echo "skip daemon test (needs Linux with libdbus-1-dev)"
fi

if [ "${1:-}" = --mutants ]; then
    # the daemon-level mutants need what the daemon test needs
    DM=""
    if [ "$(uname)" = Linux ] && pkg-config --exists dbus-1 2>/dev/null && command -v dbus-daemon >/dev/null 2>&1 &&
       command -v curl >/dev/null 2>&1; then DM=--daemon; fi
    if python3 "$ROOT/tools/wishtest-mutants.py" $DM >"$OUT/mut" 2>&1; then
        echo "ok   mutants: $(grep -c '^caught' "$OUT/mut") checks each shown to fail when removed"
    else
        echo "FAIL mutants"; grep -vE '^caught' "$OUT/mut"; fail=1
    fi
fi
exit $fail
