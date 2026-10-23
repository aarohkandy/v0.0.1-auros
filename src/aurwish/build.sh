#!/bin/sh
# build.sh -- build aurwish.
#
#   sh src/aurwish/build.sh OUT            the real thing (hardened, no test hooks)
#   TEST=1 sh src/aurwish/build.sh OUT     the test build (-DAURWISH_TEST: paths come from
#                                          the environment, plain http allowed, ASan+UBSan)
#
# Needs libdbus-1-dev (pkg-config dbus-1). build/forge runs the first form
# inside the image, only when the profile says wishes="on" (owner decision D14).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:?usage: build.sh OUT}
S=$HERE/../aurshell
C=$HERE/../common
SRC="$HERE/main.c $HERE/json.c $HERE/text.c $HERE/wish.c $HERE/store.c $HERE/engine.c \
     $HERE/cloud.c $HERE/answer.c $HERE/sensors.c $S/power.c $S/run.c $C/theme.c"
LIBS="$(pkg-config --cflags --libs dbus-1) -lm"
if [ -n "${TEST:-}" ]; then
    # shellcheck disable=SC2086
    ${CC:-cc} -std=gnu11 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
        -DAURWISH_TEST -I "$HERE" -o "$OUT" $SRC $LIBS
else
    if [ -f "$HERE/../../build/cflags" ]; then . "$HERE/../../build/cflags"; fi
    # shellcheck disable=SC2086
    ${CC:-gcc} ${HARDEN_CFLAGS:--O2} -std=gnu11 -Wall -Wextra -I "$HERE" -o "$OUT" $SRC $LIBS
fi
