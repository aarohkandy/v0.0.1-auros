#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  Fuzz Ferry's parsers of hostile data: libFuzzer + AddressSanitizer +
#  UndefinedBehaviorSanitizer, seeded with the hives the suite builds.
#
#    sh fuzz/run.sh [SECONDS]        (from src/ferry; needs clang)
#
#  Anything found lands in fuzz/found/<harness>/; turn each into a file
#  under tests/regress/ once fixed, so the suite keeps it fixed.
# ═══════════════════════════════════════════════════════════════════
set -eu
SECS=${1:-120}
HERE=$(cd "$(dirname "$0")" && pwd)
FERRY=$(dirname "$HERE")
W=${FUZZ_WORK:-$(mktemp -d "${TMPDIR:-/tmp}/ferryfuzz.XXXXXX")}
mkdir -p "$W" "$HERE/found"
CFLAGS="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined"

# Seeds: real-shaped hives from the fixture builder, and odd names.
sh "$FERRY/tests/mkfakewin.sh" "$W/win" >/dev/null
mkdir -p "$W/seed-hive" "$W/seed-names"
cp "$W/win/Windows/System32/config/SOFTWARE" "$W/win/Windows/System32/config/SYSTEM" \
   "$W/win/users/Bob/NTUSER.DAT" "$W/seed-hive/"
for f in "$FERRY"/tests/regress/hive-*; do [ -f "$f" ] && cp "$f" "$W/seed-hive/"; done
mkdir -p "$W/seed-marks"
cp "$FERRY/tests/bookmarks.json" "$W/seed-marks/" 2>/dev/null || true
printf '\001photo.jpg' > "$W/seed-names/a"
printf '\002%s.mp3' "$(printf '%0250d' 0)" > "$W/seed-names/b"
printf '\001\346\227\245\346\234\254.txt' > "$W/seed-names/c"

rc=0
for h in hive names marks; do
    clang $CFLAGS -o "$W/fuzz-$h" "$HERE/$h.c"
    mkdir -p "$W/corpus-$h" "$HERE/found/$h"
    echo "fuzzing $h for ${SECS}s"
    "$W/fuzz-$h" -max_total_time="$SECS" -timeout=10 -rss_limit_mb=2048 \
        -artifact_prefix="$HERE/found/$h/" -print_final_stats=1 \
        "$W/corpus-$h" "$W/seed-$h" 2>&1 | grep -E 'stat::number_of_executed_units|stat::new_units_added|ERROR|SUMMARY|Test unit written|BINGO|invariant' || true
    if [ -n "$(find "$HERE/found/$h" \( -name 'crash-*' -o -name 'leak-*' -o -name 'timeout-*' -o -name 'oom-*' \) 2>/dev/null)" ]; then
        echo "  $h: FOUND something, see fuzz/found/$h/"; rc=1
    else
        echo "  $h: nothing found"
    fi
done
[ -n "${FUZZ_WORK:-}" ] || rm -rf "$W"
exit $rc
