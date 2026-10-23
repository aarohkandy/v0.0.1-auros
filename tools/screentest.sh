#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  screentest — does the staging screen say where the install is?
#
#  src/aurscreen paints the one restart from the lines aurstage says.
#  It finds the seven steps by the sentence that starts each, so the
#  check that matters most is that those sentences are still in
#  src/aurstage/install.c, word for word: reword one there and the
#  screen would sit on the step before it for the rest of the install,
#  with nothing failing anywhere.
#
#  Then, off-screen (--state, --png), on transcripts:
#    an install part way           the right step, the bar, the warning
#    an install that finished      every step done, no warning
#    a refusal                     "was not installed", in red, verdict kept
#    a restore                     its own title, no install steps
#    garbage on the pipe           no crash
#  and, if a serial log from a real run is given, that it ends handed
#  over with every step reached.
#
#    sh tools/screentest.sh [SERIAL-LOG-OF-A-REAL-INSTALL]
# ═══════════════════════════════════════════════════════════════════
set -u
LOG="${1:-}"       # before anything below uses `set --`
case "$LOG" in ""|/*) ;; *) LOG="$PWD/$LOG" ;; esac
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-62s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-62s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

T=$(mktemp -d "${TMPDIR:-/tmp}/screen.XXXXXX")
trap 'rm -rf "$T"' EXIT
B="$T/aurscreen"
gcc -O2 -std=gnu11 -Wall -Wextra -Werror -o "$B" src/aurscreen/screen.c \
    src/aurshell/kms.c src/aurshell/draw.c src/common/font.c src/common/png.c -lm \
    || { echo "aurscreen did not build"; exit 1; }

echo
echo "The staging screen"
echo
echo "  the sentences it follows are the ones aurstage says"
sed -n '/^} STEP\[\] = {/,/^};/p' src/aurscreen/screen.c \
    | sed -n 's/^ *{ "\([^"]\+\)", *".*/\1/p' > "$T/starts"
n=$(wc -l < "$T/starts")
[ "$n" -ge 5 ] && ok "read $n step sentences from screen.c" || bad "read the step sentences from screen.c" "got $n"
while IFS= read -r s; do
    grep -qF "stage_say(\"$s" src/aurstage/install.c \
      && ok "install.c says: $s" || bad "install.c says: $s" "reworded or gone"
done < "$T/starts"

say() { printf 'aurstage: %s\n' "$@"; }
tm() { printf 'aurstage-time %s\n' "$1"; }
part() {
    tm 1; say "AurOS staging environment" "" "── installing AurOS ──" "drive    healthy"
    tm 5; say "" "Saving this computer's Windows startup, so it can be put back." "saved    12 MB"
    say "" "Everything AurOS can check has been checked."
    tm 60; say "" "Making room on the Windows drive. This is the only part" \
        "that cannot be undone. Do not turn the computer off." "windows  61.2 GiB"
    tm 400; say "" "Copying AurOS onto this computer."
    printf 'aurstage-progress 30\naurstage-progress 47\n'
}
st() { "$B" --mode "$1" --state; }
# a field of the state line
fld() { printf '%s\n' "$1" | sed -n "1s/^\(.* \)\{0,1\}$2=\([^ ]*\).*/\2/p"; }

echo
echo "  states"
S=$(part | st install)
[ "$(fld "$S" step)" = 4 ] && [ "$(fld "$S" pct)" = 47 ] && [ "$(fld "$S" stopped)" = 0 ] \
    && ok "part way: copying, 47%, not stopped" || bad "part way: copying, 47%, not stopped" "$S"
[ "$(fld "$S" position)" = 4 ] && [ "$(fld "$S" of)" = 7 ] \
    && ok "...step 4 of 7" || bad "...step 4 of 7" "$S"
case "$S" in *"para=Putting AurOS on this computer."*) ok "...in her words, written for the step" ;;
    *) bad "...in her words, written for the step" "$S" ;; esac

# NOTHING A TECHNICIAN WROTE reaches her, whatever aurstage says.
case "$S" in *GiB*|*MB*|*"/dev"*|*vda*|*"61.2"*|*"drive    healthy"*)
    bad "no size, device or log line reaches the screen" "$S" ;;
    *) ok "no size, device or log line reaches the screen" ;; esac

# THE ESTIMATE IS MEASURED. A computer that takes the typical time to save
# the way back and to make room (about 11 minutes) and is a tenth through
# copying has most of the copy still to come.
E=$(fld "$S" eta)
if [ "$E" -gt 120 ] 2>/dev/null && [ "$E" -lt 1800 ]; then ok "about N minutes left is a number it measured ($E s)"
else bad "about N minutes left is a number it measured" "eta=$E"; fi
# And it follows how fast THIS computer is: the same transcript on one that
# is three times slower at the early steps must promise longer.
SLOW=$( { tm 1; say "AurOS staging environment"; tm 15; say "" "Saving this computer's Windows startup, so it can be put back."
          tm 190; say "" "Making room on the Windows drive. This is the only part"
          tm 1800; say "" "Copying AurOS onto this computer."; tm 1850; printf 'aurstage-progress 5\n'; } | st install)
FAST=$( { tm 1; say "AurOS staging environment"; tm 6; say "" "Saving this computer's Windows startup, so it can be put back."
          tm 30; say "" "Making room on the Windows drive. This is the only part"
          tm 120; say "" "Copying AurOS onto this computer."; tm 170; printf 'aurstage-progress 5\n'; } | st install)
[ "$(fld "$SLOW" eta)" -gt "$(fld "$FAST" eta)" ] 2>/dev/null \
    && ok "a slower computer is promised longer than a faster one" \
    || bad "a slower computer is promised longer than a faster one" "slow=$(fld "$SLOW" eta) fast=$(fld "$FAST" eta)"
S0=$( { tm 1; say "AurOS staging environment"; tm 3; } | st install)
[ "$(fld "$S0" eta)" = "-1" ] && ok "in the first seconds it promises nothing" \
    || bad "in the first seconds it promises nothing" "$S0"

S=$( { part; say "" "Making this computer able to start AurOS." "" \
        "Checking that this computer works under AurOS." "" "Writing the new layout." \
        "handing over to the system on /dev/vda3, in this same boot"; } | st install)
[ "$(fld "$S" step)" = 7 ] && [ "$(fld "$S" handover)" = 1 ] \
    && ok "finished: last step reached, handed over" || bad "finished: last step reached, handed over" "$S"

# THE FAST WAY has its own list, and the steps Windows already did are not on it.
S=$( { printf 'aurstage-mode gap\n'; tm 1; say "AurOS staging environment"; tm 20
       say "" "Checking the copy of AurOS that Windows wrote."; } | st install)
[ "$(fld "$S" gap)" = 1 ] && [ "$(fld "$S" of)" = 5 ] && [ "$(fld "$S" position)" = 2 ] \
    && ok "the fast way: step 2 of 5, no 'making room', no 'copying'" \
    || bad "the fast way: step 2 of 5" "$S"
case "$S" in *"para=Checking the copy of AurOS that Windows put beside itself."*) ok "...in her words" ;;
    *) bad "...in her words" "$S" ;; esac

# A FAILURE: what happened to her task, where Windows is, what happens
# next, and a number to read out -- and nothing the machine wrote.
S=$( { part; say "WARNING: what was written to this computer's disk did not read back the same, 475 MB in." \
        "aurstage-report v1 verdict=write-failed record=failed -"; printf 'aurstage-now smaller\naurstage-countdown 41\n'; } | st install)
[ "$(fld "$S" stopped)" = 1 ] && [ "$(fld "$S" verdict)" = write-failed ] \
    && ok "a failure: stopped, verdict kept" || bad "a failure: stopped, verdict kept" "$S"
case "$S" in *"para=Your Windows files are safe. Windows will start as usual."*) ok "...and says where Windows is" ;;
    *) bad "...and says where Windows is" "$S" ;; esac
case "$S" in *"para=This computer will restart by itself in 41 seconds."*) ok "...and that it restarts by itself, in how long" ;;
    *) bad "...and that it restarts by itself, in how long" "$S" ;; esac
case "$S" in *"para=To try again, open AurOS Setup in Windows and press Install AurOS."*) ok "...and what to press, by its name on the button" ;;
    *) bad "...and what to press, by its name on the button" "$S" ;; esac
case "$S" in *"475"*|*"MB"*|*AurBridge*|*"off and on"*|*"--"*|*"Start installing"*)
    bad "...with no size, no codename, no 'off and on again'" "$S" ;;
    *) ok "...with no size, no codename, no 'off and on again'" ;; esac
C=$(fld "$S" code)
[ "$C" = "$("$B" --code write-failed | tr ' ' _)" ] && [ "$(printf '%s' "$C" | wc -c)" = 11 ] \
    && ok "...and a nine-digit number a helper can look up ($C)" \
    || bad "...and a nine-digit number a helper can look up" "code=$C"
S=$( { part; say "aurstage-report v1 verdict=write-failed record=failed -"; } | st install)
[ "$(fld "$S" code)" = "$C" ] && ok "...the same number every time for the same stop" || bad "...the same number every time" "$S"
S=$( { part; say "aurstage-report v1 verdict=gap-damaged record=refused -"; } | st install)
[ "$(fld "$S" code)" != "$C" ] && ok "...and a different one for a different stop" || bad "...a different one for a different stop" "$S"

S=$( { say "" "Everything AurOS can check has been checked." \
        "aurstage-report v1 verdict=refused-bitlocker record=refused -"; printf 'aurstage-now untouched\n'; } | st install)
[ "$(fld "$S" stopped)" = 1 ] && ok "a refusal: stopped" || bad "a refusal: stopped" "$S"
case "$S" in *"para=Nothing on this computer has been changed."*) ok "...and it says nothing has been changed (it is true)" ;;
    *) bad "...and it says nothing has been changed" "$S" ;; esac
# Where the disk has changed, it must NOT say so.
S=$( { part; say "aurstage-report v1 verdict=copy-failed record=failed -"; printf 'aurstage-now smaller\n'; } | st install)
case "$S" in *"nothing on this computer has been changed"*|*"Nothing on this computer has been changed"*)
    bad "after the shrink it never says nothing has changed" "$S" ;;
    *) ok "after the shrink it never says nothing has changed" ;; esac
# And no code on a screen where nothing went wrong.
S=$(part | st install)
[ "$(fld "$S" code)" = "-" ] && ok "no number to read out when nothing went wrong" || bad "no number when nothing went wrong" "$S"

S=$( { say "" "Putting Windows back." ; printf 'aurstage-progress 80\n'; } | st restore)
[ "$(fld "$S" mode)" = 1 ] && [ "$(fld "$S" pct)" = 80 ] && [ "$(fld "$S" stopped)" = 0 ] \
    && ok "a restore: its own mode, its own bar" || bad "a restore: its own mode, its own bar" "$S"

S=$( { say "" "── putting Windows back ──" \
        "WARNING: there is no saved copy of this computer's Windows startup" \
        "         Nothing has been changed." \
        "aurstage-report v1 verdict=restore-nothing-saved record=none -"; } | st restore)
[ "$(fld "$S" stopped)" = 1 ] && ok "a restore with nothing saved: stopped" || bad "a restore with nothing saved: stopped" "$S"
case "$S" in *"para=There is no saved copy"*) ok "...and says why, in the words aurstage wrote for her" ;;
    *) bad "...and says why" "$S" ;; esac
S=$( { say "WARNING: no readable saved copy of this computer's Windows startup" \
        "aurstage-report v1 verdict=restore-no-usable-copy record=none -"; } | st restore)
[ "$(fld "$S" stopped)" = 1 ] && ok "a restore with no usable copy: stopped" || bad "a restore with no usable copy: stopped" "$S"
S=$( { say "restoring /dev/vda" "" "aurstage-report v1 verdict=restored record=done -" "" \
        "Windows is back. This computer will switch itself off;"; } | st restore)
[ "$(fld "$S" stopped)" = 0 ] && [ "$(fld "$S" finished)" = 1 ] \
    && ok "a restore that worked: finished, not stopped" || bad "a restore that worked: finished, not stopped" "$S"
case "$S" in *"para=The screen goes black, then Windows starts. About two minutes."*) ok "...and says what the black screen is" ;;
    *) bad "...and says what the black screen is" "$S" ;; esac

S=$( say "AurOS staging environment" "this is the one restart; nothing has been changed yet" \
         "5 drivers loaded" "this machine has 3 disks" | st install)
case "$S" in *"para=AurOS is starting. About one minute. Please don't press anything."*)
    case "$S" in *"drivers loaded"*) bad "start-up: her sentence, not the technician's lines" "$S" ;;
        *) ok "start-up: AurOS is starting, about a minute, don't press anything" ;; esac ;;
    *) bad "start-up: AurOS is starting, about a minute" "$S" ;; esac
S=$( say "AurOS staging environment" "5 drivers loaded" "── putting Windows back ──" \
         "checking the saved copy on /dev/vdb" "restoring /dev/vda" | st restore)
case "$S" in *"para=Putting Windows back exactly as it was."*) ok "a restore under way: says what it is doing" ;;
    *) bad "a restore under way: says what it is doing" "$S" ;; esac

S=$( { head -c 20000 /dev/urandom; printf '\naurstage-progress 999\naurstage-progress -5\n'; } | st install)
[ "$(fld "$S" pct)" = 0 ] && ok "garbage and out-of-range numbers: no crash, clamped" \
    || bad "garbage and out-of-range numbers: no crash, clamped" "$S"

echo
echo "  pictures"
for sz in "1024 768" "1366 768" "1920 1080" "800 600"; do
    set -- $sz
    if part | "$B" --mode install --png "$T/s.png" --size "$1" "$2" 2>/dev/null && \
       [ "$(head -c 8 "$T/s.png" | od -An -c | tr -d ' ')" = '211PNG\r\n032\n' ]; then
        ok "paints at $1x$2"
    else bad "paints at $1x$2"; fi
done

if [ -n "$LOG" ]; then
    echo
    echo "  a real install's serial log: $LOG"
    S=$(tr -d '\r' < "$LOG" | grep -a '^aurstage' | st install)
    case "$S" in *" step=7 "*"handover=1 "*) ok "followed it to the end: every step, handed over" ;;
        *) bad "followed it to the end: every step, handed over" "$S" ;; esac
fi

echo
echo "screentest: $((checked - fail))/$checked"
[ "$fail" = 0 ]
