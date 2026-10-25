#!/bin/sh
# nightlighttest.sh — CPU tint correctness, bounds and timing evidence.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/nightlighttest.$$
trap 'rm -rf "$T"' EXIT
mkdir -p "$T"

SAN=
printf 'int main(void){return 0;}\n' > "$T/san.c"
if cc -fsanitize=address,undefined -fno-sanitize-recover=all -o "$T/san" "$T/san.c" 2>/dev/null && "$T/san"; then
    SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
fi
cc -O2 -g -std=gnu11 -Wall -Wextra -Werror $SAN -o "$T/nightlighttest" \
   "$ROOT/tools/nightlighttest.c" "$ROOT/src/aurshell/nightlight.c" \
   "$ROOT/src/aurshell/draw.c" -I "$ROOT/src/aurshell" -lm
"$T/nightlighttest" "$ROOT/src/aurshell/main.c"
