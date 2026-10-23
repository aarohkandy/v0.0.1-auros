#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  rollbacktest — when a new kernel does not start, does AurOS start
#  by itself, with nobody touching anything?
#
#  Ubuntu's kernels, shim and grub update AurOS by themselves
#  (51auros-unattended), so one day a kernel that does not start will
#  arrive on a PC whose owner will never see a menu. The start-up menu
#  counts starts nothing confirmed (auros_try) and, after two in a row,
#  uses the kernel before and sets the newest aside (auros_bad); the
#  desktop confirms a start two minutes after it is up (auros-boot-ok).
#  This boots the REAL image under OVMF with Secure Boot on, keys and
#  all, and breaks it the way a bad kernel update does:
#
#    run 1  the newest kernel (6.8.0-999) is the real signed kernel with
#           an initramfs that cannot start: PID 1 exits, the kernel
#           panics, panic=10 restarts the computer. Nobody touches the
#           machine; the same VM keeps restarting. It must panic twice
#           and then start the kernel before, reach the desktop, and
#           confirm that start.
#    run 2  an ordinary start afterwards: the kernel before, at once
#           (the bad one is remembered), no panic.
#    run 3  a NEWER kernel arrives (1000, good) and the menu is written
#           again: it starts. The first start of it is cut short
#           (powered off at once): that is not a verdict.
#    run 4  an ordinary start: still the new kernel, and confirmed.
#    run 5  a still newer kernel (1001) starts, and its desktop never
#           appears (a graphics driver that hangs the desktop, as far as
#           anybody can tell from outside). Nobody touches anything:
#           auros-boot-guard restarts the computer when the start is not
#           confirmed in time, twice; the third start is the kernel
#           before, with a desktop.
#
#  ROLLBACKTEST_MUTANT=nocount removes the counting from the menu,
#  =noclear stops auros-boot-ok clearing it and =noguard stops
#  auros-boot-guard restarting; each makes this test fail, which is how
#  it is shown to test something.
#
#  NOT REAL: the "bad kernel" (a good one with a dead initramfs), and
#  the two-minute wait, which the test shortens to ten seconds
#  (AUROS_BOOT_OK_SECS). Not covered here: a kernel that hangs without
#  a panic, and a desktop that crashes in a loop (WP-30, full version).
#
#    sudo sh tools/rollbacktest.sh [profile]     (default: desktop)
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

PROFILE="${1:-desktop}"
E=tools/efivarstore.py
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
BOOT_TIMEOUT="${ROLLBACK_TIMEOUT:-420}"
GOOD=6.8.0-147-generic
BAD=6.8.0-999-generic
NEW=6.8.0-1000-generic
MUTANT="${ROLLBACKTEST_MUTANT:-}"

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

for t in qemu-system-x86_64 sgdisk python3 losetup mount gcc cpio gzip grub-editenv; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys"; exit 2; }
[ -f "out/auros-$PROFILE.img" ] || { echo "no out/auros-$PROFILE.img"; exit 2; }

LOCK="${TMPDIR:-/tmp}/installtest.lock"
mkdir "$LOCK" 2>/dev/null || { echo "another end-to-end test is running"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/rollback.XXXXXX")
QP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    rm -rf "$T" "$LOCK"
}
trap cleanup EXIT

echo
echo "When a new kernel does not start, does AurOS start by itself?"
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
menu() { # write the menu the way a kernel update does
    python3 rootfs/usr/lib/auros/bootchain menu --root "$T/m" --running "$GOOD" >/dev/null 2>&1
}

DISK="$T/auros.img"
cp --sparse=always "out/auros-$PROFILE.img" "$DISK"
set -- $(part_of "$DISK" AUROS-ESP); ESPN=$1

# ── a kernel that panics: the real signed one, and an initramfs whose
# PID 1 exits ───────────────────────────────────────────────────────
cat > "$T/dead.c" <<'EOC'
int main(void) { return 0; }
EOC
mkdir -p "$T/ir"
gcc -static -O1 -o "$T/ir/init" "$T/dead.c" 2>/dev/null || { echo "  could not build the dead initramfs"; exit 2; }
( cd "$T/ir" && echo init | cpio -o -H newc --quiet | gzip ) > "$T/dead.img"

mount_root || exit 2
R="$T/m"
[ -x "$R/usr/lib/auros/boot-ok" ] && ok "the image carries auros-boot-ok" || bad "the image carries auros-boot-ok"
[ -L "$R/etc/systemd/system/multi-user.target.wants/auros-boot-ok.service" ] \
  && ok "...and it is switched on" || bad "...and it is switched on"
printf 'confirmed by rollbacktest\n' > "$R/var/lib/auros/converted.confirmed" 2>/dev/null \
    || { mkdir -p "$R/var/lib/auros"; printf 'confirmed by rollbacktest\n' > "$R/var/lib/auros/converted.confirmed"; }
cp "$R/boot/vmlinuz-$GOOD" "$R/boot/vmlinuz-$BAD"
cp "$T/dead.img" "$R/boot/initrd.img-$BAD"
menu
grep -q '^set auros_prev=1$' "$R/boot/grub/grub.cfg" && grep -A1 -- '--id auros {' "$R/boot/grub/grub.cfg" | grep -q "vmlinuz-$BAD " \
  && ok "the menu starts the new kernel, with the one before it to fall back to" \
  || bad "the menu starts the new kernel, with the one before it to fall back to" "$(grep -n 'vmlinuz\|auros_prev' "$R/boot/grub/grub.cfg" | head -4)"
case "$MUTANT" in
  debug) # not a mutant: says what the menu sees, on the serial line
         sed -i 's|^function auros_count {|function auros_count {\n  echo "BT: count sees try=[${auros_try}]"|; s|^  save_env --file (\$root)/boot/grub/grubenv auros_try|  echo "BT: count sets try=[${auros_try}]"\n&|; s|^if \[ -z "\${auros_req}" \]; then|echo "BT: menu sees try=[${auros_try}] bad=[${auros_bad}] prev=[${auros_prev}]"\n&|' "$R/boot/grub/grub.cfg" ;;
  nocount) sed -i '/^ *auros_count$/d' "$R/boot/grub/grub.cfg" ;;
  noclear) printf '#!/bin/sh\nexit 0\n' > "$R/usr/lib/auros/boot-ok" ;;
  noguard) printf '#!/bin/sh\nexit 0\n' > "$R/usr/lib/auros/boot-guard" ;;
esac

# the stand-in for "the desktop came up" and for nobody doing anything
mkdir -p "$R/usr/lib/auros-e2e" "$R/etc/auros-e2e" "$R/etc/systemd/system/auros-boot-ok.service.d"
printf '[Service]\nEnvironment=AUROS_BOOT_OK_SECS=10\n' > "$R/etc/systemd/system/auros-boot-ok.service.d/test.conf"
mkdir -p "$R/etc/systemd/system/auros-boot-guard.service.d"
printf '[Service]\nEnvironment=AUROS_GUARD_SECS=25\n' > "$R/etc/systemd/system/auros-boot-guard.service.d/test.conf"
[ -L "$R/etc/systemd/system/multi-user.target.wants/auros-boot-guard.service" ] \
  && ok "the image carries auros-boot-guard, switched on" || bad "the image carries auros-boot-guard, switched on"
cat > "$R/usr/lib/auros-e2e/run.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. tools/rollbacktest.sh puts this into a scratch copy
# of the image. It only writes down what each start did, and powers off
# once a start has been confirmed (or, in "quick" mode, at once).
OUT=/var/lib/auros-e2e
mkdir -p "$OUT"
mode=$(cat /etc/auros-e2e/mode 2>/dev/null || echo none)
run=$(cat /etc/auros-e2e/run 2>/dev/null || echo 0)
n=$(( $(cat "$OUT/n" 2>/dev/null || echo 0) + 1 )); echo "$n" > "$OUT/n"
log() { printf 'run%s start%s %s\n' "$run" "$n" "$*" >> "$OUT/log"; sync; }
img=$(tr ' ' '\n' < /proc/cmdline | sed -n 's/^BOOT_IMAGE=//p')
log "up mode=$mode kernel=${img##*/boot/vmlinuz-} running=$(uname -r)"
# /run/auros exists as soon as aurshell.service is TRIED (its
# RuntimeDirectory); a desktop that keeps failing needs "is it active".
up() { if [ "$mode" = guard ]; then systemctl is-active --quiet aurshell.service; else [ -d /run/auros ]; fi; }
lim=300; [ "$mode" = guard ] && lim=40
i=0
while ! up && [ "$i" -lt "$lim" ]; do sleep 1; i=$((i+1)); done
up && log "desktop after ${i}s" || log "desktop NEVER"
# No desktop, and nobody to restart it: leave it to auros-boot-guard.
if [ "$mode" = guard ] && ! up; then log "waiting to be restarted"; exit 0; fi
if [ "$mode" = quick ]; then log "cut short"; sync; systemctl poweroff; exit 0; fi
i=0
while grep -aq '^auros_try=' /boot/grub/grubenv && [ "$i" -lt 240 ]; do sleep 2; i=$((i+2)); done
if grep -aq '^auros_try=' /boot/grub/grubenv; then log "NOT CONFIRMED after ${i}s"
else log "confirmed after ${i}s"; fi
grep -a '^auros_' /boot/grub/grubenv | tr '\n' ' ' > "$OUT/env.run$run"
sync
systemctl poweroff
EOS
chmod 755 "$R/usr/lib/auros-e2e/run.sh"
cat > "$R/etc/systemd/system/auros-e2e.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: rollbacktest's record of each start
# Not After=aurshell.service: a desktop that keeps failing is "activating"
# for ever, and a unit ordered after it would never start to say so.
After=auros-hold.service
[Service]
Type=oneshot
ExecStart=/usr/lib/auros-e2e/run.sh
TimeoutStartSec=900
[Install]
WantedBy=multi-user.target
EOU
ln -sf /etc/systemd/system/auros-e2e.service "$R/etc/systemd/system/multi-user.target.wants/auros-e2e.service"
umount_root

# Firmware as a confirmed install leaves it: AurOS first.
NV="$T/vars.fd"; cp "$VARS" "$NV"
python3 "$E" "$NV" plant-hd Boot0002 "AurOS" '\EFI\AurOS\shimx64.efi' "$DISK" "$ESPN" &&
python3 "$E" "$NV" set-order 0002 || { echo "  could not make the firmware"; exit 2; }

RUN=0
# One power-on: the machine restarts itself as often as it likes (no
# -no-reboot) until the guest switches itself off.
power_on() { # mode
    RUN=$((RUN + 1))
    mount_root || return 1
    printf '%s\n' "$1" > "$T/m/etc/auros-e2e/mode"
    printf '%s\n' "$RUN" > "$T/m/etc/auros-e2e/run"
    umount_root
    SER="$T/serial.$RUN"; : > "$SER"
    qemu-system-x86_64 -machine q35,smm=on,accel=tcg -m 2048 -smp 2 \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$NV" \
        -drive file="$DISK",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -device virtio-vga -display none -serial file:"$SER" -net none \
        >/dev/null 2>&1 &
    QP=$!
    i=0
    while kill -0 "$QP" 2>/dev/null && [ "$i" -lt "$BOOT_TIMEOUT" ]; do sleep 2; i=$((i + 2)); done
    TIMED_OUT=0
    kill -0 "$QP" 2>/dev/null && TIMED_OUT=1
    kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
    tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;?=]*[a-zA-Z]//g' > "$SER.txt"
}
e2e_log() { mount_root && cat "$T/m/var/lib/auros-e2e/log" 2>/dev/null; umount_root; }
grubenv_now() { mount_root && grub-editenv "$T/m/boot/grub/grubenv" list 2>/dev/null | tr '\n' ' '; umount_root; }
panics() { grep -ac 'Kernel panic - not syncing' "$SER.txt"; }
starts() { e2e_log | grep -c "^run$1 start[0-9]* up "; }

[ -n "${ROLLBACK_ONLY:-}" ] && BOOT_TIMEOUT=$ROLLBACK_ONLY
# ── run 1: the newest kernel panics ─────────────────────────────────
echo "  run 1: the new kernel panics; nobody touches anything"
power_on none
L=$(e2e_log)
P=$(panics)
[ "$P" = 2 ] && ok "it panicked twice, and was restarted each time by panic=10" \
             || bad "it panicked twice, and was restarted each time by panic=10" "panics=$P" "$(tail -3 "$SER.txt")"
echo "$L" | grep -q "^run1 start1 up .*kernel=$GOOD running=$GOOD" \
  && ok "the third start was the kernel before it (6.8.0-147)" \
  || bad "the third start was the kernel before it (6.8.0-147)" "$L"
echo "$L" | grep -q "^run1 start1 desktop after" && ok "...and the desktop came up" \
  || bad "...and the desktop came up" "$L"
echo "$L" | grep -q "^run1 start1 confirmed after" && ok "...and that start was confirmed" \
  || bad "...and that start was confirmed" "$L" "grubenv: $(grubenv_now)"
GE=$(grubenv_now)
case "$GE" in *auros_try=*) bad "...which cleared the count" "$GE" ;; *) ok "...which cleared the count" ;; esac
case "$GE" in *auros_bad=$BAD*) ok "...and the new kernel is set aside as bad" ;;
    *) bad "...and the new kernel is set aside as bad" "$GE" ;; esac
[ "$TIMED_OUT" = 0 ] && ok "...and it all took no input and no more than ${BOOT_TIMEOUT}s" \
                     || bad "...and it all took no input and no more than ${BOOT_TIMEOUT}s"

[ -n "${ROLLBACK_ONLY:-}" ] && { grep -a 'BT:' "$SER.txt" | sed 's/\x1b\[[0-9;?=]*[a-zA-Z]//g'; grub-editenv "$T/m/boot/grub/grubenv" list 2>/dev/null; mount_root && grub-editenv "$T/m/boot/grub/grubenv" list; umount_root; [ -n "${ROLLBACK_KEEP:-}" ] && { mkdir -p "$ROLLBACK_KEEP"; cp "$T"/serial.*.txt "$ROLLBACK_KEEP"/; }; exit 0; }
# ── run 2: an ordinary start afterwards ─────────────────────────────
echo
echo "  run 2: the next ordinary start"
power_on none
L=$(e2e_log)
[ "$(panics)" = 0 ] && ok "no panic: the bad kernel is not tried again" \
                    || bad "no panic: the bad kernel is not tried again" "panics=$(panics)"
echo "$L" | grep -q "^run2 start[0-9]* up .*kernel=$GOOD" && ok "...it starts the kernel before, at once" \
  || bad "...it starts the kernel before, at once" "$(echo "$L" | grep '^run2')"
echo "$L" | grep -q "^run2 .*confirmed after" && ok "...and is confirmed" \
  || bad "...and is confirmed" "$(echo "$L" | grep '^run2')"

# ── run 3 and 4: a newer kernel arrives; a cut-short start is no verdict
echo
echo "  run 3: a newer, good kernel arrives; its first start is cut short"
mount_root
cp "$T/m/boot/vmlinuz-$GOOD" "$T/m/boot/vmlinuz-$NEW"
cp "$T/m/boot/initrd.img-$GOOD" "$T/m/boot/initrd.img-$NEW"
menu
case "$MUTANT" in nocount) sed -i '/^ *auros_count$/d' "$T/m/boot/grub/grub.cfg" ;; esac
umount_root
power_on quick
L=$(e2e_log)
echo "$L" | grep -q "^run3 start[0-9]* up .*kernel=$NEW" && ok "the new kernel is tried, though the old one was set aside as bad" \
  || bad "the new kernel is tried, though the old one was set aside as bad" "$(echo "$L" | grep '^run3')"
echo
echo "  run 4: an ordinary start"
power_on none
L=$(e2e_log)
echo "$L" | grep -q "^run4 start[0-9]* up .*kernel=$NEW" && ok "one cut-short start does not send it back: still the new kernel" \
  || bad "one cut-short start does not send it back: still the new kernel" "$(echo "$L" | grep '^run4')"
echo "$L" | grep -q "^run4 .*confirmed after" && ok "...and it is confirmed" \
  || bad "...and it is confirmed" "$(echo "$L" | grep '^run4')"

# ── run 5: the desktop never appears ────────────────────────────────
echo
echo "  run 5: a newer kernel starts but its desktop never appears; nobody touches anything"
NEW2=6.8.0-1001-generic
mount_root
cp "$T/m/boot/vmlinuz-$GOOD" "$T/m/boot/vmlinuz-$NEW2"
cp "$T/m/boot/initrd.img-$GOOD" "$T/m/boot/initrd.img-$NEW2"
menu
# On that kernel only, the desktop refuses to start (so the computer is up,
# the kernel is fine, and she sees no desktop).
mkdir -p "$T/m/etc/systemd/system/aurshell.service.d"
printf "[Service]\nExecStartPre=/bin/sh -c '! grep -q vmlinuz-%s /proc/cmdline'\n" "$NEW2" \
    > "$T/m/etc/systemd/system/aurshell.service.d/zz-test-nodesktop.conf"
case "$MUTANT" in nocount) sed -i '/^ *auros_count$/d' "$T/m/boot/grub/grub.cfg" ;; esac
umount_root
power_on guard
L=$(e2e_log)
n5=$(echo "$L" | grep -c "^run5 start[0-9]* up .*kernel=$NEW2")
[ "$n5" = 2 ] && ok "the new kernel started twice, and twice showed no desktop" \
  || bad "the new kernel started twice, and twice showed no desktop" "starts on it: $n5" "$(echo "$L" | grep '^run5')"
[ "$(grep -ac 'boot-guard: this start was not confirmed' "$SER.txt")" = 2 ] && ok "...each restarted by auros-boot-guard, not by anybody" \
  || bad "...each restarted by auros-boot-guard, not by anybody" "guard said it $(grep -ac 'boot-guard: this start was not confirmed' "$SER.txt") times" "$(echo "$L" | grep '^run5')"
echo "$L" | grep -q "^run5 start[0-9]* up .*kernel=$GOOD" && echo "$L" | grep -q "^run5 .*confirmed after" \
  && ok "the third start was the kernel before it: a desktop, confirmed" \
  || bad "the third start was the kernel before it: a desktop, confirmed" "$(echo "$L" | grep '^run5')"
[ "$TIMED_OUT" = 0 ] && ok "...with no input, no more than ${BOOT_TIMEOUT}s" || bad "...with no input, no more than ${BOOT_TIMEOUT}s"

# For whoever is working out why it failed: the serial lines, kept.
if [ -n "${ROLLBACK_KEEP:-}" ]; then mkdir -p "$ROLLBACK_KEEP"; cp "$T"/serial.*.txt "$ROLLBACK_KEEP"/ 2>/dev/null; fi
echo
echo "rollbacktest: $((checked - fail))/$checked"
[ "$fail" = 0 ]
