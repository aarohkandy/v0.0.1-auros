#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  bootlocktest — does GRUB's menu keep a stranger out, and let her in?
#
#  Anyone at the keyboard of a PC that starts AurOS used to get GRUB's
#  menu, and with it `e` (edit the start line: add init=/bin/bash, and
#  the machine opens a root shell past her login and her disk's PIN) and
#  `c` (GRUB's command line). The menu is now locked with a password that
#  nobody knows (rootfs/usr/lib/auros/bootchain, lock_line), and the
#  ordinary starts are marked --unrestricted. This boots the REAL image
#  under OVMF with Secure Boot on and presses the keys a person would:
#
#    session 1   `e` asks for a user name; `c` asks for a user name;
#                the entry "Windows" does NOT ask: it starts
#                (start-windows says "Starting Windows").
#    session 2   hands off: the menu counts down, AurOS starts, the
#                desktop comes up, and nothing asked for a password.
#
#  ROLLBACK-style mutants, to show it can fail:
#    LOCKTEST_MUTANT=nolock      the superusers/password lines are removed:
#                                `e` must be seen to OPEN (the test fails)
#    LOCKTEST_MUTANT=lockedboot  --unrestricted removed from the AurOS
#                                entry: the ordinary start asks (fails)
#
#    sudo sh tools/bootlocktest.sh [profile]     (default: desktop)
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

PROFILE="${1:-desktop}"
E=tools/efivarstore.py
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
BOOT_TIMEOUT="${LOCKTEST_TIMEOUT:-300}"
MUTANT="${LOCKTEST_MUTANT:-}"

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

for t in qemu-system-x86_64 sgdisk python3 losetup mount mkfs.vfat mmd mcopy; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys"; exit 2; }
[ -f "out/auros-$PROFILE.img" ] || { echo "no out/auros-$PROFILE.img"; exit 2; }

LOCK="${TMPDIR:-/tmp}/installtest.lock"
mkdir "$LOCK" 2>/dev/null || { echo "another end-to-end test is running"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/bootlock.XXXXXX")
QP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    rm -rf "$T" "$LOCK"
}
trap cleanup EXIT

echo
echo "Does GRUB's menu keep a stranger out, and let her in?"
[ -n "$MUTANT" ] && echo "  (MUTANT: $MUTANT -- this run is meant to fail)"
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

DISK="$T/auros.img"
cp --sparse=always "out/auros-$PROFILE.img" "$DISK"
set -- $(part_of "$DISK" AUROS-ESP); ESPN=$1

# "Windows": an EFI partition with a file where the menu looks for one,
# so that the menu offers the entry. What it starts is not tested here.
WIN="$T/windows.img"
truncate -s 256M "$WIN"
sgdisk -n1:2048:+200M -t1:EF00 -c1:"EFI system partition" "$WIN" >/dev/null
dd if=/dev/zero of="$T/wesp.fat" bs=1M count=200 2>/dev/null
mkfs.vfat -F 32 "$T/wesp.fat" >/dev/null
mmd -i "$T/wesp.fat" ::/EFI ::/EFI/Microsoft ::/EFI/Microsoft/Boot
printf 'not a boot manager\n' > "$T/bootmgfw.efi"
mcopy -i "$T/wesp.fat" "$T/bootmgfw.efi" ::/EFI/Microsoft/Boot/bootmgfw.efi
dd if="$T/wesp.fat" of="$WIN" bs=512 seek=2048 conv=notrunc 2>/dev/null
rm -f "$T/wesp.fat"

# The stand-in for "the desktop came up", and the machine, confirmed.
mount_root || exit 2
R="$T/m"
mkdir -p "$R/var/lib/auros" "$R/usr/lib/auros-e2e" "$R/etc/auros-e2e"
printf 'confirmed by bootlocktest\n' > "$R/var/lib/auros/converted.confirmed"
C="$R/boot/grub/grub.cfg"
grep -q '^set superusers=' "$C" && grep -q '^password_pbkdf2 ' "$C" \
  && ok "the image's menu carries a lock" || bad "the image's menu carries a lock"
grep -v '^[[:space:]]*#' "$C" | grep -Eq 'init=/(usr/)?bin/(ba|da)?sh'  && bad "...and no entry starts a shell" || ok "...and no entry starts a shell"
# SEC-014: no serial terminal in the shipped menu. The keys below still
# reach it: OVMF carries the firmware console on the serial line, both ways.
grep -v '^[[:space:]]*#' "$C" | grep -Eq '^[[:space:]]*(serial([[:space:]]|$)|terminal_(input|output)[[:space:]].*serial)' \
  && bad "...and no serial terminal in it (the keys below arrive through the firmware's)" \
  || ok "...and no serial terminal in it (the keys below arrive through the firmware's)"
case "$MUTANT" in
  nolock)       sed -i '/^set superusers=/d; /^password_pbkdf2 /d' "$C" ;;
  lockedboot)   sed -i "s/--class auros --unrestricted --id auros {/--class auros --id auros {/" "$C" ;;
esac
# Which line of the menu is "Windows": the top-level entries before it.
WIDX=$(awk '/^(menuentry|submenu) /{ if ($0 ~ /--id windows[ {]/) { print n; exit } n++ }' "$C")
cat > "$R/usr/lib/auros-e2e/run.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. tools/bootlocktest.sh: says the desktop came up.
OUT=/var/lib/auros-e2e; mkdir -p "$OUT"
log() { printf '%s\n' "$*" >> "$OUT/log"; sync; }
log "up $(tr ' ' '\n' < /proc/cmdline | sed -n 's/^BOOT_IMAGE=//p')"
i=0; while [ ! -d /run/auros ] && [ "$i" -lt 240 ]; do sleep 1; i=$((i+1)); done
[ -d /run/auros ] && log "desktop after ${i}s" || log "desktop NEVER"
sync; systemctl poweroff
EOS
chmod 755 "$R/usr/lib/auros-e2e/run.sh"
cat > "$R/etc/systemd/system/auros-e2e.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: bootlocktest's record of a start
After=auros-hold.service aurshell.service
[Service]
Type=oneshot
ExecStart=/usr/lib/auros-e2e/run.sh
TimeoutStartSec=900
[Install]
WantedBy=multi-user.target
EOU
ln -sf /etc/systemd/system/auros-e2e.service "$R/etc/systemd/system/multi-user.target.wants/auros-e2e.service"
umount_root
[ -n "$WIDX" ] && ok "the menu offers Windows (entry number $WIDX)" || bad "the menu offers Windows" "no --id windows entry"
[ -n "$WIDX" ] || WIDX=3

NV="$T/vars.fd"; cp "$VARS" "$NV"
python3 "$E" "$NV" plant-hd Boot0002 "AurOS" '\EFI\AurOS\shimx64.efi' "$DISK" "$ESPN" &&
python3 "$E" "$NV" set-order 0002 || { echo "  could not make the firmware"; exit 2; }

qemu_args() {
    echo "-machine q35,smm=on,accel=tcg -m 2048 -smp 2 \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file=$CODE \
        -drive if=pflash,format=raw,unit=1,file=$NV \
        -drive file=$DISK,format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -drive file=$WIN,format=raw,if=none,id=d1 -device virtio-blk-pci,drive=d1 \
        -device virtio-vga -display none -net none"
}

# ── session 1: the keys ─────────────────────────────────────────────
echo "  session 1: a person at the keyboard presses e, c, then chooses Windows"
S="$T/sock1"
# shellcheck disable=SC2046
qemu-system-x86_64 $(qemu_args) -chardev "socket,id=s0,path=$S,server=on,wait=on" \
    -serial chardev:s0 >/dev/null 2>&1 &
QP=$!
python3 - "$S" "$WIDX" "$T/serial.1" > "$T/keys.out" <<'EOPY'
import socket, sys, time
path, widx, logf = sys.argv[1], int(sys.argv[2]), sys.argv[3]
s = socket.socket(socket.AF_UNIX)
for _ in range(300):
    try: s.connect(path); break
    except OSError: time.sleep(0.1)
s.settimeout(0.5); log = open(logf, 'wb'); buf = b''
def pump():
    global buf
    try: d = s.recv(4096)
    except socket.timeout: d = b''
    if d: buf += d; log.write(d); log.flush()
def wait(marks, secs, since=0):
    end = time.time() + secs
    while time.time() < end:
        pump()
        for i, m in enumerate(marks):
            if m in buf[since:]: return i
    return -1
def settle(secs):
    end = time.time() + secs
    while time.time() < end: pump()
res = {}
if wait([b'automatically in'], 180) < 0: print('nomenu'); sys.exit()
for key, name, opens in ((b'e', 'edit', [b'Emacs-like', b'BASH-like', b'setparams', b'init=']),
                         (b'c', 'console', [b'grub>', b'BASH-like'])):
    mark = len(buf)
    s.send(key)
    r = wait([b'Enter username'] + opens, 20, mark)
    res[name] = 'asks' if r == 0 else 'opens' if r > 0 else 'nothing'
    s.send(b'\x1b'); settle(2.5)       # Escape: back to the menu
    if res[name] == 'opens': s.send(b'\x1b'); settle(1.5)
    # the menu is up again; keep the countdown stopped
for _ in range(widx):
    s.send(b'\x1b[B'); settle(0.4)
mark = len(buf)
s.send(b'\r')
r = wait([b'Enter username', b'Starting Windows'], 150, mark)
res['windows'] = 'asks' if r == 0 else 'starts' if r == 1 else 'nothing'
print(' '.join('%s=%s' % kv for kv in res.items()))
EOPY
kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
K=$(cat "$T/keys.out")
case "$K" in *nomenu*) bad "the menu was reached" "no menu on the serial line"; K="" ;; esac
case "$K" in *"edit=asks"*) ok "e (edit a start line) asks for a user name" ;;
    *) bad "e (edit a start line) asks for a user name" "$K" ;; esac
case "$K" in *"console=asks"*) ok "c (GRUB's command line) asks for a user name" ;;
    *) bad "c (GRUB's command line) asks for a user name" "$K" ;; esac
case "$K" in *"windows=starts"*) ok "choosing Windows does not ask: it starts" ;;
    *) bad "choosing Windows does not ask: it starts" "$K" ;; esac

# ── session 2: hands off ────────────────────────────────────────────
echo
echo "  session 2: nobody touches anything"
cp "$VARS" "$NV"
python3 "$E" "$NV" plant-hd Boot0002 "AurOS" '\EFI\AurOS\shimx64.efi' "$DISK" "$ESPN" &&
python3 "$E" "$NV" set-order 0002
SER="$T/serial.2"; : > "$SER"
# shellcheck disable=SC2046
qemu-system-x86_64 $(qemu_args) -serial file:"$SER" >/dev/null 2>&1 &
QP=$!
i=0
while kill -0 "$QP" 2>/dev/null && [ "$i" -lt "$BOOT_TIMEOUT" ]; do sleep 2; i=$((i + 2)); done
kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
mount_root; L=$(cat "$T/m/var/lib/auros-e2e/log" 2>/dev/null); umount_root
grep -aq 'Enter username' "$SER" && bad "the ordinary start never asks for a password" "it asked" \
  || ok "the ordinary start never asks for a password"
case "$L" in *"desktop after"*) ok "...AurOS starts and the desktop comes up" ;;
    *) bad "...AurOS starts and the desktop comes up" "$L" ;; esac

echo
echo "bootlocktest: $((checked - fail))/$checked"
[ "$fail" = 0 ]
