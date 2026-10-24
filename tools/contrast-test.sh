#!/bin/sh
# contrast-test -- the shell's palette (tools/contrast.c) in the two looks she can choose, and
# in the three that only a profile turns on. Renders shell.conf from every theme with aurora,
# builds contrast.c, and holds:
#   sandstone (Light) and nocturne (Dark)   every pair, rules included (--strict)
#   ember, moss, slate                       the pairs that carry meaning (rules are advisory)
# Pairs include the words on an accent fill (aurora's @accent|on@ and the shell's primary
# button). tools/theme-test.sh holds the GTK and terminal pairs.
#
#   sh tools/contrast-test.sh        (from anywhere; needs cc, sh, awk)
set -eu
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
cc -O2 -std=gnu11 -o "$T/contrast" tools/contrast.c src/common/theme.c -lm
mkdir -p "$T/st"
for th in themes/*.theme; do
    n=$(basename "$th" .theme)
    printf '%s\n' "$n" > "$T/st/theme"
    tail -n +2 themes/templates/shell.conf.tpl |
        AURORA_THEMES=themes AURORA_TEMPLATES=themes/templates AURORA_STATE="$T/st" AURORA_CACHE="$T/c" \
        ./src/aurora/aurora render > "$T/$n.conf"
done
fail=0
"$T/contrast" --strict "$T/sandstone.conf" "$T/nocturne.conf" || fail=1
"$T/contrast" "$T/ember.conf" "$T/moss.conf" "$T/slate.conf" || fail=1
if [ $fail = 0 ]; then echo "contrast-test: every pair holds"; else echo "contrast-test: FAILED"; fi
exit $fail
