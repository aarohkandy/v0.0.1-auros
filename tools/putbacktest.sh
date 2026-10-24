#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  putbacktest — does "Put Windows back" in AurOS reach the restore?
#
#  The restore itself (aurstage.restore) is installtest's and
#  nosticktest's: install, put Windows back, every Windows file
#  byte-identical. What they boot it with is QEMU's -kernel. What a
#  person has is a Settings page in a running AurOS, and a line in
#  AurOS's own start-up menu. This boots the REAL image, with Secure
#  Boot on and Microsoft's keys, and follows the button all the way:
#
#    boot 1  AurOS, on a machine whose person said "it does not work"
#            (so nothing would bring it back to AurOS by itself). A
#            stand-in presses "Put Windows back" the way Settings does:
#            the bare word, as the desktop user, O_EXCL. answer.sh must
#            answer ok, set the menu's next_entry, arm BootNext at
#            AurOS's entry, and restart.
#    boot 2  the firmware starts AurOS's menu, the menu takes the
#            restore entry by itself, and grub starts the installer's
#            Canonical-signed staging kernel from the OTHER disk's EFI
#            partition with aurstage.restore -- under Secure Boot. The
#            restore reports. next_entry is gone afterwards.
#    boot 3  an ordinary start: AurOS, not the restore again.
#    boot 4  she presses it again, and this time the firmware does not
#            act on the restart it was asked for (it starts Windows).
#    boot 5  three weeks later (the clock moved on) AurOS is started:
#            the stale request reaches the restore, which must refuse
#            it -- nothing changed -- and restart by itself.
#    boot 6  an ordinary start: AurOS.
#
#  The "Windows" disk here holds an EFI partition with the installer's
#  files in \EFI\AurOS and nothing to restore, so the restore's verdict
#  is restore-nothing-saved and the disk must come out unchanged. That
#  is the point where this test stops and installtest's begins.
#
#  WHAT IS NOT REAL: the stand-in for the button (as in firstboottest),
#  and one grubenv variable, aurstage_extra, which adds a serial console
#  to the restore so this can read it. Nothing a person does sets it.
#
#    sudo sh tools/putbacktest.sh [profile]     (default: desktop)
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

PROFILE="${1:-desktop}"
E=tools/efivarstore.py
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
BOOT_TIMEOUT="${PUTBACK_TIMEOUT:-1200}"
POLL_INTERVAL="${PUTBACK_POLL_INTERVAL:-2}"
EVIDENCE="${PUTBACK_EVIDENCE:-$PWD/transcripts/putbacktest-evidence}"

case "$BOOT_TIMEOUT:$POLL_INTERVAL" in
    *[!0-9:]*|:*|*:) echo "PUTBACK_TIMEOUT and PUTBACK_POLL_INTERVAL must be positive integers"; exit 2 ;;
esac
[ "$BOOT_TIMEOUT" -gt 0 ] 2>/dev/null && [ "$POLL_INTERVAL" -gt 0 ] 2>/dev/null \
    || { echo "PUTBACK_TIMEOUT and PUTBACK_POLL_INTERVAL must be positive integers"; exit 2; }

# Return 124 only when a still-running boot reaches its deadline. A normal
# shutdown and the restore's final report are both successful ends.
wait_for_boot() { # pid serial
    deadline=$(( $(date +%s) + BOOT_TIMEOUT ))
    while kill -0 "$1" 2>/dev/null; do
        now=$(date +%s)
        [ "$now" -ge "$deadline" ] && return 124
        # Bound every scan: serial output can grow for the life of the VM.
        if tail -c 1048576 "$2" 2>/dev/null | grep -aq 'aurstage-report v1 verdict=restore'; then
            left=$((deadline - $(date +%s)))
            [ "$left" -le 0 ] && return 124
            [ "$left" -lt 3 ] && sleep "$left" || sleep 3
            return 0
        fi
        now=$(date +%s)
        [ "$now" -ge "$deadline" ] && return 124
        left=$((deadline - now))
        [ "$left" -lt "$POLL_INTERVAL" ] && sleep "$left" || sleep "$POLL_INTERVAL"
    done
    return 0
}

if [ "${PUTBACK_SELFTEST:-}" != timeout ]; then
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-62s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-62s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

for t in qemu-system-x86_64 sgdisk python3 losetup mount mkfs.vfat mmd mcopy \
         grub-editenv; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys"; exit 2; }
[ -f "out/auros-$PROFILE.img" ] || { echo "no out/auros-$PROFILE.img"; exit 2; }
for f in out/auros-staging-vmlinuz out/auros-staging.img; do
    [ -f "$f" ] || { echo "no $f: run ./build/staging"; exit 2; }
done

LOCK="${TMPDIR:-/tmp}/installtest.lock"
mkdir "$LOCK" 2>/dev/null || { echo "another end-to-end test is running"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/putback.XXXXXX")
QP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    rm -rf "$T" "$LOCK"
}
trap cleanup EXIT

echo
echo "Does \"Put Windows back\" in AurOS reach the restore, with Secure Boot on?"
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

# ── the machine ─────────────────────────────────────────────────────
DISK="$T/auros.img"
cp --sparse=always "out/auros-$PROFILE.img" "$DISK"

# The "Windows" disk: GPT, one EFI partition, the installer's files in
# \EFI\AurOS exactly where AurBridge leaves them. Nothing to restore.
WIN="$T/windows.img"
truncate -s 256M "$WIN"
sgdisk -n1:2048:+200M -t1:EF00 -c1:"EFI system partition" "$WIN" >/dev/null
EO=$((2048 * 512))
dd if=/dev/zero of="$T/esp.fat" bs=1M count=200 2>/dev/null
mkfs.vfat -F 32 "$T/esp.fat" >/dev/null
mmd -i "$T/esp.fat" ::/EFI ::/EFI/AurOS ::/EFI/Microsoft ::/EFI/Microsoft/Boot
mcopy -i "$T/esp.fat" out/auros-staging-vmlinuz ::/EFI/AurOS/staging.efi
mcopy -i "$T/esp.fat" out/auros-staging.img    ::/EFI/AurOS/staging.img
dd if="$T/esp.fat" of="$WIN" bs=512 seek=2048 conv=notrunc 2>/dev/null
rm -f "$T/esp.fat"
WIN_BEFORE=$(md5sum "$WIN" | cut -d' ' -f1)

# Declined: the stamp aurfirst writes when she says "it does not work",
# so the hold re-arms nothing and only "Put Windows back" can bring the
# next start back to AurOS. The stand-in for the button, as firstboottest.
mount_root || exit 2
mkdir -p "$T/m/var/lib/auros" "$T/m/usr/lib/auros-e2e" "$T/m/etc/auros-e2e"
printf 'declined by putbacktest\n' > "$T/m/var/lib/auros/converted.declined"
cat > "$T/m/usr/lib/auros-e2e/run.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. tools/putbacktest.sh puts this into a scratch copy
# of the image. It stands in for one press of "Put Windows back".
OUT=/var/lib/auros-e2e
mkdir -p "$OUT"
mode=$(cat /etc/auros-e2e/mode 2>/dev/null || echo none)
n=$(cat /etc/auros-e2e/boot 2>/dev/null || echo 0)
log() { printf '%s %s\n' "$n" "$*" >> "$OUT/log"; sync; }
sb=$(od -An -t u1 /sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c 2>/dev/null | awk '{print $NF}')
log "up mode=$mode sb=$sb"
i=0
while [ ! -d /run/auros ] && [ "$i" -lt 300 ]; do sleep 1; i=$((i+1)); done
[ -d /run/auros ] && log "desktop after ${i}s" || log "desktop NEVER"
if [ "$mode" = putback ]; then
    u=$(stat -c %U /run/auros 2>/dev/null)
    if runuser -u "$u" -- sh -c "umask 077; set -C; printf %s putback > /run/auros/answer"
    then log "pressed putback as $u"; else log "could not press putback as $u"; fi
    i=0
    while [ "$i" -lt 180 ]; do
        grep -qx "request=putback" /run/auros-answer/result 2>/dev/null && break
        sleep 1; i=$((i+1))
    done
    cp /run/auros-answer/result "$OUT/result.$n" 2>/dev/null \
        && log "answered after ${i}s" || log "NO ANSWER after ${i}s"
    cp /var/log/auros-answer.log "$OUT/answer.log.$n" 2>/dev/null
    sync
    # answer.sh restarts the machine itself. If it has not within a
    # minute, it is not going to, and the test is told so by the log.
    sleep 60
    log "NO RESTART"
fi
sync
systemctl poweroff
EOS
chmod 755 "$T/m/usr/lib/auros-e2e/run.sh"
# "Put Windows back" first saves her files into Windows (`ferry back`,
# answer.sh). This machine's "Windows" is an EFI partition with the
# installer's files and no Windows drive to save into, which the real
# Ferry refuses (exit 7) -- correctly, and that is Ferry's own test
# (src/ferry/tests/sandbox.sh, scenario 3, under this unit's sandbox).
# What this test is about is the chain after it, so Ferry is a program
# that saved everything.
rm -f "$T/m/usr/bin/ferry"
printf '#!/bin/sh\n# NOT PART OF AUROS: tools/putbacktest.sh. Ferry'"'"'s own tests save files.\nexit 0\n' > "$T/m/usr/bin/ferry"
chmod 755 "$T/m/usr/bin/ferry"
cat > "$T/m/etc/systemd/system/auros-e2e.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: putbacktest's stand-in for one button press
After=auros-hold.service aurshell.service
[Service]
Type=oneshot
ExecStart=/usr/lib/auros-e2e/run.sh
TimeoutStartSec=900
[Install]
WantedBy=multi-user.target
EOU
ln -sf /etc/systemd/system/auros-e2e.service \
    "$T/m/etc/systemd/system/multi-user.target.wants/auros-e2e.service"
[ -f "$T/m/boot/grub/grubenv" ] && ok "the image carries grub's environment block" \
    || bad "the image carries grub's environment block"
grep -q "put-windows-back-yes" "$T/m/boot/grub/grub.cfg" \
    && ok "...and a Put Windows back entry in its menu" \
    || bad "...and a Put Windows back entry in its menu"
umount_root

# Firmware as an install leaves it, with Secure Boot on: Windows first
# in BootOrder, AurOS's entry, and the one restart armed so boot 1 is
# AurOS. After that nothing re-arms: she declined.
set -- $(part_of "$DISK" AUROS-ESP)
NV="$T/vars.fd"; cp "$VARS" "$NV"
python3 "$E" "$NV" plant Boot0000 "Windows Boot Manager" '\EFI\Microsoft\Boot\bootmgfw.efi' &&
python3 "$E" "$NV" plant-hd Boot0002 "AurOS" '\EFI\AurOS\shimx64.efi' "$DISK" "$1" &&
python3 "$E" "$NV" set-order 0000 &&
python3 "$E" "$NV" set-next 0002 || { echo "  could not make the firmware"; exit 2; }

fi

BOOTN=0
QEXTRA=""
capture_framebuffer() { # qmp output
    python3 - "$1" "$2" <<'PY'
import json, socket, sys, time

sock = socket.socket(socket.AF_UNIX)
sock.settimeout(1)
deadline = time.monotonic() + 3
while True:
    try:
        sock.connect(sys.argv[1])
        break
    except (FileNotFoundError, ConnectionRefusedError):
        if time.monotonic() >= deadline:
            raise
        time.sleep(0.1)
stream = sock.makefile("rwb", buffering=0)
stream.readline()
for command in (
    {"execute": "qmp_capabilities"},
    {"execute": "screendump", "arguments": {"filename": sys.argv[2]}},
):
    stream.write(json.dumps(command).encode() + b"\n")
    while True:
        reply = json.loads(stream.readline())
        if "return" in reply:
            break
        if "error" in reply:
            raise RuntimeError(reply["error"])
sock.close()
PY
}

save_timeout_evidence() { # boot serial qmp
    mkdir -p "$EVIDENCE"
    tail -c 1048576 "$2" > "$EVIDENCE/serial-$1.log" 2>/dev/null || :
    capture_framebuffer "$3" "$EVIDENCE/framebuffer-$1.ppm" 2> "$EVIDENCE/framebuffer-$1.error" || :
    [ -s "$EVIDENCE/framebuffer-$1.error" ] || rm -f "$EVIDENCE/framebuffer-$1.error"
}

save_guest_evidence() { # boot
    if mount_root; then
        tail -c 1048576 "$T/m/var/lib/auros-e2e/log" > "$EVIDENCE/guest-$1.log" 2>/dev/null || :
        grub-editenv "$T/m/boot/grub/grubenv" list > "$EVIDENCE/grubenv-$1.txt" 2>&1 || :
        for f in "$T/m/var/lib/auros-e2e"/answer.log.* "$T/m/var/lib/auros-e2e"/result.*; do
            [ -f "$f" ] || continue
            tail -c 262144 "$f" > "$EVIDENCE/$(basename "$f")"
        done
        umount_root
    else
        printf 'could not mount AUROS-ROOT after timeout\n' > "$EVIDENCE/guest-$1.error"
    fi
}

power_on() { # mode
    BOOTN=$((BOOTN + 1))
    mount_root || return 1
    printf '%s\n' "$1" > "$T/m/etc/auros-e2e/mode"
    printf '%s\n' "$BOOTN" > "$T/m/etc/auros-e2e/boot"
    umount_root
    SER="$T/serial.$BOOTN"; QMP="$T/qmp.$BOOTN"; : > "$SER"
    qemu-system-x86_64 -machine q35,smm=on,accel=tcg -m 2048 -smp 2 -no-reboot \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$NV" \
        -drive file="$DISK",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -drive file="$WIN",format=raw,if=none,id=d1 -device virtio-blk-pci,drive=d1 \
        -device virtio-vga -display none -serial file:"$SER" \
        -qmp unix:"$QMP",server=on,wait=off -net none $QEXTRA \
        >/dev/null 2>&1 &
    QP=$!
    wait_for_boot "$QP" "$SER"; rc=$?
    [ "$rc" -eq 124 ] && save_timeout_evidence "$BOOTN" "$SER" "$QMP"
    kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
    if [ "$rc" -eq 124 ]; then
        save_guest_evidence "$BOOTN"
        echo "  ERROR: boot $BOOTN exceeded its ${BOOT_TIMEOUT}s deadline"
        echo "  diagnostic evidence: $EVIDENCE"
        return 124
    fi
    return 0
}

# Exercise the real boot timeout and evidence path without an image or a VM.
if [ "${PUTBACK_SELFTEST:-}" = timeout ]; then
    S=$(mktemp -d "${TMPDIR:-/tmp}/putback-selftest.XXXXXX")
    T="$S/state"; EVIDENCE="$S/evidence"
    mkdir -p "$T/m/etc/auros-e2e" "$T/m/var/lib/auros-e2e" "$T/m/boot/grub" "$S/bin"
    python3 - "$T/m/var/lib/auros-e2e/log" <<'GUEST_PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_text("x" * (1048576 + 17) + "\nguest metadata\n")
GUEST_PY
    printf 'answer metadata\n' > "$T/m/var/lib/auros-e2e/answer.log.1"
    printf 'result metadata\n' > "$T/m/var/lib/auros-e2e/result.1"
    cat > "$S/bin/qemu-system-x86_64" <<'QEMU_PY'
#!/usr/bin/env python3
import json, socket, sys, time
serial = sys.argv[sys.argv.index("-serial") + 1].removeprefix("file:")
qmp = sys.argv[sys.argv.index("-qmp") + 1].removeprefix("unix:").split(",", 1)[0]
with open(serial, "wb") as out:
    out.write(b"aurstage-report v1 verdict=restore\n" + b"s" * (1048576 + 17))
s = socket.socket(socket.AF_UNIX); s.bind(qmp); s.listen(1)
c, _ = s.accept(); f = c.makefile("rwb", buffering=0)
f.write(b'{"QMP":{"version":{}}}\n')
for _ in range(2):
    command = json.loads(f.readline())
    if command.get("execute") == "screendump":
        with open(command["arguments"]["filename"], "wb") as out:
            out.write(b"P6\n1 1\n255\n\0\0\0")
    f.write(b'{"return":{}}\n')
time.sleep(30)
QEMU_PY
    cat > "$S/bin/grub-editenv" <<'GRUB_SH'
#!/bin/sh
printf 'next_entry=put-windows-back\n'
GRUB_SH
    chmod 755 "$S/bin/qemu-system-x86_64" "$S/bin/grub-editenv"
    PATH="$S/bin:$PATH"
    mount_root() { LP=mock; return 0; }
    umount_root() { LP=""; }
    BOOT_TIMEOUT=1; POLL_INTERVAL=1; BOOTN=0; QEXTRA=""
    CODE=mock-code; NV=mock-vars; DISK=mock-disk; WIN=mock-windows; QP=""
    started=$(date +%s)
    power_on none; rc=$?
    elapsed=$(( $(date +%s) - started ))
    bad=0
    [ "$rc" -eq 124 ] || { echo "timeout selftest: expected 124, got $rc"; bad=1; }
    [ "$elapsed" -le 5 ] || { echo "timeout selftest: deadline took ${elapsed}s"; bad=1; }
    [ "$QP" = "" ] || { echo "timeout selftest: QEMU was not reaped"; bad=1; }
    [ "$(wc -c < "$EVIDENCE/serial-1.log")" -eq 1048576 ] \
        || { echo "timeout selftest: serial evidence was not capped"; bad=1; }
    [ "$(wc -c < "$EVIDENCE/guest-1.log")" -eq 1048576 ] \
        || { echo "timeout selftest: guest evidence was not capped"; bad=1; }
    grep -q 'next_entry=put-windows-back' "$EVIDENCE/grubenv-1.txt" \
        || { echo "timeout selftest: grubenv evidence missing"; bad=1; }
    grep -q 'answer metadata' "$EVIDENCE/answer.log.1" \
        || { echo "timeout selftest: answer evidence missing"; bad=1; }
    [ "$(head -c 2 "$EVIDENCE/framebuffer-1.ppm")" = P6 ] \
        || { echo "timeout selftest: framebuffer evidence missing"; bad=1; }
    rm -rf "$S"
    [ "$bad" -eq 0 ] || exit 1
    echo "timeout selftest: fail-fast evidence ok"
    exit 0
fi
e2e_log() { mount_root && cat "$T/m/var/lib/auros-e2e/log" 2>/dev/null; umount_root; }
grubenv_list() { mount_root && grub-editenv "$T/m/boot/grub/grubenv" list 2>/dev/null; umount_root; }

# ── boot 1: AurOS, and the button ───────────────────────────────────
echo "  boot 1: AurOS, where she presses Put Windows back"
power_on putback || exit $?
L=$(e2e_log)
case "$L" in *"1 pressed putback"*) ok "the desktop came up and the button was pressed" ;;
    *) bad "the desktop came up and the button was pressed" "$(echo "$L" | tail -4)" ;; esac
mount_root; R=$(cat "$T/m/var/lib/auros-e2e/result.1" 2>/dev/null)
AL=$(tail -5 "$T/m/var/lib/auros-e2e/answer.log.1" 2>/dev/null); umount_root
case "$R" in *"request=putback"*"result=ok"*) ok "answer.sh answered ok" ;;
    *) bad "answer.sh answered ok" "$(echo "$R" | tr '\n' ' ')" "$AL" ;; esac
case "$L" in *"NO RESTART"*) bad "...and restarted the computer itself" ;;
    *) ok "...and restarted the computer itself" ;; esac
GL=$(grubenv_list)
case "$GL" in "next_entry=put-windows-back>put-windows-back-yes
auros_asked="[0-9]*) ok "the menu will take the restore entry next, and knows when" ;;
    *) bad "the menu will take the restore entry next, and knows when" "$GL" ;; esac
[ "$(python3 "$E" "$NV" bootnext)" = "0002" ] \
  && ok "and the firmware will start AurOS's menu, though she declined" \
  || bad "and the firmware will start AurOS's menu, though she declined" \
         "BootNext=$(python3 "$E" "$NV" bootnext)"
[ "$(python3 "$E" "$NV" bootorder | awk '{print $1}')" = "0000" ] \
  && ok "...with BootOrder untouched: Windows is still the default" \
  || bad "...with BootOrder untouched: Windows is still the default" \
         "BootOrder=$(python3 "$E" "$NV" bootorder)"

# ── boot 2: the menu takes the restore by itself ────────────────────
echo
echo "  boot 2: the next start, with nobody touching anything"
mount_root && grub-editenv "$T/m/boot/grub/grubenv" set \
    "aurstage_extra=console=ttyS0,115200n8 aurstage.nolog"; umount_root
power_on none || exit $?
S2=$(tr -d '\r' < "$T/serial.2")
echo "$S2" | grep -aq 'starting Boot0002 "AurOS"' \
  && ok "the firmware started AurOS's entry" \
  || bad "the firmware started AurOS's entry" "$(echo "$S2" | grep -ao 'BdsDxe:.*' | head -3)"
# THE KERNEL'S OWN LINES ARE QUIET NOW ("quiet loglevel=3" on the restore
# line, so that she sees one calm screen), so the command line is not on
# the serial line any more. What is: the staging environment's own, and
# "putting Windows back" is printed only when it was started with
# aurstage.restore. It could only be started by grub through shim, with
# Secure Boot on (read inside AurOS on boot 1, same firmware, same keys).
echo "$S2" | grep -aq 'AurOS staging environment' && echo "$S2" | grep -aq 'putting Windows back' \
  && ok "grub started the staging kernel with aurstage.restore" \
  || bad "grub started the staging kernel with aurstage.restore" \
         "$(echo "$S2" | grep -a 'aurstage' | head -3)"
case "$(e2e_log)" in *"1 up mode=putback sb=1"*) ok "...with Secure Boot on (SecureBoot=1, read inside AurOS)" ;;
    *) bad "...with Secure Boot on (SecureBoot=1, read inside AurOS)" "$(e2e_log | grep '^1 up')" ;; esac
V=$(echo "$S2" | grep -ao 'aurstage-report v1 verdict=restore[a-z-]*' | head -1)
[ "$V" = "aurstage-report v1 verdict=restore-nothing-saved" ] \
  && ok "the restore ran, and found nothing on this test disk to put back" \
  || bad "the restore ran, and found nothing on this test disk to put back" "${V:-no report}" \
         "$(echo "$S2" | grep -a 'aurstage:' | tail -5)"
[ "$(md5sum "$WIN" | cut -d' ' -f1)" = "$WIN_BEFORE" ] \
  && ok "...and changed nothing on that disk" || bad "...and changed nothing on that disk"
GL=$(grubenv_list)
case "$GL" in *next_entry=put*) bad "the menu's one-time choice was used up" "$GL" ;;
    *) ok "the menu's one-time choice was used up" ;; esac

# ── boot 3: an ordinary start ───────────────────────────────────────
echo
echo "  boot 3: the start after that"
mount_root && grub-editenv "$T/m/boot/grub/grubenv" unset aurstage_extra; umount_root
python3 "$E" "$NV" set-next 0002 >/dev/null
power_on none || exit $?
S3=$(tr -d '\r' < "$T/serial.3")
echo "$S3" | grep -aq 'AurOS staging environment' \
  && bad "it does not restore again" "$(echo "$S3" | grep -a 'aurstage' | head -2)" \
  || ok "it does not restore again"
L=$(e2e_log)
case "$L" in *"3 up mode=none"*) ok "...it starts AurOS" ;;
    *) bad "...it starts AurOS" "$(echo "$L" | tail -3)" ;; esac

# ── boot 4 and 5: a request the firmware never acted on ─────────────
echo
echo "  boot 4: she presses it again; the firmware then starts Windows instead"
python3 "$E" "$NV" set-next 0002 >/dev/null
power_on putback || exit $?
mount_root; R=$(cat "$T/m/var/lib/auros-e2e/result.4" 2>/dev/null); umount_root
GL=$(grubenv_list)
case "$R" in *"request=putback"*"result=ok"*) ok "answered ok again" ;;
    *) bad "answered ok again" "$(echo "$R" | tr '\n' ' ')" ;; esac
case "$GL" in *"next_entry=put-windows-back>put-windows-back-yes"*"auros_asked="[0-9]*)
        ok "...and the request says when it was made" ;;
    *) bad "...and the request says when it was made" "$GL" ;; esac
echo "  (boot 4b: Windows starts instead -- nothing of AurOS's runs)"
mount_root && grub-editenv "$T/m/boot/grub/grubenv" set \
    "aurstage_extra=console=ttyS0,115200n8 aurstage.nolog"; umount_root
echo
echo "  boot 5: three weeks later, AurOS is started again"
QEXTRA="-rtc base=$(date -u -d '+21 days' '+%Y-%m-%dT%H:%M:%S')"
python3 "$E" "$NV" set-next 0002 >/dev/null
power_on none || exit $?
QEXTRA=""
S5=$(tr -d '\r' < "$T/serial.5")
echo "$S5" | grep -aq 'AurOS staging environment' \
  && ok "the menu still took the old request to the restore" \
  || bad "the menu still took the old request to the restore" \
         "$(echo "$S5" | grep -a 'aurstage' | head -2)"
V=$(echo "$S5" | grep -ao 'aurstage-report v1 verdict=restore[a-z-]*' | head -1)
[ "$V" = "aurstage-report v1 verdict=restore-stale" ] \
  && ok "...which refused it as stale, and did not run" \
  || bad "...which refused it as stale, and did not run" "${V:-no report}" \
         "$(echo "$S5" | grep -a 'aurstage' | tail -5)"
[ "$(md5sum "$WIN" | cut -d' ' -f1)" = "$WIN_BEFORE" ] \
  && ok "...and changed nothing on Windows' disk" || bad "...and changed nothing on Windows' disk"
GL=$(grubenv_list)
case "$GL" in *next_entry=put*|*auros_asked=[0-9]*) bad "...and the request is used up" "$GL" ;;
    *) ok "...and the request is used up" ;; esac
echo
echo "  boot 6: the start after that"
python3 "$E" "$NV" set-next 0002 >/dev/null
power_on none || exit $?
S6=$(tr -d '\r' < "$T/serial.6")
echo "$S6" | grep -aq 'AurOS staging environment' \
  && bad "it does not go to the restore" "$(echo "$S6" | grep -a 'aurstage' | head -2)" \
  || ok "it does not go to the restore"
L=$(e2e_log)
case "$L" in *"6 up mode=none"*) ok "...it starts AurOS" ;;
    *) bad "...it starts AurOS" "$(echo "$L" | tail -3)" ;; esac

# For whoever is working out why it failed: the serial lines, kept.
if [ -n "${PUTBACK_KEEP:-}" ]; then mkdir -p "$PUTBACK_KEEP"; cp "$T"/serial.* "$PUTBACK_KEEP"/ 2>/dev/null; fi
echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: the button in AurOS restarts into the restore, once,"
echo "through AurOS's own menu, with Secure Boot on; and a request three"
echo "weeks old is refused, with nothing changed."
exit 0
