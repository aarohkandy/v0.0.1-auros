#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  What answer.sh tells the screen for each thing Ferry can say.
#
#  The screen believes `result=`. "Your files are here" must follow
#  result=ok and nothing else, and "ok" must never mean "nothing came"
#  or "I could not tell whose files". answer.sh is run with a stand-in
#  for Ferry (one that exits as told and writes the done-signal) and a
#  stand-in for aurfirst's request, so only the mapping is under test;
#  Ferry itself is tests/run.sh's, and the whole chain under systemd is
#  tests/sandbox.sh's.
#
#    sh tests/answertest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
ANSWER="$REPO/rootfs/usr/lib/auros/answer.sh"
T=$(mktemp -d "${TMPDIR:-/tmp}/answertest.XXXXXX")
trap 'rm -rf "$T"' EXIT
trap 'rm -rf "$T"; exit 130' INT TERM HUP PIPE
pass=0; fail=0
ok()  { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad() { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; }
eq()  { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2', want '$3')"; fi; }

mkdir -p "$T/bin" "$T/out" "$T/state" "$T/fstate"
# aurfirst: `request` hands over the word in $T/word; `ferry` says the gate is open.
cat > "$T/bin/aurfirst" <<STUB
#!/bin/sh
case "\$1" in request) cat "$T/word" ;; *) exit 0 ;; esac
STUB
# ferry: exits as told, leaves the done-signal like the real one, and records how it was called.
cat > "$T/ferry" <<STUB
#!/bin/sh
echo "\$*" >> "$T/ferry.args"
case "\$1" in
    run) printf 'kind=import\nresult=x\nfiles=%s\nat=now\n' "\${STUB_FILES:-5}" > "$T/out/ferry.done"
         exit \${STUB_RC:-0} ;;
    report) exit 0 ;;
esac
STUB
chmod +x "$T/bin/aurfirst" "$T/ferry"
printf 'Bob\nHarold\n' > "$T/fstate/users"

ask() { # WORD RC FILES -> the result file
    rm -f "$T/out/result" "$T/ferry.args"; printf '%s\n' "$1" > "$T/word"
    STUB_RC=$2 STUB_FILES=${3:-5} AUROS_ANSWER_OUT="$T/out" AUROS_STATE="$T/state" AUROS_BIN="$T/bin" \
        AUROS_LOG="$T/log" AUROS_FERRY="$T/ferry" FERRY_STATE="$T/fstate" sh "$ANSWER" >/dev/null 2>&1
    cat "$T/out/result" 2>/dev/null
}
line() { printf '%s\n' "$1" | sed -n "s/^$2=//p" | head -1; }

R=$(ask import 0 5);  eq "all came, 5 files: ok" "$(line "$R" result)" ok;  eq "...with the count" "$(line "$R" files)" 5
R=$(ask import 0 0);  eq "Ferry exits 0 but nothing came: NOT ok (empty)" "$(line "$R" result)" empty
                      eq "...and says so" "$(line "$R" note)" "there were no files on the Windows side to bring across"
R=$(ask import 2 5);  eq "something did not come: partial" "$(line "$R" result)" partial
R=$(ask import 3 0);  eq "Windows not shut down properly: refused" "$(line "$R" result)" refused
R=$(ask import 6 0);  eq "several accounts: choose" "$(line "$R" result)" choose
                      eq "...with the names to ask with" "$(line "$R" users)" "Bob,Harold"
R=$(ask import 1 0);  eq "nothing could be brought: failed" "$(line "$R" result)" failed
R=$(ask import 4 0);  eq "not enough room on this computer: failed, nothing copied" "$(line "$R" result)" failed
case "$(line "$R" note)" in *"not enough room on this computer"*) ok "...and it says so" ;; *) bad "...and it says so" ;; esac
R=$(ask import 124 0); eq "stopped for taking too long: failed, in her words" "$(line "$R" result)" failed
case "$(line "$R" note)" in *"carries on where it stopped"*) ok "...and it says pressing it again carries on" ;; *) bad "...it says pressing it again carries on" ;; esac
R=$(ask import 0 5)
eq "no name given: Ferry is not told one" "$(cat "$T/ferry.args" | head -1)" "run --auto --plain"
R=$(ask import:Harold 0 5)
eq "import:Harold: Ferry is told --user Harold" "$(head -1 "$T/ferry.args")" "run --auto --plain --user Harold"
eq "...and the answer is an ordinary ok" "$(line "$R" result)" ok
eq "...to the request it was (the screen compares)" "$(line "$R" request)" import
R=$(ask 'import:-rf' 0 5)
eq "a name that starts with a dash is not accepted" "$(line "$R" result)" failed
[ ! -s "$T/ferry.args" ] && ok "...and Ferry was not run" || bad "...and Ferry was not run"
R=$(ask 'import:José Nuñez' 0 5)
eq "a name with spaces and accents reaches Ferry as ONE argument" "$(head -1 "$T/ferry.args")" "run --auto --plain --user José Nuñez"

echo
printf 'answer.sh mapping: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
