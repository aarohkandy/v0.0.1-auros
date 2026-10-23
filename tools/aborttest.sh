#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  aborttest — closing the installer takes back everything it did
#
#  Before the restart, everything AurBridge does can be undone, and
#  closing the window is supposed to undo it. It did not all come back
#  (docs/ATLAS.md, gap 22): Fast Startup and hibernation stayed off, the
#  "AurOS Installer" Boot#### and the files under \EFI\AurOS stayed,
#  and gigabytes stayed in \AurOS\ on the Windows drive. This arms a
#  simulated machine with the real phase engine (out/aurbridge-sim, no
#  stick), then aborts it the way closing the window does, and checks
#  each of those is gone and nothing else was touched.
#
#  No root, no KVM: the machine is files (src/aurbridge/plat_sim.c).
#    sh tools/aborttest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-60s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-60s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

[ -x out/aurbridge-sim ] || { echo "run ./build/aurbridge first"; exit 2; }
command -v sgdisk >/dev/null || { echo "need sgdisk"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/aborttest.XXXXXX")
# A kill (timeout, ^C) must not leak the images in $TMP: the EXIT trap alone
# does not run on a signal in dash, and a leaked 3 GB disk image per run
# once filled homebase. INT and TERM exit, which runs the EXIT trap, and
# the background QEMU goes with it.
_a2_clean() { kill $(jobs -p) 2>/dev/null; rm -rf "$T"; }
trap _a2_clean EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo
echo "Does closing the installer take back everything it did?"
echo

# A disk shaped like a PC's: ESP, a Windows partition whose first sector
# says NTFS (all the simulated preflight reads), WinRE at the end.
D="$T/disk.img"
truncate -s 3G "$D"
sgdisk -n 1:2048:206847 -t 1:ef00 -n 2:206848:2303999 -t 2:0700 \
       -n 3:5500000:6291422 -t 3:2700 "$D" >/dev/null
printf 'NTFS    ' | dd of="$D" bs=1 seek=$((206848*512+3)) conv=notrunc status=none
# The image: its own GPT with an ESP and a root, which is what the
# engine reads to write the manifest. Contents do not matter here.
I="$T/win/AurOS/auros-desktop.img"
mkdir -p "$T/win/AurOS"
truncate -s 64M "$I"
sgdisk -n 1:2048:+16M -t 1:ef00 -n 2:0:0 -t 2:8304 "$I" >/dev/null
head -c 4096 /dev/urandom > "$T/kern"; head -c 4096 /dev/urandom > "$T/initrd"
S="$T/sim"; mkdir -p "$S/esp"
: > "$S/efivars.txt"
printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D" 2>/dev/null || stat -f%z "$D")" \
    > "$S/disks.txt"
DISKSUM=$(cksum < "$D")

AURBRIDGE_ABORT=1 AURBRIDGE_FORGET=1 out/aurbridge-sim "$S" desktop none "$I" \
    "$T/kern" "$T/initrd" > "$T/out" 2> "$T/err"
grep -q 'verdict=armed' "$T/out" && ok "the engine arms the simulated machine" \
    || { bad "the engine arms the simulated machine" "$(tail -3 "$T/err")"; exit 1; }
grep -q 'verdict=aborted' "$T/out" && ok "...and closing the window aborts it" \
    || bad "...and closing the window aborts it" "$(tail -3 "$T/err")"

# The firmware: the entry it made is gone, and BootNext with it. In
# efivars.txt the last line for a name wins and an empty one is a delete.
last() { grep "^$1=" "$S/efivars.txt" | tail -1 | cut -d= -f2-; }
made=$(grep -o '^Boot[0-9A-F]\{4\}=AurOS Installer' "$S/efivars.txt" | head -1 | cut -d= -f1)
[ -n "$made" ] || bad "it had made an AurOS Installer entry" "$(cat "$S/efivars.txt")"
[ -n "$made" ] && [ -z "$(last "$made")" ] \
    && ok "the AurOS Installer start-up entry is gone" \
    || bad "the AurOS Installer start-up entry is gone" "$made=$(last "$made")"
[ -z "$(last BootNext)" ] && ok "BootNext is cleared" || bad "BootNext is cleared" "$(last BootNext)"
# The EFI partition: nothing of ours left.
left=$(find "$S/esp/EFI/AurOS" -type f 2>/dev/null)
[ -z "$left" ] && ok "nothing is left under \\EFI\\AurOS" \
               || bad "nothing is left under \\EFI\\AurOS" "$left"
# Fast Startup and hibernation, as they were.
[ -f "$S/fast-startup-restored" ] && [ ! -f "$S/fast-startup-off" ] \
    && ok "Fast Startup is put back as it was" \
    || bad "Fast Startup is put back as it was" "$(ls "$S")"
# The download on the Windows drive.
[ ! -e "$I" ] && [ ! -e "$I.manifest" ] && [ ! -e "$I.part" ] \
    && ok "the copy of AurOS on the Windows drive is gone" \
    || bad "the copy of AurOS on the Windows drive is gone" "$(ls "$T/win/AurOS")"
# And the machine's own disk was never written.
[ "$(cksum < "$D")" = "$DISKSUM" ] && ok "the machine's disk was not touched" \
                                   || bad "the machine's disk was not touched"

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: closing the installer leaves the machine as it was."
