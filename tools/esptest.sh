#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  esptest — a computer with two EFI partitions is refused, not guessed
#
#  AurBridge copies its start-up files to ONE EFI partition (the one
#  Windows mounts) and names a partition in the firmware's boot entry.
#  Those were taken by different rules: the entry named the LAST EFI
#  partition in the table. On a computer with two, the entry pointed at
#  a partition with nothing on it, BootNext was consumed, and the
#  install never started (docs/safety, finding on phases.c:239).
#
#  The real phase engine against a simulated machine, no root, no KVM.
#    sh tools/esptest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-62s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-62s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

[ -x out/aurbridge-sim ] || { echo "run ./build/aurbridge first"; exit 2; }
command -v sgdisk >/dev/null || { echo "need sgdisk"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/esptest.XXXXXX")
_clean() { kill $(jobs -p) 2>/dev/null; rm -rf "$T"; }
trap _clean EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo
echo "Does a computer with two EFI partitions get refused?"
echo

# $1 = disk; builds one-ESP (a) or two-ESP (b) machine and runs the engine.
run() { # name  n-esp
    D="$T/$1.img"; rm -f "$D"; truncate -s 3G "$D"
    sgdisk -n 1:2048:206847 -t 1:ef00 -n 2:206848:2303999 -t 2:0700 \
           -n 3:5500000:6291422 -t 3:2700 "$D" >/dev/null
    if [ "$2" = 2 ]; then
        # A second EFI partition (another operating system's) after Windows.
        sgdisk -n 4:2400000:2500000 -t 4:ef00 "$D" >/dev/null
    fi
    printf 'NTFS    ' | dd of="$D" bs=1 seek=$((206848*512+3)) conv=notrunc status=none
    mkdir -p "$T/$1/win/AurOS" "$T/$1/sim/esp"
    I="$T/$1/win/AurOS/auros-desktop.img"
    truncate -s 64M "$I"; sgdisk -n 1:2048:+16M -t 1:ef00 -n 2:0:0 -t 2:8304 "$I" >/dev/null
    head -c 4096 /dev/urandom > "$T/kern"; head -c 4096 /dev/urandom > "$T/initrd"
    : > "$T/$1/sim/efivars.txt"
    printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D" 2>/dev/null || stat -f%z "$D")" \
        > "$T/$1/sim/disks.txt"
    out/aurbridge-sim "$T/$1/sim" desktop none "$I" "$T/kern" "$T/initrd" \
        > "$T/$1/out" 2> "$T/$1/err"
}

run one 1
grep -q 'verdict=armed' "$T/one/out" && ok "one EFI partition: the engine arms it" \
    || bad "one EFI partition: the engine arms it" "$(tail -3 "$T/one/err")"
# The entry names the partition the files went to: partition 1.
grep -q 'AurOS Installer|.*|HD(1,GPT,2048,' "$T/one/sim/efivars.txt" \
    && ok "...and the boot entry names that partition (1)" \
    || bad "...and the boot entry names that partition (1)" "$(grep -a Boot "$T/one/sim/efivars.txt" | head -2)"

run two 2
grep -q 'verdict=refused' "$T/two/out" && ok "two EFI partitions: refused" \
    || bad "two EFI partitions: refused" "$(cat "$T/two/out")"
grep -q 'two start-up partitions' "$T/two/out" && ok "...in words that name the reason" \
    || bad "...in words that name the reason" "$(cat "$T/two/out")"
[ -z "$(find "$T/two/sim/esp" -type f 2>/dev/null)" ] && ok "...before one file was written to the EFI partition" \
    || bad "...before one file was written to the EFI partition" "$(find "$T/two/sim/esp" -type f)"
[ ! -s "$T/two/sim/efivars.txt" ] && ok "...and no boot entry was made" \
    || bad "...and no boot entry was made" "$(cat "$T/two/sim/efivars.txt")"

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: two EFI partitions are refused before anything is written."
