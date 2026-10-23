#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  theftvm — the encrypted drive and its PIN on the REAL AurOS image,
#  in a KVM VM with Secure Boot (OVMF, Microsoft's keys) and a TPM 2.0
#  (swtpm). docs/security/THEFT.md, "How it is proven".
#
#  The PIN is typed the way she types it: key presses into the VM's
#  keyboard (QEMU sendkey), read by the start page from the kernel.
#  Pictures of the page are taken from the VM's screen.
#
#    1  plain AurOS: `lockctl prepare` (not something the image does by
#       itself: theft protection is switched off in the default install)
#    2  the start encrypts the partition in place; the VM's power is
#       pulled half-way through the pass; the next start resumes it;
#       her letter is intact; the raw disk no longer holds a byte of it;
#       speed of the pass, of the cipher, of reads; set-pin
#    3  the PIN page; a wrong PIN ("didn't work", counted by the chip);
#       the right PIN starts AurOS; how long the chip took
#    4  a dbx update in the firmware (PCR 7 changes): the PIN still
#       starts AurOS -- no rescue code (the key is bound to no PCR)
#    5  a different security chip: "this computer has changed"; the
#       rescue code; she chooses her PIN; AurOS starts and seals it
#    6  the new PIN starts AurOS, in the new chip
#
#    sudo bash tools/theftvm.sh [--noaes]     (--noaes: a CPU without AES
#                                             instructions -> Adiantum)
#
#  Needs out/auros-desktop.img, KVM, qemu-system-x86, ovmf, swtpm, gcc,
#  python3, gdisk, mtools, and the network (the image is given the
#  packages it would need: cryptsetup-bin and the TPM libraries).
#  Evidence: $THEFT_EVIDENCE (default out/theft-evidence): every serial
#  log, every screen as PNG, summary.txt.
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
TOP=$PWD
export PYTHONDONTWRITEBYTECODE=1    # as root in a container this would leave root-owned files in the checkout
NOAES=0; [ "${1:-}" = --noaes ] && NOAES=1
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
IMG=${THEFT_IMAGE:-out/auros-desktop.img}
EV=${THEFT_EVIDENCE:-out/theft-evidence}
TIMEOUT=${THEFT_TIMEOUT:-900}
PIN1=1234 PIN2=2580
# 7: the key is sealed to PCR 7 (the design). "none": to no PCR.
PCRS=${THEFT_PCRS:-7}; [ "$PCRS" = none ] && PCRS=''
CUR=$PIN1

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s %s\n' "$1" "ok" | tee -a "$EV/summary.txt"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s %s\n' "$1" "FAIL" | tee -a "$EV/summary.txt"
        shift; for m in "$@"; do printf '      %s\n' "$m" | tee -a "$EV/summary.txt"; done; }
note() { printf '    %s\n' "$*" | tee -a "$EV/summary.txt"; }

[ "$(id -u)" = 0 ] || { echo "theftvm needs root"; exit 2; }
for t in qemu-system-x86_64 swtpm gcc python3 sgdisk losetup mcopy; do
    command -v "$t" >/dev/null || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys"; exit 2; }
[ -f "$IMG" ] || { echo "no $IMG"; exit 2; }
[ -w /dev/kvm ] || { echo "no KVM"; exit 2; }
mkdir -p "$EV"; : > "$EV/summary.txt"

# TMPDIR on a tmpfs (docker --tmpfs) keeps the 5 GB working disk off a shared disk
T=$(mktemp -d "${TMPDIR:-/tmp}/theftvm.XXXXXX")
QP=""; SWP=""; LP=""
cleanup() {
    set +e
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    [ -n "$SWP" ] && kill "$SWP" 2>/dev/null
    for m in dev/pts dev sys proc ""; do mountpoint -q "$T/m/$m" && umount -l "$T/m/$m"; done
    [ -n "$LP" ] && losetup -d "$LP"
    rm -rf "$T"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# ── the disk ──────────────────────────────────────────────────────
DISK="$T/disk.img"
cp --sparse=always "$IMG" "$DISK"
part_of() { sgdisk -p "$DISK" 2>/dev/null | awk -v n="$1" '$NF==n {print $1, $2, $3}'; }
set -- $(part_of AUROS-ROOT); RSTART=$2; REND=$3
set -- $(part_of AUROS-ESP); ESPOFF=$(( $2 * 512 ))
[ -n "${RSTART:-}" ] || { echo "no AUROS-ROOT in $IMG"; exit 2; }
mount_root() {
    LP=$(losetup --find --show -o $(( RSTART * 512 )) --sizelimit $(( (REND - RSTART + 1) * 512 )) "$DISK") || return 1
    mkdir -p "$T/m"; mount "$LP" "$T/m"
}
umount_root() { sync; umount "$T/m"; losetup -d "$LP"; LP=""; }
export MTOOLS_SKIP_CHECK=1
esp_ls() { mdir -b -i "$DISK@@$ESPOFF" "::$1" 2>/dev/null; }

echo
echo "AurOS's encrypted drive and PIN, in a VM with Secure Boot and a TPM 2.0$([ $NOAES = 1 ] && echo ', on a CPU without AES instructions'), key sealed to ${PCRS:+PCR $PCRS}${PCRS:-no PCR}" | tee -a "$EV/summary.txt"
echo

# ── what the image is given (what the forge would install once this is switched on)
gcc -O2 -std=gnu11 -Wall -Wextra -Werror -o "$T/aurlock-pin" src/aurshell/lockpin.c \
    src/aurshell/kms.c src/aurshell/draw.c src/common/font.c src/common/png.c src/common/theme.c -lm -ldl \
    || { echo "aurlock-pin did not build"; exit 1; }
mount_root || { echo "cannot mount AUROS-ROOT"; exit 2; }
R="$T/m"
for m in proc sys dev dev/pts; do mount --bind "/$m" "$R/$m"; done
cp /etc/resolv.conf "$R/etc/resolv.conf.theft" 2>/dev/null
mv "$R/etc/resolv.conf" "$R/etc/resolv.conf.orig" 2>/dev/null; cp /etc/resolv.conf "$R/etc/resolv.conf"
chroot "$R" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin DEBIAN_FRONTEND=noninteractive sh -c '
    timeout 300 apt-get update -q >/dev/null 2>&1 || timeout 300 apt-get update -q >/dev/null 2>&1
    timeout 600 apt-get install -y -q --no-install-recommends cryptsetup-bin \
        libtss2-esys-3.0.2-0t64 libtss2-tctildr0t64 libtss2-tcti-device0t64 libtss2-rc0t64 libtss2-mu-4.0.1-0t64' \
    > "$EV/apt.txt" 2>&1
pkgs=$(chroot "$R" dpkg-query -W -f '${Package} ${Version}\n' cryptsetup-bin libtss2-esys-3.0.2-0t64 2>/dev/null)
echo "$pkgs" | grep -q cryptsetup-bin && ok "the image is given cryptsetup-bin and the TPM libraries" \
    || { bad "the image is given cryptsetup-bin and the TPM libraries" "$(tail -3 "$EV/apt.txt")"; exit 1; }
mv -f "$R/etc/resolv.conf.orig" "$R/etc/resolv.conf" 2>/dev/null; rm -f "$R/etc/resolv.conf.theft"
for m in dev/pts dev sys proc; do umount "$R/$m"; done
mkdir -p "$R/usr/lib/auros/lock"
cp -a rootfs/usr/lib/auros/lock/. "$R/usr/lib/auros/lock/"
install -m755 "$T/aurlock-pin" "$R/usr/lib/auros/lock/aurlock-pin"

# Her letter. The words are made here, so nothing on the disk but the
# letter contains them, and the raw disk can be searched for them later.
MARK="HER-LETTER-$(od -An -tx1 -N6 /dev/urandom | tr -d ' \n')"
mkdir -p "$R/var/lib/auros-theft"
for i in $(seq 1 300); do echo "$MARK line $i: the bank statement, the photos, the Wi-Fi password"; done \
    > "$R/var/lib/auros-theft/letter.txt"
LSUM=$(sha256sum "$R/var/lib/auros-theft/letter.txt" | cut -c1-64)

# The stand-in for her: a service that reports on the serial line at
# every start and says what to do next. NOT PART OF AUROS.
cat > "$R/usr/lib/auros/lock/theft-e2e" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. tools/theftvm.sh puts this into a scratch copy of the image.
L=/usr/lib/auros/lock/lockctl
say() { echo "THEFT $*" > /dev/ttyS0; }
st=$($L status 2>&1 | tr '\n' ' ')
say "up $st"
say "letter $(sha256sum /var/lib/auros-theft/letter.txt | cut -c1-64)"
say "analyze $(systemd-analyze 2>/dev/null | head -1)"
say "cmdline $(cat /proc/cmdline)"
say "look $(grep -oE '(^|[[:space:]])(bg|err)="[^"]*"' /etc/auros/shell.conf 2>/dev/null | tr -d ' \t' | tr '\n' ' ')"
say "cryptenroll $(ls -l /usr/bin/systemd-cryptenroll 2>&1)"
grep -q 'aurlock-pin\|auros-lock' /proc/cmdline 2>/dev/null
journalctl -b -o cat 2>/dev/null | grep -E 'aurlock-pin|auros-lock|lockctl' | while read -r l; do say "log $l"; done
case "$st" in
*state=plain*)
    AUROS_LOCK_PCRS="@PCRS@" $L prepare 2>&1 | while read -r l; do say "prepare $l"; done
    say "esp $(ls /boot/efi/auros 2>&1 | tr '\n' ' ')"
    say "stub $(tr '\n' ' ' < /boot/efi/EFI/AurOS/grub.cfg)"
    ;;
*state=encrypting*|*state=open-no-pin*)
    say "pass waiting"
    t0=$(date +%s)
    while $L status | grep -q state=encrypting; do
        sleep 2
        [ $(( $(date +%s) - t0 )) -gt 2400 ] && { say "pass TIMEOUT"; break; }
    done
    say "pass done in $(( $(date +%s) - t0 )) s (this start)"
    say "letter-after-pass $(sha256sum /var/lib/auros-theft/letter.txt | cut -c1-64)"
    dev=$($L status | sed -n 's/^device=//p')
    say "bench-xts $(cryptsetup benchmark -c aes-xts-plain64 -s 512 2>/dev/null | tail -1)"
    say "bench-adiantum $(cryptsetup benchmark -c xchacha12,aes-adiantum-plain64 -s 256 2>/dev/null | tail -1)"
    echo 3 > /proc/sys/vm/drop_caches
    say "read-encrypted $(dd if=/dev/mapper/auros-root of=/dev/null bs=4M count=256 iflag=direct 2>&1 | tail -1)"
    say "read-raw $(dd if="$dev" of=/dev/null bs=4M count=256 skip=8 iflag=direct 2>&1 | tail -1)"
    say "write-encrypted $(dd if=/dev/zero of=/var/tmp/w bs=4M count=128 oflag=direct 2>&1 | tail -1)"
    rm -f /var/tmp/w
    code=$(printf '%s\n' "@PIN1@" | $L set-pin 2>/tmp/setpin.err)
    say "setpin-exit $? $(cat /tmp/setpin.err)"
    say "RESCUE $code"
    say "after-setpin $($L status | tr '\n' ' ')"
    ;;
*state=pin*)
    say "tpm $(/usr/lib/auros/lock/aurlock-pin --tpm-status | tr '\n' ' ')"
    say "repin $(journalctl -b -u auros-lock-repin -o cat 2>/dev/null | tr '\n' ' ')"
    ;;
esac
say "poweroff"
sync
systemctl poweroff
EOS
sed -i "s/@PIN1@/$PIN1/; s/@PCRS@/$PCRS/" "$R/usr/lib/auros/lock/theft-e2e"
chmod 755 "$R/usr/lib/auros/lock/theft-e2e"
cat > "$R/etc/systemd/system/auros-theft-e2e.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: tools/theftvm.sh's stand-in for her
After=multi-user.target
[Service]
Type=oneshot
ExecStart=/usr/lib/auros/lock/theft-e2e
StandardOutput=journal
TimeoutStartSec=3000
[Install]
WantedBy=graphical.target
EOU
mkdir -p "$R/etc/systemd/system/graphical.target.wants"
ln -sf /etc/systemd/system/auros-theft-e2e.service "$R/etc/systemd/system/graphical.target.wants/"
umount_root
grep -qa "$MARK" "$DISK" && ok "the plain disk holds her letter in the clear (the check below can see it)" \
    || bad "the plain disk holds her letter in the clear"

# ── the machine ───────────────────────────────────────────────────
cp "$VARS" "$T/vars.fd"
CPU="host"; [ "$NOAES" = 1 ] && CPU="host,-aes"
TPMD="$T/tpm-a"; mkdir -p "$TPMD"
N=0
power_on() { # NAME
    N=$((N + 1)); SER="$EV/serial-$N-$1.txt"; : > "$SER"
    swtpm socket --tpm2 --tpmstate dir="$TPMD" --ctrl type=unixio,path="$TPMD/sock" \
        --pid file="$TPMD/pid" --daemon || return 1
    SWP=$(cat "$TPMD/pid")
    qemu-system-x86_64 -machine q35,smm=on,accel=kvm -cpu "$CPU" -m 2048 -smp 2 -no-reboot \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$T/vars.fd" \
        -chardev socket,id=chrtpm,path="$TPMD/sock" -tpmdev emulator,id=tpm0,chardev=chrtpm \
        -device tpm-crb,tpmdev=tpm0 \
        -drive file="$DISK",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -device virtio-vga -display none -serial file:"$SER" -net none \
        -monitor unix:"$T/mon.sock",server,nowait >/dev/null 2>&1 &
    QP=$!
    T0=$(date +%s)
}
wait_for() { # PATTERN [SECONDS] -> 0 when the serial log has it
    local lim=${2:-$TIMEOUT} i=0
    while [ "$i" -lt "$lim" ]; do
        grep -aq -- "$1" "$SER" && return 0
        kill -0 "$QP" 2>/dev/null || { grep -aq -- "$1" "$SER"; return; }
        sleep 1; i=$((i + 1))
    done
    return 1
}
power_off() { # wait for the VM to switch itself off, or pull the plug
    local i=0
    while kill -0 "$QP" 2>/dev/null && [ "$i" -lt "${1:-120}" ]; do sleep 1; i=$((i + 1)); done
    kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
    kill "$SWP" 2>/dev/null; sleep 0.5; SWP=""
}
mon() { python3 -c 'import socket,sys,time
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.1); s.recv(4096)
s.sendall((sys.argv[2]+"\n").encode()); time.sleep(0.2); s.close()' "$T/mon.sock" "$1"; }
keys() { # DIGITS-AND-WORDS: "1234" or "ret"
    local w=$1 i
    if [ "$w" = ret ]; then mon "sendkey ret"; sleep 0.3; return; fi
    for i in $(echo "$w" | fold -w1); do mon "sendkey $i"; sleep 0.25; done
}
# A picture of the screen, as PNG, and how much of it is her page's
# background and its warning colour.
shot() { # NAME -> sets SHOT="bg% err-pixels"
    rm -f "$T/s.ppm"; mon "screendump $T/s.ppm"; sleep 1
    SHOT=$(python3 - "$T/s.ppm" "$EV/screen-$N-$1.png" "${BG:-0B0E14}" "${ERR:-F2788D}" <<'EOP'
import sys, zlib, struct
src, out, bg, err = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
import re
d = open(src, 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+\d+\s', d)      # not split(): pixel bytes can be "whitespace"
w, h = int(m.group(1)), int(m.group(2)); px = d[m.end():m.end() + w*h*3]
def rgb(x): return tuple(int(x[i:i+2], 16) for i in (0, 2, 4))
B, E = rgb(bg), rgb(err)
near = lambda p, q, t: all(abs(a - b) <= t for a, b in zip(p, q))
nb = ne = 0
for i in range(0, len(px), 3 * 7):          # every 7th pixel is plenty
    p = (px[i], px[i+1], px[i+2])
    if near(p, B, 3): nb += 1
    if near(p, E, 24): ne += 1
raw = b''.join(b'\0' + px[y*w*3:(y+1)*w*3] for y in range(h))
def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                      + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
print(round(100 * nb / max(1, len(px) // 21)), ne * 7, w, h)
EOP
)
}
serial() { tr -d '\r' < "$SER"; }
val() { serial | sed -n "s/^THEFT $1 //p" | tail -1; }

# ── 0: shim's first start on a firmware that has no entry for AurOS yet
# creates one and resets; QEMU is run with -no-reboot, so it exits. The
# entry stays in the variable store for every start after this.
power_on prime
power_off 180

# ── 1: plain AurOS, armed ─────────────────────────────────────────
echo "  1  plain AurOS: lockctl prepare" | tee -a "$EV/summary.txt"
power_on plain
wait_for "THEFT poweroff" && ok "AurOS started plain and reported" || bad "AurOS started plain" "$(serial | tail -5)"
power_off
LOOK=$(val look)
BG=$(echo "$LOOK" | sed -n 's/.*bg="#\{0,1\}\([0-9A-Fa-f]\{6\}\).*/\1/p')
ERR=$(echo "$LOOK" | sed -n 's/.*err="#\{0,1\}\([0-9A-Fa-f]\{6\}\).*/\1/p')
note "her look, read from the running system: background #${BG:-none, the page default}, warnings #${ERR:-none}"
note "$(val cryptenroll)"
serial | grep -q "THEFT prepare lockctl: armed" && ok "lockctl prepare armed it" || bad "lockctl prepare" "$(serial | grep 'THEFT prepare' | tail -4)"
esp_ls /auros | grep -q '^.*grub\.cfg' && esp_ls /auros | grep -q 'initrd.img-' \
    && ok "the kernel, initramfs and menu are on the EFI partition" || bad "boot set on the EFI partition" "$(esp_ls /auros)"

# ── 2: the start that encrypts, a power cut mid-pass, the pass, set-pin
echo "  2  encrypting in place, a power cut, the pass, the first PIN" | tee -a "$EV/summary.txt"
power_on encrypt
if wait_for "THEFT pass waiting"; then
    ok "the start encrypted in place and AurOS came up on it"
    serial | grep -q 'auros-lock: the drive is being encrypted' && note "$(serial | grep -o 'auros-lock: the drive is being encrypted.*' | head -1)"
    sleep "${THEFT_CUT_AFTER:-8}"
    if serial | grep -q "THEFT pass done"; then
        note "the pass finished before the power could be cut (fast disk); no power cut tested"
        CUT=0
    else
        kill -9 "$QP"; CUT=1
        ok "the power was pulled while the pass ran"
    fi
else
    bad "the start encrypted in place" "$(serial | tail -8)"; CUT=0
fi
power_off 5
if [ "$CUT" = 1 ]; then
    power_on resume
    wait_for "THEFT pass done" && ok "the next start resumed the pass and finished it" \
        || bad "the pass resumed after the power cut" "$(serial | tail -6)"
fi
wait_for "THEFT poweroff"
[ "$(val letter-after-pass)" = "$LSUM" ] && ok "her letter is intact" || bad "her letter is intact" "$(val letter-after-pass) != $LSUM"
serial | grep -q "THEFT up .*state=encrypting.*cipher=" || true
CODE=$(val RESCUE | tr -d ' ')
[ ${#CODE} = 24 ] && ok "set-pin gave a 24-number rescue code" || bad "set-pin gave a rescue code" "$(serial | grep -E 'setpin|RESCUE')"
val after-setpin | grep -q "state=pin" && val after-setpin | grep -q "pin=tpm" \
    && ok "the PIN is sealed in the VM's TPM" || bad "the PIN is sealed in the TPM" "$(val after-setpin)"
if [ "$NOAES" = 1 ]; then
    val after-setpin | grep -q 'cipher=xchacha12,aes-adiantum-plain64' \
        && ok "no AES instructions: the drive uses Adiantum" || bad "no AES instructions: Adiantum" "$(val after-setpin)"
else
    val after-setpin | grep -q 'cipher=aes-xts-plain64' \
        && ok "AES instructions: the drive uses AES-XTS" || bad "AES instructions: AES-XTS" "$(val after-setpin)"
fi
note "pass:      $(val 'pass done in' | head -1)"
note "aes-xts:   $(val bench-xts)"
note "adiantum:  $(val bench-adiantum)"
note "read, through the encryption: $(val read-encrypted)"
note "read, the raw disk:           $(val read-raw)"
note "write, through the encryption: $(val write-encrypted)"
power_off
n=$(grep -ac "$MARK" "$DISK")
[ "$n" = 0 ] && ok "the raw disk holds no line of her letter any more" || bad "no plaintext on the raw disk" "$n copies found"

# ── 3: the PIN page, a wrong PIN, the right PIN ───────────────────
echo "  3  the PIN page" | tee -a "$EV/summary.txt"
power_on pin
if wait_for "aurlock-pin: asking for the PIN" 300; then
    T1=$(( $(date +%s) - T0 )); sleep 2
    shot page; set -- $SHOT
    [ "${1:-0}" -ge 50 ] && ok "a full-screen AurOS page, not a console ($1% her background, $3x$4)" \
        || bad "the PIN page is on screen" "$SHOT"
    note "power on to PIN page: ${T1} s"
    keys 9999; sleep 4
    shot wrong; set -- $SHOT
    serial | grep -q 'aurlock-pin: PIN wrong' && ok "a wrong PIN: the page says it did not work" || bad "a wrong PIN is wrong" "$(serial | grep aurlock-pin)"
    [ "${2:-0}" -gt 100 ] && ok "...in her warning colour, on the page" || bad "the wrong-PIN sentence is on screen" "$SHOT"
    keys "$PIN1"
    wait_for "THEFT poweroff" && ok "the right PIN started AurOS" || bad "the right PIN started AurOS" "$(serial | tail -5)"
    note "$(serial | grep -o 'aurlock-pin: PIN open in [0-9]* ms' | head -1) (the TPM's unseal, KVM)"
    val tpm | grep -q 'counter=[1-9]' && ok "the chip counted the wrong PIN ($(val tpm | grep -o 'counter=[0-9]*'))" \
        || bad "the chip counted the wrong PIN" "$(val tpm)"
else
    bad "the PIN page appeared" "$(serial | tail -8)"
fi
power_off

# ── 4: a dbx update (PCR 7 changes) ───────────────────────────────
echo "  4  a dbx update in the firmware" | tee -a "$EV/summary.txt"
python3 -I - "$T/vars.fd" "$TOP/tools" <<'EOP'
import sys, os, uuid
sys.path.insert(0, sys.argv[2]); import efivarstore as e
path = sys.argv[1]
live = [d for n, _g, _a, d in e.read_store(path) if n == 'dbx']
old = live[-1] if live else b''
# one EFI_CERT_SHA256 list holding one made-up hash: what a dbx update
# adds, revoking nothing this machine starts
owner = uuid.UUID('77fa9abd-0359-4d32-bd60-28f4e78f784b').bytes_le
sig = owner + os.urandom(32)
lst = uuid.UUID('c1c41626-504c-4092-aca9-41f936934328').bytes_le + \
      (28 + len(sig)).to_bytes(4, 'little') + (0).to_bytes(4, 'little') + len(sig).to_bytes(4, 'little') + sig
e.supersede(path, 'dbx', old + lst)
print('dbx grew from %d to %d bytes' % (len(old), len(old) + len(lst)))
EOP
power_on dbx
if wait_for "aurlock-pin: asking for the PIN" 300; then
    sleep 2; keys "$CUR"; sleep 4
    if [ -n "$PCRS" ]; then
        shot dbx-changed
        serial | grep -q 'aurlock-pin: PIN changed' && ok "PCR 7 changed: the page says the computer changed (not \"wrong PIN\")" \
            || bad "a dbx update -> changed" "$(serial | grep aurlock-pin | tail -3)"
        keys "$CODE"; sleep 5
        serial | grep -q 'aurlock-pin: rescue code open' && ok "the rescue code opened the drive" || bad "the rescue code after a dbx update" "$(serial | grep aurlock-pin | tail -3)"
        keys "$PIN1"; keys ret; sleep 1; keys "$PIN1"; keys ret
        wait_for "THEFT poweroff" && ok "she chose her PIN again and AurOS started" || bad "AurOS started after the rescue code" "$(serial | tail -5)"
        val repin | grep -q 'sealed again' && ok "the PIN is sealed again, to the new PCR 7" || bad "re-seal after a dbx update" "$(val repin)"
        CUR=$PIN1
    else
        wait_for "THEFT poweroff" && ok "bound to no PCR: after the dbx update the PIN still starts AurOS" \
            || bad "the PIN after a dbx update (no PCR)" "$(serial | grep aurlock-pin | tail -3)"
    fi
else
    bad "the PIN page after a dbx update" "$(serial | tail -6)"
fi
power_off
if [ -n "$PCRS" ]; then
    # and the re-seal held: the same PIN opens it at the next start
    power_on reseal
    if wait_for "aurlock-pin: asking for the PIN" 300; then
        sleep 2; keys "$CUR"
        wait_for "THEFT poweroff" && ok "the next start: the same PIN, no rescue code" || bad "the PIN after the re-seal" "$(serial | grep aurlock-pin | tail -3)"
    else bad "the PIN page after the re-seal" "$(serial | tail -6)"; fi
    power_off
fi

# ── 5: a different chip ───────────────────────────────────────────
echo "  5  a different security chip" | tee -a "$EV/summary.txt"
TPMD="$T/tpm-b"; mkdir -p "$TPMD"
power_on newchip
if wait_for "aurlock-pin: asking for the PIN" 300; then
    sleep 2; keys "$CUR"; sleep 4
    shot changed
    serial | grep -q 'aurlock-pin: PIN changed' && ok "the page knows the computer changed (not \"wrong PIN\")" \
        || bad "a new chip -> changed" "$(serial | grep aurlock-pin)"
    keys "$CODE"; sleep 5
    shot choose
    serial | grep -q 'aurlock-pin: rescue code open' && ok "the rescue code opened the drive" || bad "the rescue code" "$(serial | grep aurlock-pin)"
    keys "$PIN2"; keys ret; sleep 1; keys "$PIN2"; keys ret
    wait_for "THEFT poweroff" && ok "she chose her PIN again and AurOS started" || bad "AurOS started after the rescue code" "$(serial | tail -5)"
    val repin | grep -q 'sealed again' && ok "the new PIN was sealed in the new chip" || bad "repin" "$(val repin)"
else
    bad "the PIN page with a new chip" "$(serial | tail -6)"
fi
power_off

# ── 6: the new PIN ────────────────────────────────────────────────
echo "  6  the new PIN, in the new chip" | tee -a "$EV/summary.txt"
power_on newpin
if wait_for "aurlock-pin: asking for the PIN" 300; then
    sleep 2; keys "$PIN2"
    wait_for "THEFT poweroff" && ok "the new PIN starts AurOS" || bad "the new PIN" "$(serial | grep aurlock-pin | tail -3)"
    [ "$(val letter)" = "$LSUM" ] && ok "her letter is still intact" || bad "her letter at the end"
else
    bad "the PIN page in the new chip" "$(serial | tail -6)"
fi
power_off

echo | tee -a "$EV/summary.txt"
if [ "$fail" -eq 0 ]; then echo "  $checked checks, all ok" | tee -a "$EV/summary.txt"; exit 0; fi
echo "  $fail of $checked checks FAILED" | tee -a "$EV/summary.txt"; exit 1
