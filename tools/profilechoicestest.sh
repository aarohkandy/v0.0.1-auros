#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  profilechoicestest — what an installer built for a profile writes for
#  first boot, applied by the REAL image of that profile
#
#  tools/choicesboottest.sh does this for desktop with a hand-written
#  choices.conf. This takes the choices.conf the installer for PROFILE
#  really writes (the simulation, built for that profile, after the person
#  picks the "Like a Mac" card on the look page), puts it where AurBridge
#  puts it, starts out/auros-PROFILE.img under firmware in QEMU, and after
#  first boot reads the image's root and asks:
#
#    - a look page was offered:  is the desktop her card (dock) and the
#      theme the PROFILE's own, not the wizard's old fixed Sandstone?
#    - none was offered (a kiosk): did first boot leave the image's own
#      look alone -- the profile's shell and theme, locked desktop intact?
#
#    sudo sh tools/profilechoicestest.sh PROFILE      (needs out/auros-PROFILE.img
#          and out/aurbridge-sim built for PROFILE: AUROS_IMAGE_PROFILE=PROFILE build/aurbridge)
#    OLD=1 ...   writes what the installer wrote BEFORE profiles existed
#                (shell=dock theme=sandstone whatever the image) to show the difference
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

P="${1:?usage: profilechoicestest.sh PROFILE}"
IMG="out/auros-$P.img"
SIM=out/aurbridge-sim
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

[ -f "$IMG" ] || { echo "no $IMG; build it first"; exit 2; }
[ -x "$SIM" ] || { echo "no $SIM; run build/aurbridge"; exit 2; }
for t in qemu-system-x86_64 sgdisk mkfs.vfat mcopy mmd losetup debugfs; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ "$($SIM profile | sed -n 's/^profile=\([^ ]*\) .*/\1/p')" = "$P" ] \
    || { echo "$SIM was not built for $P ($($SIM profile))"; exit 2; }
TMP=$(mktemp -d "${TMPDIR:-/tmp}/profilechoices.XXXXXX")
trap 'mountpoint -q "$TMP/m" && umount "$TMP/m"; [ -n "${L:-}" ] && losetup -d "$L" 2>/dev/null; rm -rf "$TMP"' EXIT

echo
echo "Does the real $P image start up the way its installer says?"
echo

# ── the choices.conf the installer for $P writes ────────────────────
# A trivial disk and image: the engine only needs somewhere to write.
D="$TMP/disk.img"; truncate -s 3G "$D"
sgdisk -n 1:2048:206847 -t 1:ef00 -n 2:206848:2303999 -t 2:0700 -n 3:5500000:6291422 -t 3:2700 "$D" >/dev/null
printf 'NTFS    ' | dd of="$D" bs=1 seek=$((206848*512+3)) conv=notrunc status=none
mkdir -p "$TMP/win/AurOS" "$TMP/sim/esp"; : > "$TMP/sim/efivars.txt"
printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D")" > "$TMP/sim/disks.txt"
I="$TMP/win/AurOS/auros-$P.img"; truncate -s 64M "$I"
sgdisk -n 1:2048:+16M -t 1:ef00 -n 2:0:0 -t 2:8304 "$I" >/dev/null
head -c 4096 /dev/urandom > "$TMP/k"; head -c 4096 /dev/urandom > "$TMP/i"
if [ "${OLD:-0}" = 1 ]; then
    AURBRIDGE_SHELL=dock AURBRIDGE_THEME=sandstone \
        "$SIM" "$TMP/sim" "$P" none "$I" "$TMP/k" "$TMP/i" >/dev/null 2>"$TMP/err"
else
    AURBRIDGE_SHELL=dock AURBRIDGE_THEME=sandstone \
        "$SIM" "$TMP/sim" @baked none "$I" "$TMP/k" "$TMP/i" >/dev/null 2>"$TMP/err"
fi
CH="$TMP/sim/esp/EFI/AurOS/choices.conf"
[ -f "$CH" ] || { echo "the engine wrote no choices.conf"; tail -3 "$TMP/err"; exit 1; }
echo "  choices.conf the installer writes${OLD:+ (the OLD fixed behaviour)}:"; grep -v '^#' "$CH" | sed 's/^/      /'

# ── boot the real image with it waiting on the EFI partition ────────
cp --sparse=always "$IMG" "$TMP/disk2.img"
E="$TMP/esp.img"; truncate -s 64M "$E"
sgdisk --zap-all "$E" >/dev/null 2>&1
sgdisk -n 1:2048:0 -t 1:ef00 -c 1:"EFI" "$E" >/dev/null 2>&1
truncate -s 62M "$TMP/fat.img"; mkfs.vfat -F32 -n EFI "$TMP/fat.img" >/dev/null 2>&1
export MTOOLS_SKIP_CHECK=1
mmd -i "$TMP/fat.img" ::/EFI ::/EFI/AurOS
mcopy -i "$TMP/fat.img" "$CH" ::/EFI/AurOS/choices.conf
dd if="$TMP/fat.img" of="$E" bs=1M seek=1 conv=notrunc status=none
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$TMP/vars.fd"
: > "$TMP/out.txt"
qemu-system-x86_64 -machine q35,accel=tcg -m 3072 -smp 2 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,unit=1,file="$TMP/vars.fd" \
    -drive file="$TMP/disk2.img",format=raw,if=none,id=d0 \
    -device virtio-blk-pci,drive=d0,bootindex=0 \
    -drive file="$E",format=raw,if=none,id=d1 -device virtio-blk-pci,drive=d1 \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    -display none -serial stdio > "$TMP/out.txt" 2>&1 &
qp=$!
R=$(python3 - "$TMP/disk2.img" <<'EOPY'
import sys, struct
f = open(sys.argv[1], 'rb'); f.seek(512); h = f.read(512)
pl = struct.unpack_from('<Q', h, 72)[0]; n = struct.unpack_from('<I', h, 80)[0]
e = struct.unpack_from('<I', h, 84)[0]; f.seek(pl * 512); arr = f.read(n * e)
for i in range(n):
    ent = arr[i*e:(i+1)*e]
    if ent[56:128].decode('utf-16-le', 'replace').rstrip('\0') == 'AUROS-ROOT':
        first, last = struct.unpack_from('<QQ', ent, 32); print(first, last); break
EOPY
)
RS=${R% *}; RE=${R#* }
done_at=""; i=0
while [ $i -lt 1500 ]; do
    kill -0 $qp 2>/dev/null || break
    if [ $((i % 15)) -eq 0 ] && [ $i -gt 0 ]; then
        L=$(losetup -r --find --show -o $((RS*512)) --sizelimit $(((RE-RS+1)*512)) "$TMP/disk2.img" 2>/dev/null)
        if [ -n "$L" ]; then
            debugfs -R 'stat /var/lib/auros/firstboot.done' "$L" 2>/dev/null | grep -q Inode && done_at=$i
            losetup -d "$L"; L=""
        fi
        [ -n "$done_at" ] && break
    fi
    sleep 1; i=$((i+1))
done
sleep 20
kill -9 $qp 2>/dev/null; wait $qp 2>/dev/null
[ -n "$done_at" ] && ok "first boot finished (after about ${done_at}s)" \
                  || bad "first boot finished" "$(tail -5 "$TMP/out.txt")"

L=$(losetup -r --find --show -o $((RS*512)) --sizelimit $(((RE-RS+1)*512)) "$TMP/disk2.img")
mkdir -p "$TMP/m"
mount -o ro,noload "$L" "$TMP/m" 2>/dev/null || { bad "the image's root can be read afterwards"; exit 1; }
M="$TMP/m"
THEME=$(cat "$M/etc/auros/theme" 2>/dev/null)
SHELLN=$(readlink "$M/etc/auros/shell/active.shell" | sed 's|.*/||; s|\.shell$||')
POLICY_SHELL=$(sed -n 's/^shell_archetype *= *//p' "$M/etc/auros/policy.conf")
POLICY_KIOSK=$(sed -n 's/^kiosk_mode *= *//p' "$M/etc/auros/policy.conf")
echo "  the installed system says: theme=$THEME desktop=$SHELLN (image's own: $POLICY_SHELL, kiosk=$POLICY_KIOSK)"
grep -a 'choices:' "$M/var/log/auros-firstboot.log" 2>/dev/null | sed 's/^/      first boot: /'
if grep -q '^shell=' "$CH"; then
    [ "$SHELLN" = "$(sed -n 's/^shell=//p' "$CH")" ] && ok "the desktop is the one her card chose ($SHELLN)" \
        || bad "the desktop is the one her card chose" "got $SHELLN"
    [ "$THEME" = "$(sed -n 's/^theme=//p' "$CH")" ] && ok "the theme is the one the installer wrote ($THEME)" \
        || bad "the theme is the one the installer wrote" "got $THEME"
else
    [ "$SHELLN" = "$POLICY_SHELL" ] && ok "the installer chose no look: the image keeps its own desktop ($SHELLN)" \
        || bad "the image keeps its own desktop" "got $SHELLN, the image says $POLICY_SHELL"
    PTHEME=$(python3 -c 'import sys; from pathlib import Path; sys.path.insert(0, "."); from tools.customize.installer_profile import resolve; print(resolve(sys.argv[1], Path("profiles")).get("theme", ""))' "$P")
    [ "$THEME" = "$PTHEME" ] && ok "...and its own theme ($THEME)" || bad "...and its own theme" "got $THEME, the profile says $PTHEME"
fi
if [ "$POLICY_KIOSK" = yes ]; then
    [ "$SHELLN" = locked ] && ok "a kiosk is still on the locked desktop after the installer's choices" \
        || bad "a kiosk is still on the locked desktop after the installer's choices" "it is on $SHELLN"
fi
echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: the real $P image starts up as its installer was built to leave it."
