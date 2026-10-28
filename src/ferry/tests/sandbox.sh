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
    # "probe:/dev/loopN" lets Ferry look for the Windows drive itself, but
    # only among those devices (the container sees the host's disks);
    # anything else names the Windows drive.
    case "$_lp" in
        probe:*) _dev="FERRY_PROBE_DEVS=${_lp#probe:}" ;;
        *)       _dev="FERRY_DEV=$_lp" ;;
    esac
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
        -p "Environment=$_dev" /usr/lib/auros/answer.sh >/srv/answer-run.log 2>&1
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
    if tail -1 /run/auros-answer/putback.log | grep -qE 'Your (file is|[0-9]+ files are) saved in Windows'; then
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

# ── no Windows drive at all / a Windows drive that will not open ─────
#  tools/putbacktest.sh's machine has a "Windows" disk with only an EFI
#  partition, and an AurOS home with nothing in it. With nothing to
#  save, the Windows drive is not even looked at: Put Windows back goes
#  ahead (a BitLocker PC with an empty home too). With files to save and
#  nowhere to save them, it STOPS and nothing is deleted.
echo "4. no Windows drive, and a Windows drive that will not open:"
IMG=/srv/blank.img
rm -f "$IMG"; truncate -s 32M "$IMG"
LOOP=$(losetup -f --show "$IMG") || { bad "no loop device"; LOOP=""; }
[ -n "$LOOP" ] && trap 'umount "$MP" 2>/dev/null; losetup -d "$LOOP" 2>/dev/null' EXIT INT TERM
if [ -n "$LOOP" ]; then
    mkfs.vfat -F 32 "$LOOP" >/dev/null 2>&1 || true            # an EFI-like partition and nothing else
    # (a) nothing in AurOS to save
    # (the earlier scenarios filled her folders: empty all six)
    for _f in Desktop Documents Downloads Music Pictures Videos; do
        rm -rf "$HR/$_f"; mkdir -p "$HR/$_f"; chown ferryher: "$HR/$_f"
    done
    R=$(run_answer "probe:$LOOP")
    if printf '%s\n' "$R" | grep -qx 'result=ok'; then ok "nothing to save and no Windows drive: Put Windows back goes ahead"
    else bad "nothing to save and no Windows drive: Put Windows back goes ahead"; printf '%s\n' "$R" | sed 's/^/  | /'
         tail -6 /run/auros-answer/putback.log 2>/dev/null | sed 's/^/  | /'; fi
    if [ -e /var/lib/auros/rebooted ]; then ok "...and the restart was armed"; else bad "...and the restart was armed"; fi
    printf '%s\n' "$R" | grep -qx 'saved=0' && ok "...and the answer says nothing was saved because there was nothing (saved=0)" ||
        bad "...and the answer says saved=0"
    tail -1 /run/auros-answer/putback.log | grep -q 'no files to save' && ok "...and the screen's last line says she had no files" ||
        bad "...and the screen's last line says she had no files"

    # (b) files to save, and no Windows drive anywhere
    echo "a photo she took" > "$HR/Pictures/new.jpg"; chown ferryher: "$HR/Pictures/new.jpg"
    R=$(run_answer "probe:$LOOP")
    if printf '%s\n' "$R" | grep -qx 'code=7'; then ok "files to save and no Windows drive: stops (failed, code 7)"
    else bad "files to save and no Windows drive: stops (failed, code 7)"; printf '%s\n' "$R" | sed 's/^/  | /'; fi
    case "$(printf '%s\n' "$R" | sed -n 's/^note=//p')" in
        *"Nothing of yours was deleted"*) ok "...and it says nothing of hers was deleted" ;; *) bad "...and it says nothing of hers was deleted" ;; esac
    if armed; then bad "...and Windows was NOT put back"; else ok "...and Windows was NOT put back"; fi
    [ -f "$HR/Pictures/new.jpg" ] && ok "...and her photo is where it was" || bad "...and her photo is where it was"

    # (c) a drive that is there and will not open. blkid is told, for this
    # device only, that it holds BitLocker (a hand-made BitLocker header is
    # not one libblkid accepts, and a damaged NTFS opens through its mirror
    # MFT): everything else is the real thing -- the real probe, the real
    # failed mount.
    echo "$LOOP" > /srv/fake-bitlocker-dev
    cat > /usr/local/bin/blkid <<'EOB'
#!/bin/sh
# test stand-in: reports BitLocker for the one device named in /srv/fake-bitlocker-dev
if [ -s /srv/fake-bitlocker-dev ] && [ "$*" = "-o value -s TYPE $(cat /srv/fake-bitlocker-dev)" ]; then echo BitLocker; exit 0; fi
PATH=/usr/sbin:/usr/bin:/sbin:/bin exec blkid "$@"
EOB
    chmod +x /usr/local/bin/blkid
    R=$(run_answer "probe:$LOOP")
    if printf '%s\n' "$R" | grep -qx 'code=5'; then ok "files to save and a Windows drive that will not open: stops (failed, code 5)"
    else bad "files to save and a Windows drive that will not open: stops (failed, code 5)"; printf '%s\n' "$R" | sed 's/^/  | /'; fi
    if armed; then bad "...and Windows was NOT put back"; else ok "...and Windows was NOT put back"; fi

    # (d) the same locked drive, nothing in AurOS to save: it is not even looked at
    rm -rf "$HR/Pictures"; mkdir -p "$HR/Pictures"; chown ferryher: "$HR/Pictures"
    R=$(run_answer "probe:$LOOP")
    if printf '%s\n' "$R" | grep -qx 'result=ok'; then ok "a locked Windows drive and nothing to save: Put Windows back goes ahead"
    else bad "a locked Windows drive and nothing to save: Put Windows back goes ahead"; printf '%s\n' "$R" | sed 's/^/  | /'; fi
    rm -f /usr/local/bin/blkid /srv/fake-bitlocker-dev
    losetup -d "$LOOP"; LOOP=""
fi
rm -f "$IMG"

# ── 5. the handover volume and a USB stick, for real ─────────────────
#  A2's put-back step makes an exFAT volume (label AUROSHANDOV), attaches
#  it as a loop device and exports FERRY_HANDOVER_DEV; `ferry back` then
#  writes her files onto it, reads them back from a fresh read-only mount,
#  and only then writes DONE. It must leave the volume UNMOUNTED (the
#  caller detaches it). Checked with the same reference verifier the
#  Windows side's checks are tested against.
echo "5. the handover volume (exFAT) and a USB stick:"
refill_home() {
    for _f in Desktop Documents Downloads Music Pictures Videos; do rm -rf "$HR/$_f"; mkdir -p "$HR/$_f"; done
    mkdir -p "$HR/Pictures/2024"
    head -c 2500000 /dev/urandom > "$HR/Pictures/2024/holiday.jpg"
    echo "my new letter" > "$HR/Documents/letter.txt"; echo "colon" > "$HR/Documents/a:b.txt"
    chown -R ferryher: "$HR"
}
refill_home
PHOTO=$(sha256sum "$HR/Pictures/2024/holiday.jpg" | cut -d' ' -f1)
IMG=/srv/handover.img
rm -f "$IMG"; truncate -s 64M "$IMG"
LOOP=$(losetup -f --show "$IMG") || { bad "no loop device"; LOOP=""; }
[ -n "$LOOP" ] && trap 'umount "$MP" 2>/dev/null; losetup -d "$LOOP" 2>/dev/null' EXIT INT TERM
if [ -n "$LOOP" ] && mkfs.exfat -L AUROSHANDOV "$LOOP" >/dev/null 2>&1; then
    R=$(run_answer "probe:/dev/null" FERRY_HANDOVER_DEV="$LOOP")
    if printf '%s\n' "$R" | grep -qx 'result=ok'; then ok "Put Windows back with a handover volume: answers ok"
    else bad "Put Windows back with a handover volume: answers ok"; printf '%s\n' "$R" | sed 's/^/  | /'
         tail -6 /run/auros-answer/putback.log | sed 's/^/  | /'; tail -12 /var/lib/ferry/ferry.log | sed 's/^/  | ferry.log: /'; fi
    if [ -e /var/lib/auros/rebooted ]; then ok "...and the restart was armed"; else bad "...and the restart was armed"; fi
    if grep -q "$LOOP" /proc/mounts; then bad "...and the volume is left UNMOUNTED (A2 detaches it next)"; else ok "...and the volume is left UNMOUNTED (A2 detaches it next)"; fi
    mkdir -p "$MP"; mount -t exfat -o ro "$LOOP" "$MP" 2>/dev/null || bad "could not mount the volume to look"
    V=$(sh "$REPO/src/ferry/tests/verify-handover.sh" "$MP" 2>&1); case "$V" in "ok "*) ok "...the Windows side's reference checks pass: $V" ;; *) bad "...the Windows side's reference checks pass"; printf '%s\n' "$V" | sed 's/^/  | /' ;; esac
    eq "...THE PHOTO IS ON THE VOLUME, byte for byte" \
       "$(sha256sum "$MP/AurOS-handover/files/Pictures/2024/holiday.jpg" 2>/dev/null | cut -d' ' -f1)" "$PHOTO"
    eq "...with the hash the manifest promised" \
       "$(awk -F'\t' '$2 == "2024/holiday.jpg" {print $4}' "$MP/AurOS-handover/manifest.tsv")" "$PHOTO"
    eq "...a name Windows cannot hold arrived already legal" "$(cat "$MP/AurOS-handover/files/Documents/a_b.txt" 2>/dev/null)" colon
    umount "$MP"
    tail -1 /run/auros-answer/putback.log | grep -qE 'saved and checked, ready for Windows' &&
        ok "...and the screen's last line says her files are saved and checked, ready for Windows" ||
        bad "...and the screen's last line says her files are saved and checked, ready for Windows"
    grep -qx 'saved=2' /run/auros-answer/result 2>/dev/null || printf '%s\n' "$R" | grep -q '^saved=[1-9]' &&
        ok "...and the answer says how many were saved" || bad "...and the answer says how many were saved"

    # A volume that is too small: stops, nothing armed, not complete.
    losetup -d "$LOOP"; truncate -s 12M "$IMG"; LOOP=$(losetup -f --show "$IMG")
    mkfs.exfat -L AUROSHANDOV "$LOOP" >/dev/null 2>&1
    head -c 30000000 /dev/urandom > "$HR/Pictures/2024/big.mp4"; chown ferryher: "$HR/Pictures/2024/big.mp4"
    R=$(run_answer "probe:/dev/null" FERRY_HANDOVER_DEV="$LOOP")
    if printf '%s\n' "$R" | grep -qx 'code=4'; then ok "a handover volume that is too small: stops (failed, code 4)"
    else bad "a handover volume that is too small: stops (failed, code 4)"; printf '%s\n' "$R" | sed 's/^/  | /'; fi
    if armed; then bad "...and Windows was NOT put back"; else ok "...and Windows was NOT put back"; fi
    mount -t exfat -o ro "$LOOP" "$MP" 2>/dev/null
    if [ -e "$MP/AurOS-handover/DONE" ]; then bad "...and the volume is not marked complete"; else ok "...and the volume is not marked complete"; fi
    umount "$MP"
    if grep -q "$LOOP" /proc/mounts; then bad "...and the volume is left unmounted"; else ok "...and the volume is left unmounted"; fi

    # The USB stick: a FAT stick, as she would buy one.
    losetup -d "$LOOP"; rm -f "$HR/Pictures/2024/big.mp4"; truncate -s 64M "$IMG"; LOOP=$(losetup -f --show "$IMG")
    mkfs.vfat -F 32 -n HOLIDAY "$LOOP" >/dev/null 2>&1
    FERRY_STATE=/var/lib/ferry-stick FERRY_OWNER=ferryher FERRY_HOME="$HR" FERRY_SIGNAL=/tmp/stick.done \
        PATH="$REPO/src/ferry:$REPO/src/ferry/build:$PATH" ferry stick "$LOOP" --plain > /tmp/stick.out 2>&1
    eq "ferry stick onto a real FAT stick: exit 0" "$?" 0
    mount -t vfat -o ro "$LOOP" "$MP" 2>/dev/null
    eq "...the photo is on the stick, byte for byte" \
       "$(sha256sum "$MP/From AurOS/Pictures/2024/holiday.jpg" 2>/dev/null | cut -d' ' -f1)" "$PHOTO"
    [ -f "$MP/From AurOS/Read me.txt" ] && [ -f "$MP/From AurOS/MANIFEST.tsv" ] && ok "...with a note and a manifest" || bad "...with a note and a manifest"
    umount "$MP"
    tail -1 /tmp/stick.out | grep -q 'saved on the USB stick' && ok "...and she is told it is on the USB stick" || bad "...and she is told it is on the USB stick"
    losetup -d "$LOOP"; LOOP=""
else
    bad "could not make the exFAT volume (is exfatprogs here, and the kernel's exfat?)"
fi
rm -f "$IMG"

echo
printf 'sandbox tests: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
