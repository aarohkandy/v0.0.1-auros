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
#    a refusal                     stopped, verdict kept
#    a restore                     its own steps, its own title
#    garbage on the pipe           no crash
#  and what she sees (docs/DESIGN-DIRECTION.md §5 C P0-3): no size and
#  no why on her page; a helper code, digits only, only when something
#  failed, different for every verdict aurstage can say; "About N
#  minutes left" only once it is measured; "Show details for a helper"
#  pressed once is pressed once; the bold UI face; the Light paper.
#  If a serial log from a real run is given, that it ends handed over
#  with every step reached.
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
sed -n '/^static const struct { const char \*starts, \*label; } STEP/,/^};/p' src/aurscreen/screen.c \
    | sed -n 's/^ *{ "\([^"]\+\)", *".*/\1/p' > "$T/starts"
n=$(wc -l < "$T/starts")
[ "$n" -ge 5 ] && ok "read $n step sentences from screen.c" || bad "read the step sentences from screen.c" "got $n"
while IFS= read -r s; do
    grep -qF "stage_say(\"$s" src/aurstage/install.c \
      && ok "install.c says: $s" || bad "install.c says: $s" "reworded or gone"
done < "$T/starts"

say() { printf 'aurstage: %s\n' "$@"; }
part() {
    say "AurOS staging environment" "" "── installing AurOS ──" "drive    healthy"
    say "" "Saving this computer's Windows startup, so it can be put back." "saved    12 MB"
    say "" "Everything AurOS can check has been checked."
    say "" "Making room on the Windows drive. This is the only part" \
        "that cannot be undone. Do not turn the computer off." "windows  61.2 GiB"
    say "" "Copying AurOS onto this computer."
    printf 'aurstage-progress 30\naurstage-progress 47\n'
}
st() { "$B" --mode "$1" --state; }

echo
echo "  states"
S=$(part | st install)
case "$S" in *"step=3 "*"pct=47 "*"stopped=0 "*) ok "part way: copying, 47%, not stopped" ;;
    *) bad "part way: copying, 47%, not stopped" "$S" ;; esac
case "$S" in *"para=Copying AurOS onto this computer."*) ok "...in aurstage's own words" ;;
    *) bad "...in aurstage's own words" "$S" ;; esac

S=$( { part; say "" "Making this computer able to start AurOS." "" \
        "Checking that this computer works under AurOS." "" "Writing the new layout." \
        "handing over to the system on /dev/vda3, in this same boot"; } | st install)
case "$S" in *"step=6 "*"handover=1 "*) ok "finished: last step reached, handed over" ;;
    *) bad "finished: last step reached, handed over" "$S" ;; esac

S=$( { part; say "aurstage-report v1 verdict=copy-failed record=failed -" "" \
        "Windows itself has not been damaged and will still start."; } | st install)
case "$S" in *"stopped=1 "*"verdict=copy-failed"*) ok "a failure: stopped, verdict kept" ;;
    *) bad "a failure: stopped, verdict kept" "$S" ;; esac
case "$S" in *"para=Windows itself has not been damaged"*) ok "...and what to do is what it shows" ;;
    *) bad "...and what to do is what it shows" "$S" ;; esac

S=$( { say "" "Everything AurOS can check has been checked." \
        "aurstage-report v1 verdict=refused-bitlocker record=refused -"; } | st install)
case "$S" in *"stopped=1 "*) ok "a refusal: stopped" ;; *) bad "a refusal: stopped" "$S" ;; esac

S=$( { say "" "Putting Windows back." ; printf 'aurstage-progress 80\n'; } | st restore)
case "$S" in "mode=1 "*"pct=80 "*"stopped=0"*) ok "a restore: its own mode, its own bar" ;;
    *) bad "a restore: its own mode, its own bar" "$S" ;; esac

S=$( { say "" "── putting Windows back ──" \
        "WARNING: there is no saved copy of this computer's Windows startup" \
        "         Nothing has been changed." \
        "aurstage-report v1 verdict=restore-nothing-saved record=none -" "" \
        "Turn this computer off with the power button, then on again." \
        "If you have an AurOS memory stick, plug it in first."; } | st restore)
case "$S" in *"stopped=1 "*"para=Turn this computer off with the power button"*"why=There is no saved copy"*)
    case "$S" in *"para=There is no saved copy"*) bad "a restore with nothing saved: what to do on her page, why on the helper's" "$S" ;;
        *) ok "a restore with nothing saved: what to do on her page, why on the helper's" ;; esac ;;
    *) bad "a restore with nothing saved: what to do on her page, why on the helper's" "$S" ;; esac
S=$( { say "WARNING: no readable saved copy of this computer's Windows startup" \
        "aurstage-report v1 verdict=restore-no-usable-copy record=none -"; } | st restore)
case "$S" in *"stopped=1 "*) ok "a restore with no usable copy: stopped" ;;
    *) bad "a restore with no usable copy: stopped" "$S" ;; esac
S=$( { say "restoring /dev/vda" "" "aurstage-report v1 verdict=restored record=done -" "" \
        "Windows is back. This computer will switch itself off;"; } | st restore)
case "$S" in *"stopped=0 "*"finished=1 "*"para=Windows is back."*) ok "a restore that worked: finished, not stopped" ;;
    *) bad "a restore that worked: finished, not stopped" "$S" ;; esac

S=$( say "AurOS staging environment" "this is the one restart; nothing has been changed yet" \
         "5 drivers loaded" "this machine has 3 disks" | st install)
case "$S" in *"para=Getting ready. Nothing on this computer has been changed yet."*)
    case "$S" in *"drivers loaded"*) bad "start-up: her sentence, not the technician's lines" "$S" ;;
        *) ok "start-up: her sentence, not the technician's lines" ;; esac ;;
    *) bad "start-up: her sentence, not the technician's lines" "$S" ;; esac
S=$( say "AurOS staging environment" "5 drivers loaded" "── putting Windows back ──" \
         "checking the saved copy on /dev/vdb" "restoring /dev/vda" | st restore)
case "$S" in *"step=2 "*"para=Putting Windows and its drive back exactly as they were."*) ok "a restore under way: its third step, says what it is doing" ;;
    *) bad "a restore under way: its third step, says what it is doing" "$S" ;; esac
S=$( say "AurOS staging environment" "checking the saved copy on /dev/vdb" | st restore)
case "$S" in *"step=1 "*) ok "a restore checking its copy: its second step" ;;
    *) bad "a restore checking its copy: its second step" "$S" ;; esac
S=$( { head -c 20000 /dev/urandom; printf '\naurstage-progress 999\naurstage-progress -5\n'; } | st install)
case "$S" in *"pct=0 "*) ok "garbage and out-of-range numbers: no crash, clamped" ;;
    *) bad "garbage and out-of-range numbers: no crash, clamped" "$S" ;; esac

echo
echo "  what she reads"
start() {
    say "AurOS staging environment" "this is the one restart; nothing has been changed yet" \
        "measuring how small the Windows drive can get" \
        "Windows is 118.2 GiB; it will be made 78.0 GiB, freeing 40.2 GiB for AurOS" \
        "reading every sector of the space that would be reclaimed"
}
S=$(start | st install)
case "$S" in *"para=Getting ready. Nothing on this computer has been changed yet."*)
    case "$S" in *GiB*|*reading*) bad "a sentence with a size never reaches her; the one before stays" "$S" ;;
        *) ok "a sentence with a size never reaches her; the one before stays" ;; esac ;;
    *) bad "a sentence with a size never reaches her; the one before stays" "$S" ;; esac
S=$(part | st install)
case "$S" in *"para=Copying AurOS onto this computer."*) ok "the latest sentence, not the one before it" ;;
    *) bad "the latest sentence, not the one before it" "$S" ;; esac
S=$( { part; say "" "Writing the new layout." "Keeping a copy of the way back on this computer too."; } | st install)
case "$S" in *"step=6 "*"para=Keeping a copy of the way back on this computer too."*)
    case "$S" in *"para=Writing"*) bad "a new sentence on the next line replaces the last" "$S" ;;
        *) ok "a new sentence on the next line replaces the last" ;; esac ;;
    *) bad "a new sentence on the next line replaces the last" "$S" ;; esac
S=$( { say "" "Making room on the Windows drive. This is the only part" \
        "that cannot be undone. Do not turn the computer off."; } | st install)
case "$S" in *"para=Making room on the Windows drive. This is the only part that cannot be undone. Do not turn the computer off."*)
    ok "one sentence over two console lines is one paragraph" ;;
    *) bad "one sentence over two console lines is one paragraph" "$S" ;; esac

failed() {
    part
    say "WARNING: the copy of AurOS could not be read back" \
        "aurstage-report v1 verdict=copy-failed record=failed -" "" \
        "Windows itself has not been damaged and will still start." "" \
        "Turn this computer off and on again. Windows will start" "as usual." "" \
        "To try again, open AurOS Setup in Windows and press" "Install AurOS."
}
S=$(failed | st install)
case "$S" in *"para=Windows itself"*"para=Turn this computer off and on again. Windows will start as usual."*"para=To try again, open AurOS Setup in Windows and press Install AurOS."*"why=The copy of AurOS could not be read back"*)
    case "$S" in *"para=The copy"*|*"para=Copying"*) bad "a failure: what happens next on her page, why on the helper's" "$S" ;;
        *) ok "a failure: what happens next on her page, why on the helper's" ;; esac ;;
    *) bad "a failure: what happens next on her page, why on the helper's" "$S" ;; esac

echo
echo "  the helper code"
case "$(part | st install)" in *"code=-"*) ok "none while nothing has failed" ;;
    *) bad "none while nothing has failed" "$(part | st install)" ;; esac
C=$("$B" --code copy-failed)
case "$S" in *"code=$C"*) ok "a failure shows the code of its verdict ($C)" ;; *) bad "a failure shows the code of its verdict" "$S" ;; esac
if printf '%s\n' "$C" | grep -Eq '^[2-9]{3} [2-9]{3} [2-9]{3}$'; then ok "nine digits, 2 to 9, in groups of three"
else bad "nine digits, 2 to 9, in groups of three" "$C"; fi
# Every verdict-shaped word aurstage says (more than the verdicts: that
# only makes it stricter) must get a number no other one gets.
grep -ho '"[a-z][a-z0-9]*\(-[a-z0-9]\+\)\+"' src/aurstage/*.c | tr -d '"' | sort -u > "$T/verdicts"
nv=$(wc -l < "$T/verdicts")
nc=$(while read -r v; do "$B" --code "$v"; done < "$T/verdicts" | sort -u | wc -l)
[ "$nv" -ge 40 ] && [ "$nc" = "$nv" ] && ok "$nv verdicts aurstage can say, $nc different codes" \
    || bad "every verdict aurstage can say has its own code" "$nv verdicts, $nc codes"

echo
echo "  About N minutes left"
at() { printf 'aurscreen-at %s\n' "$1"; }
S=$( { part; at 400; printf 'aurstage-progress 80\n'; } | st install)
# The copy was at 30% at 0 s and is at 80% at 400 s: 8 s a percent, 160 s
# to go, and a minute for each of the three short steps after it: 6.
case "$S" in *"left=6 "*) ok "copying, measured: its rest plus the short steps (6)" ;;
    *) bad "copying, measured: its rest plus the short steps (6)" "$S" ;; esac
S=$( { part; at 5; printf 'aurstage-progress 50\n'; } | st install)
case "$S" in *"left=-1 "*) ok "copying, not measured yet: no number" ;; *) bad "copying, not measured yet: no number" "$S" ;; esac
S=$( { start; at 0; printf 'aurstage-progress 10\n'; at 300; printf 'aurstage-progress 90\n'; } | st install)
case "$S" in *"step=0 "*"left=-1 "*) ok "before the copy: no number, the longest step is still to come" ;;
    *) bad "before the copy: no number, the longest step is still to come" "$S" ;; esac
S=$( { part; say "" "Making this computer able to start AurOS."; } | st install)
case "$S" in *"step=4 "*"left=3 "*) ok "after the copy: a minute for this step and each after it (3)" ;;
    *) bad "after the copy: a minute for this step and each after it (3)" "$S" ;; esac

echo
echo "  Show details for a helper"
sz="--size 1024 600"
on="aurscreen-press 200 552"; off="aurscreen-press 700 300"
S=$( { failed; at 10; echo "$on"; } | "$B" --mode install --state $sz)
case "$S" in *"details=1 "*) ok "pressing it shows the helper's page" ;; *) bad "pressing it shows the helper's page" "$S" ;; esac
S=$( { failed; at 10; echo "$on"; at 12; echo "$on"; } | "$B" --mode install --state $sz)
case "$S" in *"details=0 "*) ok "Hide details is where she pressed" ;; *) bad "Hide details is where she pressed" "$S" ;; esac
S=$( { failed; at 10; echo "$on"; at 10.3; echo "$on"; } | "$B" --mode install --state $sz)
case "$S" in *"details=1 "*) ok "a double-click is one press" ;; *) bad "a double-click is one press" "$S" ;; esac
S=$( { at 10; failed; at 10.4; echo "$on"; } | "$B" --mode install --state $sz)
case "$S" in *"details=0 "*) ok "a press just as the page changed is not taken" ;;
    *) bad "a press just as the page changed is not taken" "$S" ;; esac
S=$( { at 10; failed; at 10.4; echo "$on"; at 10.8; echo "$on"; } | "$B" --mode install --state $sz)
case "$S" in *"details=0 "*) ok "...nor the second half of that double-click" ;;
    *) bad "...nor the second half of that double-click" "$S" ;; esac
S=$( { failed; at 10; echo "$off"; } | "$B" --mode install --state $sz)
case "$S" in *"details=0 "*) ok "a press beside it does nothing" ;; *) bad "a press beside it does nothing" "$S" ;; esac
S=$( { failed; at 10; echo "aurscreen-key"; } | "$B" --mode install --state $sz)
case "$S" in *"details=1 "*) ok "Enter presses it" ;; *) bad "Enter presses it" "$S" ;; esac
S=$( { part; at 10; echo "$on"; echo "aurscreen-key"; } | "$B" --mode install --state $sz)
case "$S" in *"details=0 "*) ok "while installing there is nothing to press" ;;
    *) bad "while installing there is nothing to press" "$S" ;; esac

echo
echo "  pictures"
for sz in "1024 768" "1366 768" "1920 1080" "800 600" "1024 600"; do
    set -- $sz
    if part | "$B" --mode install --png "$T/s.png" --size "$1" "$2" 2>/dev/null && \
       [ "$(head -c 8 "$T/s.png" | od -An -c | tr -d ' ')" = '211PNG\r\n032\n' ]; then
        ok "paints at $1x$2"
    else bad "paints at $1x$2"; fi
done
if command -v python3 >/dev/null; then
    px=$(python3 -c '
import sys, struct, zlib
d = open(sys.argv[1], "rb").read(); i = 8; z = b""
while i < len(d):
    n = struct.unpack(">I", d[i:i+4])[0]
    if d[i+4:i+8] == b"IDAT": z += d[i+8:i+8+n]
    i += 12 + n
print(zlib.decompress(z)[1:4].hex())' "$T/s.png")
    [ "$px" = f6f2e9 ] && ok "Light: the page is paper" || bad "Light: the page is paper" "corner is #$px"
fi
small=$(grep -o 'face_at([0-9][0-9]*' src/aurscreen/screen.c | tr -dc '0-9\n' | sort -n | head -1)
[ "${small:-0}" -ge 18 ] && ok "no text below 18 px (smallest $small)" || bad "no text below 18 px" "smallest is $small"
# The bold face: ui-bold.ttf, when it is there, is what titles use.
mkdir -p "$T/bold" "$T/none"
DJ=$(ls /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf 2>/dev/null)
if [ -n "$DJ" ]; then
    ln -s "$DJ" "$T/bold/ui-bold.ttf"
    failed | "$B" --fonts "$T/bold" --mode install --png "$T/b.png" --size 1024 600
    failed | "$B" --fonts "$T/none" --mode install --png "$T/n.png" --size 1024 600
    cmp -s "$T/b.png" "$T/n.png" && bad "titles use ui-bold.ttf when it is there" "same picture with and without it" \
        || ok "titles use ui-bold.ttf when it is there"
fi

if [ -n "$LOG" ]; then
    echo
    echo "  a real install's serial log: $LOG"
    S=$(tr -d '\r' < "$LOG" | grep -a '^aurstage' | st install)
    case "$S" in *"step=6 "*"handover=1 "*) ok "followed it to the end: every step, handed over" ;;
        *) bad "followed it to the end: every step, handed over" "$S" ;; esac
fi

echo
echo "screentest: $((checked - fail))/$checked"
[ "$fail" = 0 ]
