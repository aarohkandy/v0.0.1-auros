#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  termstest -- the Terms she agreed to are written down
#
#  The Ready page says "By pressing Install AurOS you agree to the AurOS
#  Terms" (stream LEGAL, docs/handoff/streams/LEGAL.md item 2). The
#  wizard hands the engine the version ("v3") and the time in
#  ab_choice.terms_version / terms_unix; phase 3 writes them into the
#  install journal on the EFI partition. They stay on this computer.
#
#  This runs the real engine on a simulated machine (out/aurbridge-sim,
#  no stick), pulls journal.json out of the staging image it wrote, and
#  asks: is `"terms":"v3"` there when she agreed, and absent when nobody
#  pressed anything (the console tool, the tests)?
#    sh tools/termstest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-64s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-64s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

[ -x out/aurbridge-sim ] || { echo "run ./build/aurbridge first"; exit 2; }
command -v sgdisk >/dev/null || { echo "need sgdisk"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/termstest.XXXXXX")
trap 'rm -rf "$T"' EXIT

echo
echo "Is what she agreed to written down?"
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

# the journal is the last gzip segment of the staging image
journal() {
    tail -c 16384 "$1" | python3 -c '
import sys, zlib
d = sys.stdin.buffer.read()
i = d.rfind(b"\x1f\x8b\x08")
out = zlib.decompress(d[i:], 16 + zlib.MAX_WBITS)
j = out.find(b"{")
sys.stdout.write(out[j:out.rfind(b"}") + 1].decode("ascii", "replace"))'
}

run() { # $1 = the Terms version to hand over, or empty
    S="$T/sim$2"; mkdir -p "$S/esp"; : > "$S/efivars.txt"
    printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D" 2>/dev/null || stat -f%z "$D")" > "$S/disks.txt"
    rm -f "$I.manifest" "$I.part"
    if [ -n "$1" ]; then AURBRIDGE_TERMS="$1" out/aurbridge-sim "$S" desktop none "$I" "$T/kern" "$T/initrd" > "$T/out$2" 2> "$T/err$2"
    else out/aurbridge-sim "$S" desktop none "$I" "$T/kern" "$T/initrd" > "$T/out$2" 2> "$T/err$2"; fi
    grep -q 'verdict=armed' "$T/out$2" || return 1
    journal "$S/esp/EFI/AurOS/staging.img" > "$T/journal$2.json" 2>/dev/null
}

if run v3 1; then
    if grep -q '"terms":"v3","terms_unix":1760000000' "$T/journal1.json"; then
        ok "she pressed Install AurOS: the journal says terms v3, and when"
    else
        bad "she pressed Install AurOS: the journal says terms v3, and when" "$(cat "$T/journal1.json" | head -c 600)"
    fi
else
    bad "she pressed Install AurOS: the journal says terms v3, and when" "the engine did not arm" "$(tail -3 "$T/err1")"
fi
if run "" 2; then
    if [ -s "$T/journal2.json" ] && ! grep -q 'terms' "$T/journal2.json"; then
        ok "nobody pressed anything (the console tool): no terms in the journal"
    else
        bad "nobody pressed anything (the console tool): no terms in the journal" "$(cat "$T/journal2.json" | head -c 600)"
    fi
else
    bad "nobody pressed anything (the console tool): no terms in the journal" "the engine did not arm" "$(tail -3 "$T/err2")"
fi

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: the Terms she agreed to are written down, and only then."
