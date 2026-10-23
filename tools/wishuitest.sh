#!/bin/sh
# wishuitest.sh — build and run tools/wishuitest.c (Wishes, what she sees),
# then prove each check can fail: break the thing it checks, in a COPY of
# the tree, and require the test to go red. A test that cannot fail is not
# a test (docs/handoff/FIELD-MANUAL.md, culture rule 2).
#
#   sh tools/wishuitest.sh            # from the repository root; needs cc, python3
#   sh tools/wishuitest.sh --no-mutants
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/wishuitest.$$
trap 'rm -rf "$T"' EXIT
mkdir -p "$T"

build() {   # $1 = tree root, $2 = output
    cc -O1 -std=gnu11 -w -o "$2" "$1/tools/wishuitest.c" \
       "$1/src/aurshell/foot.c" "$1/src/aurshell/run.c" "$1/src/aurshell/draw.c" \
       "$1/src/aurshell/shellcommon.c" "$1/src/aurshell/anim.c" \
       "$1"/src/aurshell/layouts/[a-z]*.c "$1/src/common/theme.c" \
       "$1/src/common/font.c" "$1/src/common/png.c" \
       -I "$1/src/aurshell" -I "$1/src/common" -lm
}

echo "== the real tree"
build "$ROOT" "$T/real"
(cd "$ROOT" && "$T/real") | tee "$T/real.log" | tail -4
grep -q ' 0 failed$' "$T/real.log" || { echo "the unbroken tree fails its own test"; exit 1; }
[ "${1:-}" = "--no-mutants" ] && exit 0

# Each mutant: file ^ what it breaks ^ text to find ^ text to put in its place.
# (A caret divides the fields; "\n" in a field is a newline.) The find
# text must occur exactly once; a mutant that does not apply is itself a
# failure, so a refactor cannot quietly turn this into nothing.
cat > "$T/mutants.txt" <<'EOF'
wish.c^Escape on the confirmation says Yes^wish_target t = { .kind = WT_NO };^wish_target t = { .kind = WT_YES };
wish.c^Enter presses a default button^if (W.sel >= 0 && W.sel < g.n) act(c, &g.t[W.sel]);^act(c, &g.t[W.sel >= 0 && W.sel < g.n ? W.sel : 0]);
wish.c^the same words are sent again within 30 s^now_ms() - W.last_ms < SAME_TEXT_MS &&^0 &&
wish.c^waiting never gives up^now_ms() - W.req_t0 > WAIT_GIVE_UP_MS^now_ms() - W.req_t0 > 100 * WAIT_GIVE_UP_MS
wish.c^two letters are sent^if (strlen(plain) < 3) {^if (strlen(plain) < 0) {
wish.c^Undo lasts for ever^#define UNDO_MS   (10 * 60 * 1000)^#define UNDO_MS   (100 * 60 * 1000)
wish.c^an overlong line is tolerated^if (len > WLINE_MAX) { k_close(c); return 1; }^if (0) { k_close(c); return 1; }
wish.c^control characters reach the screen^if (ch < 0x20) ch = ' ';^if (0) ch = ' ';
wish.c^http addresses are opened^if (strncmp(u, "https://", 8) || strlen(u) > 200) return 0;^if (strlen(u) > 200) return 0;
wish.c^a quote in an address is opened^|| *p == '"' || *p == '\'' || *p == '\\' ||^||
wish.c^any look name is applied^if (is_one_of(look, LOOKS)) {\n                char old[24];^if (1) {\n                char old[24];
wish.c^Yes and No differ in size^for (int i = 0; i < n; i++) w[i] = maxw;^for (int i = 0; i < n; i++) w[i] = i ? maxw - 7 : maxw;
wish.c^buttons shrink below 48 px^if (g->btn_h < 48) g->btn_h = 48;^if (g->btn_h < 40) g->btn_h = 40;
wish.c^a refused consent still sends her words^if (W.consent == 0) { go(WP_CONSENT); return; }^if (0) { go(WP_CONSENT); return; }
wishover.c^a fill is not held to 10 seconds^if (seconds > WO_FILL_MAX_S) seconds = WO_FILL_MAX_S;  /* §2.2 cap, here too */^
wishover.c^the key that ends a fill reaches the program^c->wishfill_open = 0;\n    return 1;\n}\n\nvoid wishover_motion^c->wishfill_open = 0;\n    return 0;\n}\n\nvoid wishover_motion
wishover.c^a card takes the keyboard^if (!O.fill) return 0;^if (!O.fill && !O.n) return 0;
wishover.c^the release of the press that ended a fill is not used up^O.fill = 0;\n        c->wishfill_open = 0;\n        O.ate_press = 1;^O.fill = 0;\n        c->wishfill_open = 0;
wishover.c^Keep this? never puts it back^now - k->t0 >= (uint64_t)WO_KEEP_S * 1000u^now - k->t0 >= 100u * (uint64_t)WO_KEEP_S * 1000u
wishover.c^a fill has no way to stop its wish^if (O.wid[0]) add(g, g->fill_stop, WOT_STOP, -1);^
wishover.c^a card is taller than half the screen^if (h <= full_h / 2 || lines == 1) break;^if (1) break;
wishover.c^the PNG decoder reads past its input^if (s->ip >= s->inn) return -1;^if (0) return -1;
EOF

n=0; bad=0
while IFS='^' read -r file what find repl; do
    [ -n "$file" ] || continue
    n=$((n + 1))
    M="$T/m$n"
    mkdir -p "$M"
    cp -R "$ROOT/src" "$ROOT/tools" "$M/" 2>/dev/null || true
    if ! FILE="$M/src/aurshell/$file" FIND="$find" REPL="$repl" python3 - <<'PY'
import os, sys
p = os.environ["FILE"]
s = open(p).read()
find = os.environ["FIND"].replace("\\n", "\n")
repl = os.environ["REPL"].replace("\\n", "\n")
if s.count(find) != 1:
    sys.stderr.write("   find text occurs %d times\n" % s.count(find))
    sys.exit(3)
open(p, "w").write(s.replace(find, repl))
PY
    then
        echo "  BAD MUTANT $n ($what): it does not apply"
        bad=$((bad + 1)); rm -rf "$M"; continue
    fi
    if ! build "$M" "$M/bin" 2>"$M/cc.err"; then
        echo "  BAD MUTANT $n ($what): it does not compile"
        head -3 "$M/cc.err"
        bad=$((bad + 1)); rm -rf "$M"; continue
    fi
    # A mutant may crash instead of failing a check; that is red too.
    if (cd "$ROOT" && timeout 60 "$M/bin" >"$M/out.log" 2>&1); then
        echo "  SURVIVED  $n: $what"
        bad=$((bad + 1))
    else
        echo "  killed    $n: $what"
    fi
    rm -rf "$M"
done < "$T/mutants.txt"

echo
if [ "$bad" -eq 0 ]; then echo "all $n mutants killed"; else echo "$bad of $n mutants SURVIVED or were bad"; exit 1; fi
