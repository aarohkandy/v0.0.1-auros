#!/bin/sh
# wishpainttest.sh — is any word she has to read ever cut off?
#
# The layout of the Wishes panel and its cards is arithmetic; the words are
# wrapped by the real font. When the two disagree a sentence ends in the
# middle, which no test of rectangles can see (it was found by looking: a
# confirmation sentence stopped after "makes them", a waiting note ran off the
# edge of the screen at her biggest text). This paints every page and every
# card, in every look, at every text size, through the real paint path
# (tools/wishsheet.c) in the repository's own face, and fails if any word was
# cut off or ended in an ellipsis. Then each breakage below must make it fail.
#
#   sh tools/wishpainttest.sh            # from the repository root; needs cc, python3
#   sh tools/wishpainttest.sh --no-mutants
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/wishpaint.$$
trap 'rm -rf "$T"' EXIT
PNG=${WISHPAINT_PNG:-$T/png}     # set WISHPAINT_PNG=dir to keep the pictures and look at them
mkdir -p "$T/st" "$PNG"

build() {   # $1 = tree, $2 = output
    cc -O2 -std=gnu11 -w -DAUROS_WISHES=1 -o "$2" "$1/tools/wishsheet.c" \
       "$1/src/aurshell/wishover.c" "$1/src/aurshell/foot.c" "$1/src/aurshell/run.c" \
       "$1/src/aurshell/apps.c" "$1/src/aurshell/draw.c" "$1/src/aurshell/shellcommon.c" \
       "$1/src/aurshell/anim.c" "$1"/src/aurshell/layouts/[a-z]*.c "$1/src/common/theme.c" \
       "$1/src/common/font.c" "$1/src/common/png.c" "$1/src/common/wall.c" \
       -I "$1/src/aurshell" -lm
}

# The theme's settings with its faces pointed at the Atkinson files in the repo.
F=$ROOT/src/aurbridge/fonts
printf 'sandstone\n' > "$T/st/theme"
tail -n +2 "$ROOT/themes/templates/shell.conf.tpl" |
    AURORA_THEMES="$ROOT/themes" AURORA_TEMPLATES="$ROOT/themes/templates" \
    AURORA_STATE="$T/st" AURORA_CACHE="$T/st/c" "$ROOT/src/aurora/aurora" render > "$T/theme.conf" 2>/dev/null
sed -e "s#^font_display .*#font_display = $F/atkinsonnext-800.ttf#" \
    -e "s#^font_text .*#font_text = $F/atkinsonnext-400.ttf#" \
    -e "s#^font_text_bold .*#font_text_bold = $F/atkinsonnext-700.ttf#" \
    -e "s#^font_sans .*#font_sans = $F/atkinsonnext-400.ttf#" "$T/theme.conf" > "$T/shell.conf"
grep -q "atkinsonnext-400" "$T/shell.conf" || { echo "could not point the theme at the repository's fonts"; exit 2; }
for f in atkinsonnext-400 atkinsonnext-700 atkinsonnext-800; do [ -s "$F/$f.ttf" ] || { echo "no $F/$f.ttf"; exit 2; }; done

run() {   # $1 = binary; prints the CLIPPED lines; nonzero if any
    rc=0
    for look in rail taskbar dock tiles; do
        for sz in "1366 768 1.0" "1366 768 2.0" "1024 600 1.0" "1024 600 1.5" "1024 600 2.0"; do
            set -- "$1" $sz
            WISHSHEET_STRICT=1 HOME="$T" "$1" "$T/shell.conf" "$ROOT/shells/$look.shell" "$PNG" "$2" "$3" "$4" \
                > "$T/out.txt" 2>&1 || rc=1
            grep CLIPPED "$T/out.txt" | sed "s/^/  [$4] /" || true
        done
    done
    return $rc
}

echo "== the real tree"
build "$ROOT" "$T/real"
if run "$T/real" > "$T/real.log"; then
    echo "  no word is cut off on any page or card, in any look, at any size       ok"
else
    cat "$T/real.log"; echo "  words are cut off                                                    FAIL"; exit 1
fi
[ "${1:-}" = "--no-mutants" ] && exit 0

# Each mutant: file ^ what it breaks ^ text to find ^ text to put in its place.
cat > "$T/mutants.txt" <<'MUT'
wish.c^the confirmation sentence is not fitted to its box^        for (float px = 26.f * k - 2.5f; px >= 18.f; px -= 2.5f) {^        for (float px = 26.f * k - 2.5f; px >= 99.f; px -= 2.5f) {
wish.c^the waiting note gets one line at any size^        int nl = (est_w(70, g->px_body) + g->cw - 1) / g->cw;^        int nl = 1;
wish.c^the line under My wishes gets one line at any size^        int sub_n = est_w(v->sub_len, g->px_body) > g->cw ? 2 : 1;\n        g->a[A_SUB] = (rect){ g->gx, y, g->cw, lh_body * sub_n };\n        y += lh_body * sub_n + g->gap;\n        int kinds[5]^        int sub_n = 1;\n        g->a[A_SUB] = (rect){ g->gx, y, g->cw, lh_body * sub_n };\n        y += lh_body * sub_n + g->gap;\n        int kinds[5]
wishover.c^a card has three lines at most^        if (need > 5) need = 5;^        if (need > 1) need = 1;
wish.c^a heading's box is shorter than its letters^        g->head_h = (int)((float)g->px_head * 1.35f) * g->head_lines;^        g->head_h = (int)((float)g->px_head * 1.2f) * g->head_lines;
wish.c^the question's buttons are too narrow for their names^        int yes_w = est_w(9, g->px_lead) + 2 * g->pad, no_w = est_w(7, g->px_lead) + 2 * g->pad;^        int yes_w = est_w(4, g->px_lead) + 2 * g->pad, no_w = est_w(4, g->px_lead) + 2 * g->pad;
wish.c^every wish gets two lines whatever it says^        int text_h = (need < 2 ? 2 : need > 4 ? 4 : need) * lh_body;^        int text_h = 2 * lh_body;
wish.c^the offered sentence is not given room^            least = sl + g->group + lh_label + 3 * (int)(18.f * 1.5f);^            least = sl;
MUT
n=0; bad=0
while IFS='^' read -r file what find repl; do
    [ -n "$file" ] || continue
    n=$((n + 1))
    M="$T/m$n"; mkdir -p "$M"
    cp -R "$ROOT/src" "$ROOT/tools" "$ROOT/shells" "$M/" 2>/dev/null || true
    if ! FILE="$M/src/aurshell/$file" FIND="$find" REPL="$repl" python3 - <<'PY'
import os, sys
p = os.environ["FILE"]
s = open(p).read()
a = os.environ["FIND"].replace("\\n", "\n"); b = os.environ["REPL"].replace("\\n", "\n")
if s.count(a) != 1:
    sys.stderr.write("   find text occurs %d times: %s\n" % (s.count(a), a[:60])); sys.exit(3)
open(p, "w").write(s.replace(a, b))
PY
    then echo "  BAD MUTANT $n ($what): it does not apply"; bad=$((bad + 1)); rm -rf "$M"; continue; fi
    if ! build "$M" "$M/bin" 2>"$M/cc.err"; then
        echo "  BAD MUTANT $n ($what): it does not compile"; head -3 "$M/cc.err"; bad=$((bad + 1)); rm -rf "$M"; continue
    fi
    if run "$M/bin" > "$M/out.log" 2>&1; then
        echo "  SURVIVED  $n: $what"; bad=$((bad + 1))
    else
        echo "  killed    $n: $what"
    fi
    rm -rf "$M"
done < "$T/mutants.txt"
echo
if [ "$bad" -eq 0 ]; then echo "all $n mutants killed"; else echo "$bad of $n mutants SURVIVED or were bad"; exit 1; fi
