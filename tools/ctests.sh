#!/bin/sh
# ctests -- the C unit tests tools/README.md lists, compiled and run
# (stream CIW). tests.yml's `unit` runs it as one step.
#
#   modaltest   one panel covers the desktop at a time; Escape gets out
#   hittest     a click lands where the look painted; nothing highlights that cannot be pressed
#   kiosktest   the school-kiosk allow-list is what the product says it is
#   launchtest  can she start a program from every home
#   nettest     what nmcli prints becomes what she reads
#   powertest   battery and brightness, from constructed machines
#   stridetest  every look at every size, row by row
#   bttest      what bluetoothctl prints becomes what she reads
#   keytest     can she type her wifi password (real keymap, with Shift)
#   padtest     touchpads and mice are told apart the way the kernel reports them
#
# Every test is built and run even when an earlier one fails, and the
# last line says which ones did not pass. A test that does not build is
# a failure, not a skip.
#
#   sh tools/ctests.sh [name ...]       from anywhere
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d "${TMPDIR:-/tmp}/ctests.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM

S=src/aurshell
# What a test needs to link when it draws: the shell's drawing, its looks, its fonts.
DRAW="$S/draw.c $S/shellcommon.c $S/anim.c $S/layouts/*.c src/common/theme.c src/common/font.c"
W=src/aurwl/gen
failed=""

run() {  # run NAME SOURCES... -- built with -lm, then run from the repo root
    n=$1; shift
    case " ${ONLY:-$n} " in *" $n "*) ;; *) return ;; esac
    echo "=== $n"
    # shellcheck disable=SC2086  # the source lists are globs on purpose
    if ! cc -O2 -std=gnu11 -o "$T/$n" tools/$n.c "$@" -I $S -I src/common -lm; then
        echo "ctests: $n did not build"; failed="$failed $n"; return
    fi
    "$T/$n" || { echo "ctests: $n FAILED"; failed="$failed $n"; }
}

ONLY="$*"
run modaltest  $S/foot.c $DRAW
run hittest    $DRAW
run kiosktest  $S/apps.c $DRAW
run launchtest $DRAW
run nettest    $S/net.c $DRAW
run powertest  $S/power.c $S/run.c
run stridetest $DRAW
run bttest     $S/bt.c $S/foot.c $DRAW
run padtest    $S/pad.c
# keytest links the compositor itself, its protocol code and the shell's drawing.
run keytest    src/aurwl/aurwl.c src/aurwl/textinput.c $S/run.c $S/session.c $S/cursor.c $S/foot.c $DRAW \
               $W/text-input-protocol.c $W/xdg-shell-protocol.c $W/xdg-decoration-protocol.c \
               $W/viewporter-protocol.c $W/xdg-output-protocol.c -I src/aurwl -I $W \
               $(pkg-config --cflags --libs wayland-server wayland-client xkbcommon dbus-1)

if [ -n "$failed" ]; then echo "ctests: NOT PASSING:$failed"; exit 1; fi
echo "ctests: all passed"
