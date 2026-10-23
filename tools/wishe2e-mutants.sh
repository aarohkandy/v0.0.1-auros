#!/bin/sh
# wishe2e-mutants.sh — the end-to-end test (tools/wishe2e.sh) is shown to fail when the
# shell's half is broken: each mutant copies the tree, makes one exact edit that must
# match once, runs the end-to-end test, and must see it go red. About two minutes each.
#
#   sh tools/wishe2e-mutants.sh
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/wishe2e-mutants.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
bad=0
mut() {
    name=$1; file=$2; from=$3; to=$4
    rm -rf "$T/m" && mkdir "$T/m" && cp -R "$ROOT/src" "$ROOT/tools" "$T/m/"
    if ! python3 -I - "$T/m/$file" "$from" "$to" <<'PY'
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
if s.count(a) != 1:
    sys.exit(1)
open(p, "w").write(s.replace(a, b))
PY
    then echo "  BAD MUTANT  $name: it does not apply"; bad=$((bad + 1)); return; fi
    if (cd "$T/m" && timeout 300 sh tools/wishe2e.sh >"$T/out" 2>&1); then
        echo "  SURVIVED    $name"; bad=$((bad + 1))
    else
        echo "  killed      $name ($(grep -c FAIL "$T/out") checks red)"
    fi
}
mut "the fill forgets whose wish it is" src/aurshell/wishover.c \
    'copy(O.wid, sizeof O.wid, wid);' ';'
mut "Undo is not offered after a one-off wish" src/aurshell/wish.c \
    'W.undo_until = wid[0] ? 0 : now_ms() + UNDO_MS;' 'W.undo_until = 0;'
mut "the compositor is never given the trigger words" src/aurshell/wish.c \
    'if (wishhook_set) wishhook_set(wp, nw, ap, na);' ';'
[ "$bad" -eq 0 ] && echo "all mutants killed" || { echo "$bad SURVIVED or were bad"; exit 1; }
