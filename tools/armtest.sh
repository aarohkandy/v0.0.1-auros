#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  armtest -- "Restart later" starts nothing
#
#  The wizard used to arm the next start (BootNext, and the drive lock
#  paused) before it asked her whether to restart. "Restart later" then
#  closed the window with the install armed for the next time the
#  computer started, by itself, at some unsupervised moment
#  (docs/research/people-walkthrough-2026-10.md, C-08). Now phase 3 stops
#  short (ab_choice.arm_later) and the two things that make the next
#  start the installer wait for ab_arm(), which the wizard calls when she
#  presses Restart now; ab_disarm() takes just those two back.
#
#  This runs the real engine on a simulated machine (out/aurbridge-sim,
#  no stick) and reads the firmware's variables between the steps.
#    sh tools/armtest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-64s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-64s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

[ -x out/aurbridge-sim ] || { echo "run ./build/aurbridge first"; exit 2; }
command -v sgdisk >/dev/null || { echo "need sgdisk"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/armtest.XXXXXX")
trap 'rm -rf "$T"' EXIT

echo
echo "Does the install wait for her press?"
echo

D="$T/disk.img"
truncate -s 3G "$D"
sgdisk -n 1:2048:206847 -t 1:ef00 -n 2:206848:2303999 -t 2:0700 \
       -n 3:5500000:6291422 -t 3:2700 "$D" >/dev/null
printf 'NTFS    ' | dd of="$D" bs=1 seek=$((206848*512+3)) conv=notrunc status=none
I="$T/win/AurOS/auros-desktop.img"
mkdir -p "$T/win/AurOS"
truncate -s 64M "$I"
sgdisk -n 1:2048:+16M -t 1:ef00 -n 2:0:0 -t 2:8304 "$I" >/dev/null
head -c 4096 /dev/urandom > "$T/kern"; head -c 4096 /dev/urandom > "$T/initrd"
S="$T/sim"; mkdir -p "$S/esp"
: > "$S/efivars.txt"
printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D" 2>/dev/null || stat -f%z "$D")" \
    > "$S/disks.txt"

last() { grep "^$1=" "$S/efivars.txt" | tail -1 | cut -d= -f2-; }

# 1. The way the wizard runs it: prepared, armed on her press, disarmed.
AURBRIDGE_ARM_LATER=1 AURBRIDGE_DISARM=1 out/aurbridge-sim "$S" desktop none "$I" \
    "$T/kern" "$T/initrd" > "$T/out" 2> "$T/err"
grep -q 'step=prepared bootnext=0' "$T/out" \
    && ok "when the engine is done, nothing is armed (Restart later is safe)" \
    || bad "when the engine is done, nothing is armed (Restart later is safe)" "$(cat "$T/out")" "$(tail -3 "$T/err")"
grep -q 'step=armed rc=0 bootnext=1' "$T/out" \
    && ok "ab_arm (her press on Restart now) arms it" \
    || bad "ab_arm (her press on Restart now) arms it" "$(cat "$T/out")"
grep -q 'step=disarmed bootnext=0 entry=1' "$T/out" \
    && ok "ab_disarm takes back only the arming: the entry stays for another press" \
    || bad "ab_disarm takes back only the arming: the entry stays for another press" "$(cat "$T/out")"
[ -z "$(last BootNext)" ] && ok "after disarming, BootNext is clear in the firmware" \
                          || bad "after disarming, BootNext is clear in the firmware" "BootNext=$(last BootNext)"
made=$(grep -o '^Boot[0-9A-F]\{4\}=AurOS Installer' "$S/efivars.txt" | head -1 | cut -d= -f1)
[ -n "$made" ] && [ -n "$(last "$made")" ] \
    && ok "...and the AurOS Installer entry is still there" \
    || bad "...and the AurOS Installer entry is still there" "$made=$(last "$made")"

# 2. Until she presses, the firmware has no BootNext at all.
rm -rf "$S/esp"; mkdir -p "$S/esp"; : > "$S/efivars.txt"
rm -f "$I.manifest" "$I.part"
AURBRIDGE_ARM_LATER=1 out/aurbridge-sim "$S" desktop none "$I" "$T/kern" "$T/initrd" > "$T/out2" 2> "$T/err2"
# the sim's variables were written by ab_arm at the end of that run: look
# at what the run said BEFORE it, which is the state she is in while the
# window waits
first=$(grep -n 'step=prepared' "$T/out2" | head -1 | cut -d: -f1)
[ -n "$first" ] && sed -n "${first}p" "$T/out2" | grep -q 'bootnext=0' \
    && ok "the state while the window waits has no BootNext" \
    || bad "the state while the window waits has no BootNext" "$(cat "$T/out2")"

# 3. Every other caller still arms at the end of phase 3, as before.
rm -rf "$S/esp"; mkdir -p "$S/esp"; : > "$S/efivars.txt"
out/aurbridge-sim "$S" desktop none "$I" "$T/kern" "$T/initrd" > "$T/out3" 2> "$T/err3"
grep -q 'verdict=armed' "$T/out3" && [ -n "$(last BootNext)" ] \
    && ok "without arm_later (tests, the console tool) it arms at the end, as before" \
    || bad "without arm_later (tests, the console tool) it arms at the end, as before" "$(tail -3 "$T/err3")"

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: nothing starts the install until she presses Restart now."
