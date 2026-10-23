#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  windowsboottest — does "Windows" in AurOS's start-up menu start
#  Windows the way BitLocker expects?
#
#  BitLocker seals its key to what the TPM measured when Windows Boot
#  Manager was started by the firmware: PCR 7 (Secure Boot state and
#  the certificates that let each program start) and PCR 4 (the
#  programs). The menu used to chainload bootmgfw.efi through shim and
#  grub, which puts shim, grub and Microsoft's third-party CA into both
#  -- and a BitLocker PC then asks for its 48-digit recovery key. Now
#  the menu starts a few seconds of AurOS (start-windows) that sets
#  BootNext to Windows Boot Manager and restarts, so the firmware starts
#  it itself.
#
#  This boots the REAL image under OVMF with Microsoft's keys, Secure
#  Boot on and a software TPM (swtpm). "Windows" is a stand-in that can
#  be measured: Windows' EFI partition holds a Microsoft-signed program
#  at \EFI\Microsoft\Boot\bootmgfw.efi (Canonical's shim -- the only
#  Microsoft-signed program a test may carry) which starts a kernel
#  whose whole job is to print PCR 4, PCR 7 and BootCurrent. Then:
#
#    A  a normal morning: the firmware starts Windows Boot Manager.
#    B  "Windows" chosen in AurOS's menu: start-windows, BootNext,
#       restart, the firmware starts it. PCR 4 and 7 must equal A's,
#       BootCurrent must be Windows Boot Manager, BootOrder unchanged.
#    C  CONTROL: the old menu line, chainloading it. PCR 4 and 7 must
#       DIFFER from A's -- otherwise this test cannot tell the two.
#    D  no Windows Boot Manager entry, a BitLocker volume on the disk:
#       nothing is chainloaded; it says so and restarts.
#    E  no Windows Boot Manager entry, no BitLocker volume: chainloaded
#       once, the old way.
#
#  NOT REAL: the stand-in Windows; the menu choice, made by appending
#  `set default=...` to a scratch copy's grub.cfg; the BitLocker volume,
#  which is only its signature (-FVE-FS-), the thing start-windows looks
#  for. Not proven: a real Windows' own PCR profile, real firmware.
#
#    sudo sh tools/windowsboottest.sh [profile]     (default: desktop)
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

PROFILE="${1:-desktop}"
E=tools/efivarstore.py
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
BOOT_TIMEOUT="${WINDOWSBOOT_TIMEOUT:-600}"

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-64s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-64s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

for t in qemu-system-x86_64 swtpm sgdisk python3 losetup mount mkfs.vfat mmd mcopy \
         gcc cpio gzip grub-editenv; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys"; exit 2; }
[ -f "out/auros-$PROFILE.img" ] || { echo "no out/auros-$PROFILE.img"; exit 2; }

LOCK="${TMPDIR:-/tmp}/installtest.lock"
mkdir "$LOCK" 2>/dev/null || { echo "another end-to-end test is running"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/windowsboot.XXXXXX")
QP=""; TP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    [ -n "$TP" ] && kill -9 "$TP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    mountpoint -q "$T/n" 2>/dev/null && umount "$T/n"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    rm -rf "$T" "$LOCK"
}
trap cleanup EXIT

echo
echo "Does \"Windows\" in AurOS's menu start Windows the way BitLocker expects?"
echo

part_of() { sgdisk -p "$1" 2>/dev/null | awk -v n="$2" '$NF==n {print $1, $2, $3}'; }
mount_root() {
    set -- $(part_of "$DISK" AUROS-ROOT)
    [ -n "${2:-}" ] || { echo "  no AUROS-ROOT"; return 1; }
    LP=$(losetup --find --show -o $(( $2 * 512 )) \
                 --sizelimit $(( ($3 - $2 + 1) * 512 )) "$DISK") || return 1
    mkdir -p "$T/m"; mount "$LP" "$T/m" || { losetup -d "$LP"; LP=""; return 1; }
}
umount_root() { sync; umount "$T/m" 2>/dev/null; losetup -d "$LP" 2>/dev/null; LP=""; }

# ── AurOS, confirmed, and the pieces of the stand-in ────────────────
DISK="$T/auros.img"
cp --sparse=always "out/auros-$PROFILE.img" "$DISK"
set -- $(part_of "$DISK" AUROS-ESP); ESPN=$1; ESPOFF=$(( $2 * 512 ))
export MTOOLS_SKIP_CHECK=1
mcopy -n -i "$DISK@@$ESPOFF" ::/EFI/AurOS/shimx64.efi "$T/shim.efi" &&
mcopy -n -i "$DISK@@$ESPOFF" ::/EFI/AurOS/grubx64.efi "$T/grub.efi" \
    || { echo "  no shim/grub on the image's EFI partition"; exit 2; }
mount_root || exit 2
mkdir -p "$T/m/var/lib/auros"
printf 'confirmed by windowsboottest\n' > "$T/m/var/lib/auros/converted.confirmed"
cp "$(ls "$T"/m/boot/vmlinuz-*-generic | sort -V | tail -1)" "$T/vmlinuz"
[ -x "$T/m/usr/lib/auros/start-windows" ] && ok "the image carries start-windows" \
    || bad "the image carries start-windows"
grep -A2 -- "--id windows {" "$T/m/boot/grub/grub.cfg" | grep -q "init=/usr/lib/auros/start-windows" \
  && ok "...and its menu's Windows line starts it, not a chainload" \
  || bad "...and its menu's Windows line starts it, not a chainload" \
         "$(grep -A4 -- "--id windows" "$T/m/boot/grub/grub.cfg" | head -6)"
cp "$T/m/boot/grub/grub.cfg" "$T/grub.cfg.orig"
umount_root

# The stand-in's own program: print what the TPM measured, and which
# entry the firmware started, then switch off.
cat > "$T/pcr.c" <<'EOC'
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <unistd.h>
static void put(FILE *c, const char *k, const char *p)
{
    char b[160] = {0};
    int fd = open(p, O_RDONLY);
    if (fd >= 0) { ssize_t n = read(fd, b, sizeof b - 1); if (n > 0) b[n] = 0; close(fd); }
    b[strcspn(b, "\n")] = 0;
    fprintf(c, "STANDIN %s=%s\n", k, b[0] ? b : "none");
}
int main(void)
{
    mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
    mount("sysfs", "/sys", "sysfs", 0, 0);
    mount("efivarfs", "/sys/firmware/efi/efivars", "efivarfs", 0, 0);
    FILE *c = fopen("/dev/console", "w");
    if (!c) c = stdout;
    for (int i = 0; i < 50 && access("/sys/class/tpm/tpm0/pcr-sha256/7", R_OK); i++)
        usleep(200000);
    put(c, "pcr4", "/sys/class/tpm/tpm0/pcr-sha256/4");
    put(c, "pcr7", "/sys/class/tpm/tpm0/pcr-sha256/7");
    unsigned char v[8] = {0};
    int fd = open("/sys/firmware/efi/efivars/BootCurrent-8be4df61-93ca-11d2-aa0d-00e098032b8c", O_RDONLY);
    if (fd >= 0 && read(fd, v, 6) == 6) fprintf(c, "STANDIN bootcurrent=%02X%02X\n", v[5], v[4]);
    fflush(c);
    sync();
    reboot(RB_POWER_OFF);
    return 0;
}
EOC
mkdir -p "$T/ir/dev" "$T/ir/sys" "$T/ir/proc"
gcc -static -O1 -o "$T/ir/init" "$T/pcr.c" 2>/dev/null || { echo "  could not build the stand-in"; exit 2; }
( cd "$T/ir" && find . | cpio -o -H newc --quiet | gzip ) > "$T/pcr.img"

cat > "$T/standin.cfg" <<'EOG'
set timeout=0
serial --unit=0 --speed=115200
terminal_output console serial
menuentry 'stand-in Windows' {
    search --no-floppy --file --set=root /EFI/Microsoft/Boot/pcr.img
    linux /EFI/Microsoft/Boot/vmlinuz rdinit=/init console=ttyS0,115200n8 quiet
    initrd /EFI/Microsoft/Boot/pcr.img
}
EOG

# "Windows"' disk: its EFI partition, and its system drive -- empty, or
# carrying only a BitLocker volume's signature.
make_windows() { # plain|bitlocker (in $WINKIND)
    WIN="$T/windows.img"; rm -f "$WIN"
    truncate -s 512M "$WIN"
    sgdisk -n1:2048:+200M -t1:EF00 -c1:"EFI system partition" \
           -n2:0:+128M -t2:0700 -c2:"Basic data partition" "$WIN" >/dev/null
    dd if=/dev/zero of="$T/wesp.fat" bs=1M count=200 2>/dev/null
    mkfs.vfat -F 32 "$T/wesp.fat" >/dev/null
    mmd -i "$T/wesp.fat" ::/EFI ::/EFI/Microsoft ::/EFI/Microsoft/Boot
    mcopy -i "$T/wesp.fat" "$T/shim.efi"    ::/EFI/Microsoft/Boot/bootmgfw.efi
    mcopy -i "$T/wesp.fat" "$T/grub.efi"    ::/EFI/Microsoft/Boot/grubx64.efi
    mcopy -i "$T/wesp.fat" "$T/standin.cfg" ::/EFI/Microsoft/Boot/grub.cfg
    mcopy -i "$T/wesp.fat" "$T/vmlinuz"     ::/EFI/Microsoft/Boot/vmlinuz
    mcopy -i "$T/wesp.fat" "$T/pcr.img"     ::/EFI/Microsoft/Boot/pcr.img
    dd if="$T/wesp.fat" of="$WIN" bs=512 seek=2048 conv=notrunc 2>/dev/null
    rm -f "$T/wesp.fat"
    set -- $(sgdisk -p "$WIN" | awk '$1==2 {print $2, $3}')
    truncate -s $(( ($2 - $1 + 1) * 512 )) "$T/c.img"
    # Not BitLocker: whatever else is there (zeros here). BitLocker's
    # mark is the signature -FVE-FS- at byte 3 of the volume.
    if [ "$WINKIND" = bitlocker ]; then
        printf '\353\130\220-FVE-FS-' | dd of="$T/c.img" conv=notrunc 2>/dev/null
    fi
    dd if="$T/c.img" of="$WIN" bs=512 seek="$1" conv=notrunc 2>/dev/null
    rm -f "$T/c.img"
}

# Firmware: AurOS first (she said it works), and Windows Boot Manager
# pointing at the stand-in -- or no Windows Boot Manager at all.
make_nv() { # with-windows|no-windows
    NV="$T/vars.fd"; cp "$VARS" "$NV"
    python3 "$E" "$NV" plant-hd Boot0002 "AurOS" '\EFI\AurOS\shimx64.efi' "$DISK" "$ESPN" || return 1
    if [ "$1" = with-windows ]; then
        python3 "$E" "$NV" plant-hd Boot0000 "Windows Boot Manager" \
            '\EFI\Microsoft\Boot\bootmgfw.efi' "$WIN" 1 || return 1
        python3 "$E" "$NV" set-order 0002 0000
    else
        python3 "$E" "$NV" set-order 0002
    fi
}

menu_default() { # entry id, or "" for the menu as built
    # The choice is made at the TOP of the menu, so anything the menu
    # itself decides later (the one-shot chainload, boot counting) still
    # has the last word, as it would for a person pressing a key.
    mount_root || return 1
    cp "$T/grub.cfg.orig" "$T/m/boot/grub/grub.cfg"
    if [ "$1" = windows-old ]; then
        # --unrestricted: the menu is locked since c506219 (superusers), and an
        # entry without it asks for a password nobody has -- which is why this
        # control sat at the menu in run 39975129610 (21/23, only case C red).
        cat >> "$T/m/boot/grub/grub.cfg" <<'EOG'
menuentry 'Windows (the old way)' --unrestricted --id windows-old {
    insmod part_gpt
    insmod fat
    insmod chain
    search --no-floppy --file --set=root /EFI/Microsoft/Boot/bootmgfw.efi
    chainloader /EFI/Microsoft/Boot/bootmgfw.efi
}
EOG
    fi
    [ -n "$1" ] && sed -i "s/^set default=0\$/set default=$1/" "$T/m/boot/grub/grub.cfg"
    grep -q "^set default=${1:-0}\$" "$T/m/boot/grub/grub.cfg" || { echo "  could not set the menu's default"; umount_root; return 1; }
    umount_root
}
RUN=0
power_on() { # [-no-reboot]
    RUN=$((RUN + 1))
    SER="$T/serial.$RUN"; : > "$SER"
    rm -rf "$T/tpm"; mkdir -p "$T/tpm"
    swtpm socket --tpm2 --tpmstate dir="$T/tpm" --ctrl type=unixio,path="$T/swtpm.sock" \
        --flags not-need-init >/dev/null 2>&1 &
    TP=$!
    i=0; while [ ! -S "$T/swtpm.sock" ] && [ "$i" -lt 50 ]; do sleep 0.1; i=$((i+1)); done
    qemu-system-x86_64 -machine q35,smm=on,accel=tcg -m 2048 -smp 2 ${1:-} \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$NV" \
        -chardev socket,id=chrtpm,path="$T/swtpm.sock" \
        -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis,tpmdev=tpm0 \
        -drive file="$DISK",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -drive file="$WIN",format=raw,if=none,id=d1 -device virtio-blk-pci,drive=d1 \
        -device virtio-vga -display none -serial file:"$SER" -net none \
        >/dev/null 2>&1 &
    QP=$!
    i=0
    while kill -0 "$QP" 2>/dev/null && [ "$i" -lt "$BOOT_TIMEOUT" ]; do sleep 1; i=$((i+1)); done
    kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
    kill -9 "$TP" 2>/dev/null; wait "$TP" 2>/dev/null; TP=""
    rm -f "$T/swtpm.sock"
    tr -d '\r' < "$SER" > "$SER.txt"
}
val() { sed -n "s/^.*STANDIN $1=\\([0-9A-Fa-f]*\\).*/\\1/p" "$SER.txt" | tail -1; }

# ── A: a normal morning ─────────────────────────────────────────────
WINKIND=plain; make_windows
make_nv with-windows && python3 "$E" "$NV" set-order 0000 0002
menu_default ""
echo "  A: a normal morning -- the firmware starts Windows Boot Manager"
power_on
A4=$(val pcr4); A7=$(val pcr7)
[ -n "$A7" ] && [ "$A7" != 0000000000000000000000000000000000000000000000000000000000000000 ] \
  && ok "the stand-in Windows started and read the TPM" \
  || bad "the stand-in Windows started and read the TPM" "$(tail -5 "$SER.txt")"
[ "$(val bootcurrent)" = 0000 ] && ok "...started by the firmware, as Boot0000" \
  || bad "...started by the firmware, as Boot0000" "BootCurrent=$(val bootcurrent)"

# ── B: Windows, from AurOS's menu ───────────────────────────────────
echo
echo "  B: \"Windows\" chosen in AurOS's start-up menu"
make_nv with-windows
menu_default windows
power_on
grep -aq 'starting Boot0002 "AurOS"' "$SER.txt" && grep -aq 'Starting Windows' "$SER.txt" \
  && ok "AurOS's menu started start-windows" \
  || bad "AurOS's menu started start-windows" "$(grep -a 'BdsDxe\|start-windows\|Kernel panic' "$SER.txt" | head -5)"
grep -aq 'aurfirst: the next start is Windows (entry 0000)' "$SER.txt" \
  && ok "...which armed BootNext at Windows Boot Manager" \
  || bad "...which armed BootNext at Windows Boot Manager" "$(grep -a 'aurfirst\|start-windows' "$SER.txt" | head -3)"
awk '/Starting Windows/{s=1} s && /BdsDxe: (loading|starting) Boot0000/{f=1} END{exit !f}' "$SER.txt" \
  && ok "...and after the restart the firmware itself started it" \
  || bad "...and after the restart the firmware itself started it" "$(grep -a 'BdsDxe' "$SER.txt" | tail -3)"
[ "$(val bootcurrent)" = 0000 ] && ok "...as Boot0000 (BootCurrent, read in \"Windows\")" \
  || bad "...as Boot0000 (BootCurrent, read in \"Windows\")" "BootCurrent=$(val bootcurrent)"
B4=$(val pcr4); B7=$(val pcr7)
[ -n "$B7" ] && [ "$B7" = "$A7" ] && ok "PCR 7 is what a normal morning measures" \
  || bad "PCR 7 is what a normal morning measures" "A=$A7" "B=${B7:-none}"
[ -n "$B4" ] && [ "$B4" = "$A4" ] && ok "PCR 4 is what a normal morning measures" \
  || bad "PCR 4 is what a normal morning measures" "A=$A4" "B=${B4:-none}"
case "$(python3 "$E" "$NV" bootorder)" in "0002 0000"|"0002 0000 "*) ok "BootOrder untouched: AurOS still first" ;;
    *) bad "BootOrder untouched: AurOS still first" "$(python3 "$E" "$NV" bootorder)" ;; esac
[ -z "$(python3 "$E" "$NV" bootnext)" ] && ok "...and the one-shot was used up" \
  || bad "...and the one-shot was used up" "BootNext=$(python3 "$E" "$NV" bootnext)"

# ── C: the old way, as a control ────────────────────────────────────
echo
echo "  C: control -- the old menu line, chainloading bootmgfw.efi"
make_nv with-windows
menu_default windows-old
power_on
C4=$(val pcr4); C7=$(val pcr7)
[ -n "$C7" ] && ok "the chainloaded stand-in started and read the TPM" \
  || bad "the chainloaded stand-in started and read the TPM" "$(tail -5 "$SER.txt")"
[ -n "$C7" ] && [ "$C7" != "$A7" ] && [ "$C4" != "$A4" ] \
  && ok "...and PCR 4 and 7 differ: what BitLocker would refuse" \
  || bad "...and PCR 4 and 7 differ: what BitLocker would refuse" "A7=$A7 C7=$C7" "A4=$A4 C4=$C4"

# ── D: no entry to start, and BitLocker ─────────────────────────────
echo
echo "  D: no Windows Boot Manager entry, a BitLocker volume on the disk"
WINKIND=bitlocker; make_windows
make_nv no-windows
menu_default windows
power_on -no-reboot
grep -aq 'BitLocker=yes' "$SER.txt" && grep -aq 'Windows could not be started from this menu' "$SER.txt" \
  && ok "it found the BitLocker volume and said it could not" \
  || bad "it found the BitLocker volume and said it could not" "$(grep -a 'start-windows\|aurfirst\|Windows could' "$SER.txt" | head -4)"
mount_root; GL=$(grub-editenv "$T/m/boot/grub/grubenv" list 2>/dev/null); umount_root
case "$GL" in *auros_win=chain*) bad "...and asked grub for no chainload" "$GL" ;;
    *) ok "...and asked grub for no chainload" ;; esac
grep -aq 'STANDIN' "$SER.txt" && bad "...and nothing started Windows" || ok "...and nothing started Windows"

# ── E: no entry to start, no BitLocker ──────────────────────────────
echo
echo "  E: no Windows Boot Manager entry, no BitLocker volume"
WINKIND=plain; make_windows
make_nv no-windows
menu_default windows
power_on
grep -aq 'BitLocker=no' "$SER.txt" \
  && ok "it found no BitLocker volume" \
  || bad "it found no BitLocker volume" "$(grep -a 'start-windows\|aurfirst' "$SER.txt" | head -4)"
grep -aq 'STANDIN' "$SER.txt" && ok "...and after the restart grub chainloaded Windows, once" \
  || bad "...and after the restart grub chainloaded Windows, once" "$(grep -a 'BdsDxe\|error' "$SER.txt" | tail -4)"
mount_root; GL=$(grub-editenv "$T/m/boot/grub/grubenv" list 2>/dev/null); umount_root
case "$GL" in *auros_win=chain*) bad "...and the request was used up" "$GL" ;;
    *) ok "...and the request was used up" ;; esac

# ── F: AurOS's own partition is gone ────────────────────────────────
echo
echo "  F: AUROS-ROOT deleted in Disk Management; AurOS's boot entry is still there"
# The ESP's grub.cfg as build/mkimage writes it (the very lines, run
# here), for a root UUID that no partition has any more.
WINKIND=plain; make_windows
make_nv with-windows && python3 "$E" "$NV" set-order 0002 0000
mkdir -p "$T/e/EFI/ubuntu" "$T/e/EFI/AurOS" "$T/e/EFI/BOOT"
( ESP="$T/e"; ROOT_UUID=00000000-dead-beef-0000-000000000000
  eval "$(sed -n '/^for d in ubuntu AurOS BOOT/,/^done/p' build/mkimage)" )
for d in ubuntu AurOS BOOT; do
    mcopy -o -i "$DISK@@$ESPOFF" "$T/e/EFI/$d/grub.cfg" "::/EFI/$d/grub.cfg" || bad "could not plant the ESP's grub.cfg in $d"
done
menu_default ""
power_on
grep -aq 'starting Boot0002 "AurOS"' "$SER.txt" && grep -aq 'AurOS is not on this computer any more' "$SER.txt" \
  && ok "AurOS's entry started, found no AurOS, and said so in words" \
  || bad "AurOS's entry started, found no AurOS, and said so in words" "$(grep -a 'BdsDxe\|AurOS\|grub>' "$SER.txt" | head -4)"
grep -aq 'grub>' "$SER.txt" && bad "...without dropping to a grub> prompt" || ok "...without dropping to a grub> prompt"
[ "$(val bootcurrent)" = 0000 ] \
  && ok "...and the firmware went on to the next entry, Windows" \
  || bad "...and the firmware went on to the next entry, Windows" "BootCurrent=$(val bootcurrent)" "$(tail -3 "$SER.txt")"

echo
echo "windowsboottest: $((checked - fail))/$checked"
[ "$fail" = 0 ]
