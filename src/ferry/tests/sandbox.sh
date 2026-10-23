#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  "Bring my files across", inside the sandbox it really runs in.
#
#  answer.sh starts Ferry from auros-answer.service, whose systemd
#  sandbox (ProtectSystem=strict, ProtectHome, ...) decides what Ferry
#  may touch. Every other Ferry test runs outside it, which is how an
#  import that could not write its own state, nor see /home, shipped
#  looking green. This runs `ferry run` under EVERY [Service] property
#  of that unit file (all but how it is started), as root, and checks
#  her files arrive, owned by her, from:
#
#    1. a Windows tree that is already mounted (FERRY_ROOT), and
#    2. a real NTFS volume on a loop device, which Ferry mounts itself
#       read-only -- with OneDrive files as NTFS really stores them
#       (attributes and reparse tags written by ntfs-3g) -- and which
#       must be byte-for-byte unchanged afterwards.
#
#  Needs root, systemd as PID 1, ntfs-3g, losetup. tests/insystemd.sh
#  runs it in a throwaway container that has them.
# ═══════════════════════════════════════════════════════════════════
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FERRY=$(dirname "$HERE")
REPO=$(cd "$FERRY/../.." && pwd)
UNIT=${UNIT:-}
[ -n "$UNIT" ] || UNIT=$REPO/rootfs/etc/systemd/system/auros-answer.service
export NO_COLOR=1

pass=0; fail=0
ok()  { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad() { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; }
eq()  { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2', want '$3')"; fi; }

[ "$(id -u)" = 0 ] || { echo "sandbox.sh: needs root"; exit 2; }
[ -d /run/systemd/system ] || { echo "sandbox.sh: needs systemd as PID 1"; exit 2; }

# Ferry where the image puts it.
sh "$FERRY/install.sh" scripts / >/dev/null && sh "$FERRY/install.sh" helpers / >/dev/null ||
    { echo "sandbox.sh: could not install Ferry"; exit 2; }

# What the unit expects to exist on a running AurOS (ReadWritePaths
# without a "-" must exist, or the unit fails before it starts).
mkdir -p /var/lib/auros /run/auros /var/log

# Her, as first boot makes her.
id ferryher >/dev/null 2>&1 || useradd -m -u 4242 ferryher
HR=$(getent passwd ferryher | cut -d: -f6)

# The unit's sandbox, property by property, so a change to the unit is
# a change to this test.
unit_lines() {
    sed -n '/^\[Service\]/,/^\[/p' "$UNIT" | while IFS= read -r _p; do
        case "$_p" in
            ''|'#'*|'['*|Type=*|ExecStart=*|TimeoutStartSec=*|SuccessExitStatus=*) continue ;;
        esac
        printf '%s\n' "$_p"
    done
}
set --
while IFS= read -r _p; do set -- "$@" -p "$_p"; done <<EOF
$(unit_lines)
EOF
echo "sandbox: $(($# / 2)) properties from $(basename "$UNIT")"

in_sandbox() {   # ENV... -- run `ferry run --auto` in the unit's sandbox
    _env=""
    while [ "$1" != -- ]; do _env="$_env -p Environment=$1"; shift; done
    shift
    # shellcheck disable=SC2086
    systemd-run --quiet --wait --pipe --collect "$@" $_env \
        -p Environment=FERRY_OWNER=ferryher /usr/bin/ferry run --auto
}

echo "1. an already-mounted Windows tree:"
rm -rf /srv/fakewin /var/lib/ferry "$HR/Desktop" "$HR/Documents"
sh "$HERE/mkfakewin.sh" /srv/fakewin >/dev/null
in_sandbox FERRY_ROOT=/srv/fakewin -- "$@" >/tmp/sandbox1.log 2>&1
rc=$?
[ -f /var/lib/ferry/report.d/files-copy.tsv ] || rc="$rc, no records"
case "$rc" in 0|2) ok "ferry run finishes inside the sandbox (exit $rc)" ;;
    *) bad "ferry run failed inside the sandbox (exit $rc)"; tail -5 /tmp/sandbox1.log ;; esac
eq "her Desktop file arrived" "$(cat "$HR/Desktop/report.docx" 2>/dev/null)" report
[ -f "$HR/Desktop/report.docx" ] || { echo "  --- what Ferry said:"; grep -v "^ \{4,\}\|^  *•\|^\$" /tmp/sandbox1.log | head -40 | sed 's/^/  | /'; }
eq "...and it is hers" "$(stat -c %U "$HR/Desktop/report.docx" 2>/dev/null)" ferryher
[ -s /var/lib/ferry/manifest.tsv ] && ok "Ferry's state was written" || bad "Ferry's state was written"

echo "2. a real NTFS volume, mounted by Ferry:"
IMG=/srv/windows.img MP=/srv/ntfs-rw
rm -rf "$IMG" "$MP" /var/lib/ferry "$HR/Desktop" "$HR/Documents" "$HR/Pictures"
truncate -s 256M "$IMG"
LOOP=$(losetup -f --show "$IMG") || { bad "no loop device"; LOOP=""; }
# Only ever our own loop device, whatever happens (it is the host's).
[ -n "$LOOP" ] && trap 'umount "$MP" 2>/dev/null; losetup -d "$LOOP" 2>/dev/null' EXIT INT TERM
if [ -n "$LOOP" ]; then
    mkntfs -q -F -f -L Windows "$LOOP" >/dev/null 2>&1
    mkdir -p "$MP"
    if ntfs-3g "$LOOP" "$MP"; then
        # The fake tree without its multi-gigabyte sparse stubs, then
        # OneDrive files the way NTFS keeps them.
        rm -f /srv/fakewin/users/Bob/OneDrive/Documents/vacation-video.mp4
        cp -r /srv/fakewin/. "$MP"/
        OD="$MP/users/Bob/OneDrive/Documents"
        python3 - "$OD" <<'PY'
import os, sys
od = sys.argv[1]
OFFLINE, ARCH = 0x1000, 0x20
cloud = (0x9000001A).to_bytes(4, "little") + (8).to_bytes(2, "little") + b"\0\0" + b"\1" * 8
# online-only: OFFLINE, a cloud tag, and a size with nothing behind it
for rel in ("Taxes/2022.pdf", "scan.pdf"):
    p = os.path.join(od, rel)
    os.truncate(p, 0)
    os.setxattr(p, "system.ntfs_reparse_data", cloud)
    os.setxattr(p, "system.ntfs_attrib", (OFFLINE | ARCH | 0x400).to_bytes(4, "little"))
# on this computer: the cloud tag, real bytes, no OFFLINE
p = os.path.join(od, "budget.xlsx")
os.setxattr(p, "system.ntfs_reparse_data", cloud)
PY
        umount "$MP"
        SUM0=$(sha256sum "$IMG" | cut -d' ' -f1)
        in_sandbox FERRY_DEV="$LOOP" -- "$@" >/tmp/sandbox2.log 2>&1
        rc=$?
        [ -f /var/lib/ferry/report.d/files-copy.tsv ] || rc="$rc, no records"
        case "$rc" in 0|2) ok "ferry run mounts and imports inside the sandbox (exit $rc)" ;;
            *) bad "ferry run failed on the NTFS volume (exit $rc)"; tail -8 /tmp/sandbox2.log ;; esac
        echo "  ($(grep -o 'read-only at [^ ]* ([a-z0-9-]*)' /tmp/sandbox2.log | head -1))"
        eq "her Desktop file arrived from NTFS" "$(cat "$HR/Desktop/report.docx" 2>/dev/null)" report
        [ -f "$HR/Desktop/report.docx" ] || { echo "  --- what Ferry said:"; grep -v "^ \{4,\}\|^  *•\|^\$" /tmp/sandbox2.log | head -40 | sed 's/^/  | /'; }
        eq "a OneDrive file that is on the disk came across" \
           "$(cat "$HR/Documents/budget.xlsx" 2>/dev/null)" "BUDGET-real-bytes"
        if [ -e "$HR/Documents/Taxes/2022.pdf" ]; then
            bad "an online-only OneDrive file was NOT copied"
        else ok "an online-only OneDrive file was NOT copied"; fi
        grep -q 'Taxes/2022.pdf' /var/lib/ferry/report.d/files-copy.tsv 2>/dev/null &&
            ok "...and it is listed by name" || bad "...and it is listed by name"
        eq "the Windows volume is byte-for-byte unchanged" "$(sha256sum "$IMG" | cut -d' ' -f1)" "$SUM0"
        grep -q "$LOOP" /proc/mounts && bad "the volume was left mounted" || ok "the volume is not left mounted"
    else
        bad "ntfs-3g could not mount the test volume"
    fi
    losetup -d "$LOOP"
fi
rm -f "$IMG"

# ═══ 3. Put Windows back, from the button to the files in Windows ═════
#  answer.sh's `putback` request, in the unit's sandbox, on a real NTFS
#  volume: the photo she saved in AurOS must be on Windows afterwards,
#  and the restart must be armed only if it is. The restore itself (the
#  partition table) is installtest's and putbacktest's; this is the
#  step that decides whether her files are still alive when it runs.
echo "3. Put Windows back (the button, in the sandbox, on a real NTFS volume):"
mkdir -p /usr/lib/auros /srv/stubbin /srv/esp/EFI/AurOS /var/lib/auros
cp "$REPO/rootfs/usr/lib/auros/answer.sh" /usr/lib/auros/answer.sh
# What is not under test: the request itself, the menu arming, the restart.
printf '#!/bin/sh\ncase "$1" in request) echo putback ;; *) exit 0 ;; esac\n' > /srv/stubbin/aurfirst
printf '#!/bin/sh\ntouch /var/lib/auros/rebooted\n' > /srv/stubbin/reboot
chmod +x /srv/stubbin/aurfirst /srv/stubbin/reboot
: > /srv/esp/EFI/AurOS/staging.efi; : > /srv/esp/EFI/AurOS/staging.img
echo 1700000000 > /srv/rtc

make_windows() { # LOOP [hibernated] -> a fresh NTFS volume holding the fake Windows tree
    mkntfs -q -F -f -L Windows "$1" >/dev/null 2>&1 || return 1
    mkdir -p "$MP"; ntfs-3g "$1" "$MP" || return 1
    rm -f /srv/fakewin/users/Bob/OneDrive/Documents/vacation-video.mp4
    cp -r /srv/fakewin/. "$MP"/
    mkdir -p "$MP/users/Bob/From AurOS/Documents"
    echo "windows version of the letter" > "$MP/users/Bob/From AurOS/Documents/letter.txt"
    [ "${2:-}" != hibernated ] || printf 'HIBR' | dd of="$MP/hiberfil.sys" bs=4 count=1 conv=notrunc 2>/dev/null
    umount "$MP"
}
outside_hash() { # LOOP -> hashes of everything in Windows except "From AurOS"
    ntfs-3g -o ro "$1" "$MP" || return 1
    ( cd "$MP" && find . -type f -not -path './users/Bob/From AurOS/*' -print0 | sort -z | xargs -0 sha256sum )
    umount "$MP"
}
run_answer() { # LOOP [ENV=VALUE...]: the putback request, under the unit's sandbox
    _lp=$1; shift
    _extra=$(printf '%s\n' "$@")
    rm -f /run/auros-answer/result /run/auros-answer/ferry.done /var/lib/auros/grubenv /var/lib/auros/rebooted
    set --
    while IFS= read -r _p; do [ -n "$_p" ] && set -- "$@" -p "$_p"; done <<EOP
$(unit_lines)
EOP
    while IFS= read -r _v; do [ -n "$_v" ] && set -- "$@" -p "Environment=$_v"; done <<EOP
$_extra
EOP
    systemd-run --quiet --wait --pipe --collect "$@" \
        -p Environment=AUROS_BIN=/srv/stubbin -p Environment=AUROS_STATE=/var/lib/auros \
        -p Environment=AUROS_LOG=/var/log/answer-sandbox.log \
        -p Environment=AUROS_GRUBENV=/var/lib/auros/grubenv \
        -p Environment=AUROS_REBOOT=/srv/stubbin/reboot \
        -p Environment=AUROS_ESP_ROOTS=/srv/esp -p Environment=AUROS_RTC=/srv/rtc \
        -p Environment=AUROS_REBOOT_DELAY=0 -p Environment=FERRY_OWNER=ferryher \
        -p Environment=FERRY_DEV="$_lp" /usr/lib/auros/answer.sh >/srv/answer-run.log 2>&1
    cat /run/auros-answer/result 2>/dev/null
}
armed() { [ -e /var/lib/auros/rebooted ] || [ -e /var/lib/auros/grubenv ]; }

# Her AurOS home: a photo she took and a letter she wrote since she began.
rm -rf "$HR/Pictures" "$HR/Documents" "$HR/Desktop"
mkdir -p "$HR/Pictures/2024" "$HR/Documents"
head -c 3000000 /dev/urandom > "$HR/Pictures/2024/holiday.jpg"
echo "my new letter" > "$HR/Documents/letter.txt"
echo "colon in a name" > "$HR/Documents/a:b.txt"
chown -R ferryher: "$HR/Pictures" "$HR/Documents"
PHOTO=$(sha256sum "$HR/Pictures/2024/holiday.jpg" | cut -d' ' -f1)

IMG=/srv/windows3.img
rm -f "$IMG"; truncate -s 256M "$IMG"
LOOP=$(losetup -f --show "$IMG") || { bad "no loop device"; LOOP=""; }
[ -n "$LOOP" ] && trap 'umount "$MP" 2>/dev/null; losetup -d "$LOOP" 2>/dev/null' EXIT INT TERM
if [ -n "$LOOP" ] && make_windows "$LOOP"; then
    outside_hash "$LOOP" > /srv/outside.before
    R=$(run_answer "$LOOP")
    if printf '%s\n' "$R" | grep -qx 'result=ok'; then ok "Put Windows back answers ok"
    else
        bad "Put Windows back answers ok"; printf '%s\n' "$R" | sed 's/^/  | /'
        tail -8 /srv/answer-run.log | sed 's/^/  | /'; tail -12 /var/log/answer-sandbox.log 2>/dev/null | sed 's/^/  | /'; tail -5 /run/auros-answer/putback.log 2>/dev/null | sed 's/^/  | /'
    fi
    [ -e /var/lib/auros/rebooted ] && ok "...and the restart was armed" || bad "...and the restart was armed"
    ntfs-3g -o ro "$LOOP" "$MP"
    eq "THE PHOTO SHE SAVED IN AUROS IS ON WINDOWS" \
       "$(sha256sum "$MP/users/Bob/From AurOS/Pictures/2024/holiday.jpg" 2>/dev/null | cut -d' ' -f1)" "$PHOTO"
    eq "...Windows' own copy of the letter was not overwritten" \
       "$(cat "$MP/users/Bob/From AurOS/Documents/letter.txt" 2>/dev/null)" "windows version of the letter"
    eq "...hers is beside it" "$(cat "$MP/users/Bob/From AurOS/Documents/letter (from AurOS).txt" 2>/dev/null)" "my new letter"
    eq "...a name Windows cannot hold was changed, not lost" "$(cat "$MP/users/Bob/From AurOS/Documents/a_b.txt" 2>/dev/null)" "colon in a name"
    [ -s "$MP/users/Bob/From AurOS/Read me.txt" ] && ok "...and a note for her is there" || bad "...and a note for her is there"
    umount "$MP"
    eq "nothing else on the Windows volume changed" "$(outside_hash "$LOOP" | md5sum)" "$(md5sum < /srv/outside.before)"
    if grep -qx 'kind=back' /run/auros-answer/ferry.done && grep -qx 'result=ok' /run/auros-answer/ferry.done; then
        ok "the completion signal the desktop waits for says: back, ok"; else bad "the completion signal the desktop waits for says: back, ok"; fi
    if tail -1 /run/auros-answer/putback.log | grep -q 'Your files are saved in Windows'; then
        ok "the screen's last line says her files are saved"; else bad "the screen's last line says her files are saved"; fi
    if grep -q "$LOOP" /proc/mounts; then bad "the volume is not left mounted"; else ok "the volume is not left mounted"; fi

    # Not enough room in Windows: the restart is NOT armed.
    outside_hash "$LOOP" > /srv/outside.before
    R=$(run_answer "$LOOP" FERRY_ROOM_BYTES=1000000)
    if printf '%s\n' "$R" | grep -qx 'result=failed' && printf '%s\n' "$R" | grep -qx 'code=4'; then
        ok "no room in Windows: the answer is failed (4)"; else bad "no room in Windows: the answer is failed (4)"; printf '%s\n' "$R" | sed 's/^/  | /'; fi
    if armed; then bad "...and Windows was NOT put back (nothing armed)"; else ok "...and Windows was NOT put back (nothing armed)"; fi
else
    bad "could not build the NTFS volume"
fi
if [ -n "$LOOP" ]; then
    # A Windows that was not shut down properly (Fast Startup): refused before it is touched.
    make_windows "$LOOP" hibernated
    outside_hash "$LOOP" > /srv/outside.before
    R=$(run_answer "$LOOP")
    if printf '%s\n' "$R" | grep -qx 'code=3'; then ok "a hibernated Windows: the answer is failed (3)"
    else bad "a hibernated Windows: the answer is failed (3)"; printf '%s\n' "$R" | sed 's/^/  | /'; fi
    if armed; then bad "...and Windows was NOT put back"; else ok "...and Windows was NOT put back"; fi
    ntfs-3g -o ro "$LOOP" "$MP" 2>/dev/null
    if [ -d "$MP/users/Bob/From AurOS/Pictures" ]; then bad "...and nothing was written to it"; else ok "...and nothing was written to it"; fi
    umount "$MP" 2>/dev/null
    losetup -d "$LOOP"; LOOP=""
fi
rm -f "$IMG"

echo
printf 'sandbox tests: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
