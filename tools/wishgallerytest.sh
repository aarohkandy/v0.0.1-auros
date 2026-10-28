#!/bin/sh
# wishgallerytest.sh — build tools/wishgallerytest.c against the engine's own
# validator (stream W1), run it, then break the gallery's table five ways, in a
# copy, and require the test to go red every time.
#
#   sh tools/wishgallerytest.sh            # from the repository root; needs cc, python3
#   sh tools/wishgallerytest.sh --no-mutants
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/wishgallerytest.$$
trap 'rm -rf "$T"' EXIT
mkdir -p "$T"
SAN=
echo 'int main(void){return 0;}' > "$T/san.c"
if cc -fsanitize=address,undefined -fno-sanitize-recover=all -o "$T/san" "$T/san.c" 2>/dev/null && "$T/san"; then
    SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
fi
build() {   # $1 = tree root, $2 = out
    cc -std=gnu11 -g -O1 -w $SAN -I "$1/src/aurwish" -o "$2" "$1/tools/wishgallerytest.c" \
       "$1/src/aurwish/json.c" "$1/src/aurwish/text.c" "$1/src/aurwish/wish.c" "$1/src/aurwish/store.c" -lm
}
echo "== the real table"
build "$ROOT" "$T/real"
"$T/real" | tee "$T/real.log" | tail -4
grep -q ' 0 failed$' "$T/real.log" || { echo "the unbroken table fails its own test"; exit 1; }
[ "${1:-}" = "--no-mutants" ] && exit 0

cat > "$T/mutants.txt" <<'EOM'
a stop word is offered as a word she can type^{ "kitten",   "kitten",   "kitten" },^{ "hello",    "hello",    "hello" },
an emoji the engine does not allow is offered^{ "Heart",     "\xE2\x9D\xA4\xEF\xB8\x8F", "hearts" },^{ "Ghost",     "\xF0\x9F\x91\xBB", "ghosts" },
a blank is left in a document^"{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@1\"},\"do\":[{\"type\":\"speak\",\"text\":\"@2\"}]}" },^"{\"v\":1,\"when\":{\"type\":\"time\",\"at\":\"@1\"},\"do\":[{\"type\":\"speak\",\"text\":\"@3\"}]}" },
a time in the quiet hours is offered for sounds and screens^{ "1:00 in the afternoon","13:00", "1:00 pm" },^{ "1:00 in the afternoon","05:00", "5:00 am" },
a choice out of range is built^if (!s || !choice || choice[k] < 0 || choice[k] >= s->n) return -1;^if (!s || !choice) return -1;
the sentence is not what the document says^"Remind me at @1: @2",^"Remind me at @1",
EOM
n=0; bad=0
while IFS='^' read -r what find repl; do
    [ -n "$what" ] || continue
    n=$((n + 1))
    M="$T/m$n"; mkdir -p "$M"
    cp -R "$ROOT/src" "$ROOT/tools" "$M/" 2>/dev/null || true
    if ! FILE="$M/src/aurshell/wishgallery.c" FIND="$find" REPL="$repl" python3 - <<'PY'
import os, sys
s = open(os.environ["FILE"]).read()
find, repl = os.environ["FIND"], os.environ["REPL"]
if s.count(find) != 1:
    sys.stderr.write("   find text occurs %d times\n" % s.count(find)); sys.exit(3)
open(os.environ["FILE"], "w").write(s.replace(find, repl))
PY
    then echo "  BAD MUTANT $n ($what): it does not apply"; bad=$((bad + 1)); rm -rf "$M"; continue; fi
    if ! build "$M" "$M/bin" 2>"$M/cc.err"; then echo "  BAD MUTANT $n ($what): it does not compile"; head -3 "$M/cc.err"; bad=$((bad + 1)); rm -rf "$M"; continue; fi
    if "$M/bin" >"$M/out.log" 2>&1; then echo "  SURVIVED  $n: $what"; bad=$((bad + 1)); else echo "  killed    $n: $what"; fi
    rm -rf "$M"
done < "$T/mutants.txt"
echo
if [ "$bad" -eq 0 ]; then echo "all $n mutants killed"; else echo "$bad of $n mutants SURVIVED or were bad"; exit 1; fi
