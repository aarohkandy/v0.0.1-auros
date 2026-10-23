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
set --
while IFS= read -r _p; do
    case "$_p" in
        ''|'#'*|'['*|Type=*|ExecStart=*|TimeoutStartSec=*|SuccessExitStatus=*) continue ;;
    esac
    set -- "$@" -p "$_p"
done <<EOF
$(sed -n '/^\[Service\]/,/^\[/p' "$UNIT")
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

echo
printf 'sandbox tests: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
