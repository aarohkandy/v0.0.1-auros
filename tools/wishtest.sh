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

# The engine and the cloud must agree on the document (docs/wish-v1.schema.json).
if [ -f "$ROOT/docs/wish-v1.schema.json" ] && command -v python3 >/dev/null 2>&1; then
    if python3 "$ROOT/tools/wishtest-schema.py" >"$OUT/schema" 2>&1; then
        echo "ok   schema: $(tail -1 "$OUT/schema")"
    else
        echo "FAIL schema agreement"; grep -E 'FAIL|Error' "$OUT/schema" | head -20; fail=1
    fi
fi

# The daemon itself builds only where its Linux headers are.
if [ "$(uname)" = Linux ] && [ -f "$W/build.sh" ] && pkg-config --exists dbus-1 2>/dev/null; then
    if sh "$ROOT/src/aurwish/build.sh" "$OUT/aurwish" >"$OUT/err" 2>&1; then
        echo "ok   aurwish builds"
    else
        echo "FAIL aurwish build"; cat "$OUT/err"; fail=1
    fi
fi

if [ "${1:-}" = --mutants ]; then
    if python3 "$ROOT/tools/wishtest-mutants.py" >"$OUT/mut" 2>&1; then
        echo "ok   mutants: $(grep -c '^caught' "$OUT/mut") checks each shown to fail when removed"
    else
        echo "FAIL mutants"; grep -vE '^caught' "$OUT/mut"; fail=1
    fi
fi
exit $fail
