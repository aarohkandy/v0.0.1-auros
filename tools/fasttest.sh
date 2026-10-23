#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  fasttest — the fast path, end to end, in simulation
#
#  docs/AURBRIDGE.md, "Fast path". Windows shrinks C: itself while the
#  image downloads, AurBridge writes AurOS raw into the room that
#  frees, and after the restart staging only checks and commits. This
#  proves the whole thing against a synthetic machine -- the REAL phase
#  engine (out/aurbridge-sim) on a computer made of files whose
#  "Windows online shrink" is ntfsresize + a table rewrite, then the
#  REAL staging environment booted on the disk it leaves:
#
#    happy path        the gap holds exactly the image's bytes (hashed
#                      independently), staging installs, every file in
#                      Windows is what it was, AurOS starts
#    BitLocker-shaped  C: carries "-FVE-FS-". Without her agreement to
#                      pause its lock the engine REFUSES, disk untouched;
#                      with it, the pause is -RebootCount 2 (never a
#                      decrypt), and staging installs WITHOUT ONE BYTE of
#                      C: changing (hashed before and after) or mounting
#    abort             C: is its original size again, byte for byte, and
#                      the lock is switched back on
#    failures          a damaged piece, a wrong image: C: grows back
#    no room           falls back (no BitLocker) or refuses (BitLocker),
#                      before anything is changed
#
#  Needs root (loop devices), sgdisk, ntfs-3g from the forged rootfs,
#  QEMU, and the fault staging image: the powercut-nostick prerequisites.
#
#    sudo AURSTAGE_FAULT=1 ./build/staging && sudo sh tools/fasttest.sh [scenario ...]
#    scenarios: happy bitlocker abort failures noroom cuts handover stagelog   (default: all)
#    handover also needs exfatprogs and the exfat kernel driver (a2-kvm image).
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
. tools/machine.sh

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-64s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-64s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

IMG=out/auros-staging-fault.img
KERN=out/auros-staging-fault-vmlinuz
[ -f "$IMG" ] && [ -f "$KERN" ] || { echo "run: sudo AURSTAGE_FAULT=1 ./build/staging"; exit 2; }
[ -x out/aurbridge-sim ] || { echo "run ./build/aurbridge first"; exit 2; }
mach_need
LD=$(cd "$(dirname "$LD")" && pwd)/$(basename "$LD")
LPABS=""
for d in $(echo "$LP" | tr ':' ' '); do LPABS="$LPABS:$(cd "$d" 2>/dev/null && pwd)"; done
LPABS=${LPABS#:}
ROOT_DIR=$(pwd)

LOCK="${TMPDIR:-/tmp}/installtest.lock"
mkdir "$LOCK" 2>/dev/null || { echo "another end-to-end test is running"; exit 2; }
MTMP=$(mktemp -d "${TMPDIR:-/tmp}/fasttest.XXXXXX")
_a2_clean() { kill $(jobs -p) 2>/dev/null; mountpoint -q "$MTMP/m" 2>/dev/null && umount "$MTMP/m"
              rm -rf "$MTMP" "$LOCK"; }
trap _a2_clean EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

WANT="${*:-happy bitlocker abort failures noroom cuts handover stagelog}"
want() { case " $WANT " in *" $1 "*) return 0 ;; esac; return 1; }

echo
echo "The fast path, end to end: Windows makes the room, AurOS goes into it,"
echo "and the restart only checks and commits."
echo

mach_disk
mach_image
echo "  a 3 GiB machine, $WINFILES files in Windows, and a $((RLEN/1048576)) MiB AurOS root to install"

# ── the image, in pieces, the way it is published ────────────────────
# Served over HTTP from 127.0.0.1, because the installer only fetches
# from http(s) -- the same download code the real one runs.
PIECES="$MTMP/www/good"; mkdir -p "$PIECES"
gzip -c "$AIMG" | split -b 1048576 -d -a 3 - "$PIECES/auros-desktop.img.gz."
cat > "$MTMP/srv.py" <<'EOPY'
import http.server, socketserver, sys, os
os.chdir(sys.argv[1])
class H(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass
class S(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
srv = S(('127.0.0.1', 0), H)
open(sys.argv[2], 'w').write('%d\n' % srv.server_address[1])
srv.serve_forever()
EOPY
python3 "$MTMP/srv.py" "$MTMP/www" "$MTMP/port" > "$MTMP/srv.log" 2>&1 &
i=0; while [ $i -lt 100 ] && [ ! -s "$MTMP/port" ]; do i=$((i+1)); sleep 0.1; done
PORT=$(cat "$MTMP/port" 2>/dev/null) || { echo "  the test server would not start"; exit 2; }
{
    printf 'base http://127.0.0.1:%s/good/\n' "$PORT"
    for f in "$PIECES"/auros-desktop.img.gz.*; do
        printf 'piece %s %s %s\n' "$(basename "$f")" "$(stat -c%s "$f")" "$(sha256sum "$f" | cut -d' ' -f1)"
    done
} > "$MTMP/pieces.txt"
IMGSHA=$(sha256sum "$AIMG" | cut -d' ' -f1)
IMGBYTES=$(stat -c%s "$AIMG")
echo "  published as $(ls "$PIECES" | wc -l) pieces"

# ── WINDOWS' ONLINE SHRINK, as this simulated machine does it ────────
#
# What Resize-Partition leaves behind is the filesystem resized and the
# partition entry rewritten, and that is what this does, with the same
# tools the staging environment uses: ntfsresize and sgdisk. (A volume
# carrying "-FVE-FS-" has its NTFS signature put back for the length of
# the resize and the lock's signature restored after: Windows shrinks a
# BitLocker volume through its own filter driver, which no simulation
# has; what is proved is what is left on the disk.)
cat > "$MTMP/shrink.sh" <<EOSH
#!/bin/sh
LD='$LD'; LP='$LPABS'; RFS='$ROOT_DIR/$RFS'
nt() { p="\$RFS/usr/sbin/\$1"; [ -x "\$p" ] || p="\$RFS/usr/bin/\$1"; shift
       "\$LD" --library-path "\$LP" "\$p" "\$@"; }
cmd=\$1; DISK=\$2; PART=\$3
info() { sgdisk -i "\$PART" "\$DISK" 2>/dev/null; }
first=\$(info | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
last=\$(info | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
size=\$(( (last - first + 1) * 512 ))
fve_off() { # put NTFS back for the duration, remembering the lock's mark
    dd if="\$DISK" bs=1 skip=\$((first*512+3)) count=8 status=none | grep -q -- '-FVE-FS-' || return 1
    printf 'NTFS    ' | dd of="\$DISK" bs=1 seek=\$((first*512+3)) conv=notrunc status=none; return 0; }
fve_on() { printf '\\055FVE-FS\\055' | dd of="\$DISK" bs=1 seek=\$((first*512+3)) conv=notrunc status=none; }
case "\$cmd" in
limits)
    next=\$(sgdisk -p "\$DISK" 2>/dev/null | sed -n '/^Number/,\$p' | tail -n +2 | awk -v l="\$last" '\$2 > l {print \$2}' | sort -n | head -1)
    [ -n "\$next" ] || next=\$(( \$(sgdisk -p "\$DISK" 2>/dev/null | sed -n 's/^Total free space.*//p;s/.*last usable sector is \([0-9]*\).*/\1/p' | head -1) + 1 ))
    fve=0; fve_off && fve=1
    L=\$(losetup --find --show -o \$((first*512)) --sizelimit \$size "\$DISK") || exit 1
    min=\$(nt ntfsresize --info -f "\$L" 2>&1 | sed -n 's/.*You might resize at \([0-9]*\) bytes.*/\1/p' | head -1)
    losetup -d "\$L"; [ "\$fve" = 1 ] && fve_on
    [ -n "\$min" ] || exit 1
    echo "AURNOW=\$size"; echo "AURMIN=\$min"; echo "AURMAX=\$(( (next - first) * 512 ))"; echo "AUROFF=\$((first*512))"
    exit 0 ;;
resize)
    want=\$4
    uuid=\$(info | sed -n 's/^Partition unique GUID: //p')
    type=\$(info | sed -n 's/^Partition GUID code: \([0-9A-F-]*\).*/\1/p')
    name=\$(info | sed -n "s/^Partition name: '\(.*\)'/\1/p")
    fve=0; fve_off && fve=1
    if [ "\$want" -lt "\$size" ]; then
        L=\$(losetup --find --show -o \$((first*512)) --sizelimit \$size "\$DISK") || exit 1
        echo y | nt ntfsresize -f --size "\$want" "\$L" >/dev/null 2>&1; rc=\$?
        losetup -d "\$L"
        [ \$rc -eq 0 ] || { [ "\$fve" = 1 ] && fve_on; exit 1; }
        newlast=\$(( first + want / 512 - 1 ))
        sgdisk -d "\$PART" -n "\$PART:\$first:\$newlast" -t "\$PART:\$type" -u "\$PART:\$uuid" -c "\$PART:\$name" "\$DISK" >/dev/null 2>&1 || exit 1
    else
        newlast=\$(( first + want / 512 - 1 ))
        sgdisk -d "\$PART" -n "\$PART:\$first:\$newlast" -t "\$PART:\$type" -u "\$PART:\$uuid" -c "\$PART:\$name" "\$DISK" >/dev/null 2>&1 || exit 1
        L=\$(losetup --find --show -o \$((first*512)) --sizelimit \$((newlast*512 - first*512 + 512)) "\$DISK") || exit 1
        echo y | nt ntfsresize -f "\$L" >/dev/null 2>&1; rc=\$?
        losetup -d "\$L"
        [ \$rc -eq 0 ] || { [ "\$fve" = 1 ] && fve_on; exit 1; }
    fi
    [ "\$fve" = 1 ] && fve_on
    exit 0 ;;
esac
exit 2
EOSH
chmod +x "$MTMP/shrink.sh"

# ── a machine to run a scenario on ──────────────────────────────────
# new_machine NAME [fve]: a fresh copy of the disk and a simulated
# computer around it. $S is its directory; its disk is $S/disk.img.
new_machine() {
    S="$MTMP/$1"; mkdir -p "$S/sim/esp" "$S/win"
    cp --sparse=always "$DISK" "$S/disk.img"
    if [ "${2:-}" = fve ]; then
        printf '\055FVE-FS\055' | dd of="$S/disk.img" bs=1 seek=$((P2S*512+3)) conv=notrunc status=none
    fi
    : > "$S/sim/efivars.txt"
    printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$S/disk.img" "$(stat -c%s "$S/disk.img")" > "$S/sim/disks.txt"
    cp "$MTMP/shrink.sh" "$S/sim/shrink.sh"
}
# run_engine [VAR=value ...]: the real phase engine, fast path asked for.
run_engine() {
    ( cd "$ROOT_DIR" && env AURBRIDGE_FAST=1 AURBRIDGE_MIN_ROOT_MB=100 \
        AURBRIDGE_PIECES="$MTMP/pieces.txt" AURBRIDGE_IMAGE_SHA256="$IMGSHA" \
        AURBRIDGE_IMAGE_BYTES="$IMGBYTES" "$@" \
        out/aurbridge-sim "$S/sim" desktop none "$S/win/auros-desktop.img" "$KERN" "$IMG" \
        > "$S/out" 2> "$S/err" )
}
part_bytes() { # image part -> bytes
    _f=$(sgdisk -i "$2" "$1" 2>/dev/null | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
    _l=$(sgdisk -i "$2" "$1" 2>/dev/null | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
    echo $(( (_l - _f + 1) * 512 ))
}
extent_sha() { # image  byte-offset  length
    python3 - "$1" "$2" "$3" <<'EOPY'
import sys, hashlib
f = open(sys.argv[1], 'rb'); f.seek(int(sys.argv[2])); n = int(sys.argv[3]); h = hashlib.sha256()
while n:
    b = f.read(min(n, 1 << 20))
    if not b: break
    h.update(b); n -= len(b)
print(h.hexdigest())
EOPY
}
whole_sha() { sha256sum "$1" | cut -d' ' -f1; }
# every file in Windows, byte for byte, read through whatever the table
# says Windows is now
win_files_now() { # image label
    _s=$(sgdisk -i 2 "$1" 2>/dev/null | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
    _e=$(sgdisk -i 2 "$1" 2>/dev/null | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
    _l=$(losetup --find --show -o $((_s*512)) --sizelimit $(((_e-_s+1)*512)) "$1" 2>/dev/null)
    if [ -n "$_l" ] && nt ntfs-3g -o ro "$_l" "$MTMP/m" >/dev/null 2>&1; then
        ( cd "$MTMP/m" && find . -type f -exec md5sum {} \; | sort ) > "$MTMP/win.now"
        umount "$MTMP/m"
        cmp -s "$MTMP/win.before" "$MTMP/win.now" \
            && ok "$2: every file in Windows is exactly what it was" \
            || bad "$2: every file in Windows is exactly what it was" "$(diff "$MTMP/win.before" "$MTMP/win.now" | head -3)"
    else
        bad "$2: the Windows volume mounts"
    fi
    [ -n "$_l" ] && losetup -d "$_l"
}
# boot staging on a copy of $1 with the journal the engine made, on one disk
boot_staging() { # disk  kargs  marker  [timeout]
    cp --sparse=always "$1" "$MTMP/run.img"
    cp /usr/share/OVMF/OVMF_VARS_4M.fd "$MTMP/vars.fd"
    : > "$MTMP/out.txt"
    qemu-system-x86_64 -machine q35,accel=tcg -m 1536 -smp 2 \
        -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
        -drive if=pflash,format=raw,unit=1,file="$MTMP/vars.fd" \
        -no-reboot -kernel "$KERN" -initrd "$S/sim/esp/EFI/AurOS/staging.img" \
        -append "console=ttyS0 gpt aurstage.restart_after=0 $2" \
        -drive file="$MTMP/run.img",format=raw,if=none,id=d0 \
        -device virtio-blk-pci,drive=d0,serial=AUROSTEST \
        -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
        -display none -serial stdio > "$MTMP/out.txt" 2>&1 &
    _qp=$!; _seen=0; _i=0; _to=${4:-900}
    while [ "$_i" -lt "$_to" ]; do
        kill -0 "$_qp" 2>/dev/null || break
        if [ "$_seen" -eq 0 ] && grep -aq "$3" "$MTMP/out.txt" 2>/dev/null; then _seen=1; _i=$(( _to - 15 )); fi
        sleep 1; _i=$((_i+1))
    done
    kill -9 "$_qp" 2>/dev/null; wait "$_qp" 2>/dev/null
    grep -aq "$3" "$MTMP/out.txt"
}

# What "before" is: the files in Windows, as md5s.
L0=$(losetup --find --show -o $((P2S*512)) --sizelimit $(((P2E-P2S+1)*512)) "$DISK")
nt ntfs-3g -o ro "$L0" "$MTMP/m" >/dev/null 2>&1 || { echo "  cannot read the Windows volume"; exit 2; }
( cd "$MTMP/m" && find . -type f -exec md5sum {} \; | sort ) > "$MTMP/win.before"
umount "$MTMP/m"; losetup -d "$L0"
ESPSHA=$(extent_sha "$DISK" $((P1S*512)) $(((P1E-P1S+1)*512)))

# ═══════════════════════════════════════════════════════════════════
if want happy; then
echo
echo "  the happy path: Windows makes the room, AurOS is written into it"
new_machine happy
if run_engine && grep -q 'verdict=armed' "$S/out"; then
    ok "the engine arms the machine, the fast way"
else
    bad "the engine arms the machine, the fast way" "$(tail -4 "$S/err")"; WANT=""
fi
grep -q 'aurbridge-report v1 fast=1 ' "$S/out" && ok "...and says it took the fast way" \
    || bad "...and says it took the fast way" "$(grep -a 'fast way' "$S/err" | head -3)" "ran.log: $(tail -4 "$S/sim/ran.log" 2>/dev/null)" "err: $(tail -6 "$S/err")"
NOW=$(part_bytes "$S/disk.img" 2)
[ "$NOW" -lt $(( (P2E-P2S+1) * 512 )) ] && ok "Windows' partition is smaller ($((NOW/1048576)) of $(((P2E-P2S+1)*512/1048576)) MiB): Windows shrank it" \
    || bad "Windows' partition is smaller" "$NOW"
[ "$(sgdisk -p "$S/disk.img" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 | grep -c .)" = 3 ] \
    && ok "...and the table is otherwise untouched: still three partitions" || bad "the table is otherwise untouched" ""
sgdisk -v "$S/disk.img" 2>&1 | grep -q "No problems found" && ok "...and valid" || bad "...and valid" "$(sgdisk -v "$S/disk.img" 2>&1 | head -3)"

# THE GAP HOLDS EXACTLY THE IMAGE'S BYTES, hashed by a tool that is not the engine.
REP=$(grep 'aurbridge-report v1 fast=1' "$S/out")
RFIRST=$(printf '%s' "$REP" | sed -n 's/.* root=\([0-9]*\)\.\..*/\1/p')
BFIRST=$(printf '%s' "$REP" | sed -n 's/.* boot=\([0-9]*\)\.\..*/\1/p')
want_root=$(extent_sha "$AIMG" "$ROFF" "$RLEN"); got_root=$(extent_sha "$S/disk.img" $((RFIRST*512)) "$RLEN")
want_boot=$(extent_sha "$AIMG" "$EOFF" "$ELEN"); got_boot=$(extent_sha "$S/disk.img" $((BFIRST*512)) "$ELEN")
[ "$want_root" = "$got_root" ] && ok "the gap holds the image's root exactly (sha256, read back independently)" \
    || bad "the gap holds the image's root exactly" "want $want_root" "got  $got_root"
[ "$want_boot" = "$got_boot" ] && ok "...and the image's start-up partition exactly" \
    || bad "...and the image's start-up partition exactly" "want $want_boot" "got  $got_boot"
[ "$(extent_sha "$S/disk.img" $((P1S*512)) $(((P1E-P1S+1)*512)))" = "$ESPSHA" ] \
    && ok "the machine's own EFI partition was not touched" || bad "the machine's own EFI partition was not touched"
# The journal says where, and that nothing is to be mounted.
if tail -c 16384 "$S/sim/esp/EFI/AurOS/staging.img" | python3 -c '
import sys, zlib
d = sys.stdin.buffer.read(); i = d.rfind(b"\x1f\x8b\x08")
out = zlib.decompress(d[i:], 16 + zlib.MAX_WBITS)
sys.exit(0 if b"\"image_on\":\"gap\"" in out and b"\"root_sha256\":\"" in out else 1)'; then
    ok "the journal says image_on=gap, with the plan and the hashes"
else bad "the journal says image_on=gap, with the plan and the hashes"; fi
read -r PBG PBO PBS PBF PBH PBB < "$S/sim/putback-task.txt" 2>/dev/null
[ -n "${PBG:-}" ] && [ "$PBO" = $((P2S*512)) ] && [ "$PBF" = $(((P2E-P2S+1)*512)) ] && [ "$PBS" = "$NOW" ] \
    && ok "the put-back task is registered: this disk, C: at its start, shrunk size, original size" \
    || bad "the put-back task is registered with the right numbers" "$(cat "$S/sim/putback-task.txt" 2>/dev/null) want $((P2S*512)) $NOW $(((P2E-P2S+1)*512))"
[ "${PBH:-}" = 1 ] && [ "${PBB:-}" = 1 ] && ok "...and told to put hibernation back as she had it (S-23)" \
    || bad "...and told to put hibernation back as she had it" "hibfile=${PBH:-} hiberboot=${PBB:-}"
[ ! -f "$S/win/auros-desktop.img" ] && [ -z "$(ls "$S/win" | grep -v '^auros-desktop.img.gz.')" ] \
    && ok "no unpacked image was ever put on the Windows drive (only the pieces)" \
    || bad "no unpacked image was ever put on the Windows drive" "$(ls "$S/win")"

if boot_staging "$S/disk.img" "aurstage.install aurstage.min_gb=1" "INSTALLTEST-AUROS-STARTED"; then
    ok "staging installs it, and the new system starts"
else
    bad "staging installs it, and the new system starts" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -6)"
fi
grep -aq "verdict=installed .*mode=gap" "$MTMP/out.txt" && ok "...in gap mode, and says so" || bad "...in gap mode" "$(grep -a aurstage-report "$MTMP/out.txt" | tail -1)"
grep -aq "the copy written before the restart is exactly right\|Checking the copy of AurOS that Windows wrote" "$MTMP/out.txt" \
    && ok "...having checked every byte Windows wrote" || bad "...having checked every byte Windows wrote"
sgdisk -v "$MTMP/run.img" 2>&1 | grep -q "No problems found" && ok "another tool agrees the new table is valid" \
    || bad "another tool agrees the new table is valid" "$(sgdisk -v "$MTMP/run.img" 2>&1 | head -3)"
[ "$(sgdisk -p "$MTMP/run.img" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 | grep -c .)" = 6 ] \
    && ok "...six partitions: the machine's three and AurOS's three" || bad "six partitions"
[ -n "$(mach_part "$MTMP/run.img" AUROS-ROOT)" ] && [ -n "$(mach_part "$MTMP/run.img" AUROS-SAVED)" ] && [ -n "$(mach_part "$MTMP/run.img" AUROS-BOOT)" ] \
    && ok "...AUROS-ROOT, AUROS-SAVED and AUROS-BOOT among them" || bad "AUROS-ROOT, AUROS-SAVED and AUROS-BOOT"
win_files_now "$MTMP/run.img" "after the install"
grep -aqiE "windows-unreadable|mounting the windows|winvol" "$MTMP/out.txt" \
    && bad "staging never touched Windows' volume" "$(grep -aiE 'windows-unreadable|winvol' "$MTMP/out.txt" | head -2)" \
    || ok "staging never touched Windows' volume (no mount, in gap mode)"
cp --sparse=always "$S/disk.img" "$MTMP/happy-armed.img"
fi

# ═══════════════════════════════════════════════════════════════════
if want bitlocker; then
echo
echo "  a BitLocker PC: the lock is paused, never removed, and C: is never read"
new_machine fveno fve
BEFORE=$(whole_sha "$S/disk.img")
if run_engine; then bad "without her agreement to pause the lock: refused" "it armed"
else
    grep -q 'verdict=refused' "$S/out" && ok "without her agreement to pause the lock: refused" \
        || bad "without her agreement to pause the lock: refused" "$(cat "$S/out") $(tail -2 "$S/err")"
fi
[ "$(whole_sha "$S/disk.img")" = "$BEFORE" ] && ok "...with the disk byte for byte untouched (the shrink never began)" \
    || bad "...with the disk byte for byte untouched"
grep -q "protectors" "$S/sim/ran.log" 2>/dev/null && bad "...and the lock was never touched" "$(grep protectors "$S/sim/ran.log")" \
    || ok "...and the lock was never touched"

new_machine fve fve
if run_engine AURBRIDGE_BITLOCKER_PAUSE=1 && grep -q 'verdict=armed' "$S/out"; then
    ok "with it: armed, the fast way"
else bad "with it: armed, the fast way" "$(cat "$S/out") $(tail -3 "$S/err")"; fi
grep -q "manage-bde -protectors -disable C: -RebootCount 2" "$S/sim/ran.log" \
    && ok "the lock is paused for TWO restarts, by the step firmware updaters use" \
    || bad "the lock is paused for two restarts" "$(cat "$S/sim/ran.log" 2>/dev/null | tail -3)"
grep -qiE "decrypt|-off|-Disable-BitLocker|RebootCount 0" "$S/sim/ran.log" && bad "nothing is ever decrypted" "$(grep -iE 'decrypt|-off|RebootCount 0' "$S/sim/ran.log")" \
    || ok "...and nothing is ever decrypted, and never paused until further notice"
[ "$(dd if="$S/disk.img" bs=1 skip=$((P2S*512+3)) count=8 status=none)" = '-FVE-FS-' ] \
    && ok "C: still says -FVE-FS-: its lock is as it was" || bad "C: still says -FVE-FS-"
# What staging may not do: write to, or mount, C:
NOW=$(part_bytes "$S/disk.img" 2); WS=$(sgdisk -i 2 "$S/disk.img" | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
CSHA=$(extent_sha "$S/disk.img" $((WS*512)) "$NOW")
if boot_staging "$S/disk.img" "aurstage.install aurstage.min_gb=1" "INSTALLTEST-AUROS-STARTED"; then
    ok "staging installs it (it cannot read C:, and never needs to)"
else bad "staging installs it" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -6)"; fi
[ "$(extent_sha "$MTMP/run.img" $((WS*512)) "$NOW")" = "$CSHA" ] \
    && ok "C: is byte for byte what it was before staging started: not one byte written" \
    || bad "C: is byte for byte what it was before staging started"
grep -aqiE "windows-unreadable|winvol|ntfs-3g|ntfs3" "$MTMP/out.txt" && bad "...and never mounted" "$(grep -aiE 'windows-unreadable|winvol|ntfs' "$MTMP/out.txt" | head -2)" \
    || ok "...and never mounted"
sgdisk -v "$MTMP/run.img" 2>&1 | grep -q "No problems found" && ok "the new table is valid" || bad "the new table is valid"
fi

# ═══════════════════════════════════════════════════════════════════
if want abort; then
echo
echo "  closing the installer before the restart: C: is its own size again"
for kind in plain fve; do
    new_machine abort-$kind $( [ $kind = fve ] && echo fve )
    BEFORE=$(extent_sha "$S/disk.img" $((P2S*512)) $(((P2E-P2S+1)*512)))
    E=""; [ $kind = fve ] && E="AURBRIDGE_BITLOCKER_PAUSE=1"
    run_engine AURBRIDGE_ABORT=1 AURBRIDGE_FORGET=1 $E
    grep -q 'verdict=aborted' "$S/out" && ok "($kind) armed, then closed" || bad "($kind) armed, then closed" "$(tail -3 "$S/err")"
    [ "$(sgdisk -i 2 "$S/disk.img" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')" = "$P2E" ] \
        && ok "($kind) drive C: is its original size again (partition end $P2E)" \
        || bad "($kind) drive C: is its original size again" "$(sgdisk -i 2 "$S/disk.img" | grep -E 'First|Last')"
    T2=$(ntfs_total "$S/disk.img" "$P2S" | cut -d' ' -f1)
    [ $((NTFSTOT - T2)) -ge 0 ] && [ $((NTFSTOT - T2)) -le "$NTFSSPC" ] \
        && ok "($kind) ...and so is its filesystem" || bad "($kind) ...and so is its filesystem" "claims $T2, had $NTFSTOT"
    [ "$kind" = plain ] && win_files_now "$S/disk.img" "(plain) after closing"
    if [ $kind = fve ]; then
        grep -q "manage-bde -protectors -enable C:" "$S/sim/ran.log" && ok "(fve) the drive lock is switched back on" \
            || bad "(fve) the drive lock is switched back on" "$(tail -3 "$S/sim/ran.log")"
        [ "$(dd if="$S/disk.img" bs=1 skip=$((P2S*512+3)) count=8 status=none)" = '-FVE-FS-' ] \
            && ok "(fve) C: still says -FVE-FS-" || bad "(fve) C: still says -FVE-FS-"
    fi
    [ -z "$(find "$S/sim/esp" -type f 2>/dev/null)" ] && ok "($kind) nothing is left on the EFI partition" || bad "($kind) nothing is left on the EFI partition"
    [ ! -f "$S/sim/putback-task.txt" ] && ok "($kind) the put-back task is gone" || bad "($kind) the put-back task is gone" "$(cat "$S/sim/putback-task.txt")"
    [ -z "$(ls "$S/win" 2>/dev/null)" ] && ok "($kind) nothing is left of the download" || bad "($kind) nothing is left of the download" "$(ls "$S/win")"
done
fi

# ═══════════════════════════════════════════════════════════════════
if want failures; then
echo
echo "  a failure after Windows has made the room puts the room back"
# A damaged piece.
new_machine badpiece
cp -r "$PIECES" "$MTMP/www/bad"
python3 - "$MTMP/www/bad/auros-desktop.img.gz.000" <<'EOPY'
import sys
f = open(sys.argv[1], 'r+b'); f.seek(100); b = f.read(1); f.seek(100); f.write(bytes([b[0] ^ 0xFF])); f.close()
EOPY
sed "s#/good/#/bad/#" "$MTMP/pieces.txt" > "$S/pieces-bad.txt"
( cd "$ROOT_DIR" && env AURBRIDGE_FAST=1 AURBRIDGE_MIN_ROOT_MB=100 AURBRIDGE_PIECES="$S/pieces-bad.txt" \
    AURBRIDGE_IMAGE_SHA256="$IMGSHA" AURBRIDGE_IMAGE_BYTES="$IMGBYTES" \
    out/aurbridge-sim "$S/sim" desktop none "$S/win/auros-desktop.img" "$KERN" "$IMG" > "$S/out" 2> "$S/err" )
grep -q 'verdict=refused' "$S/out" && ok "a piece that arrives damaged: refused, in words" || bad "a damaged piece: refused" "$(cat "$S/out")"
[ "$(sgdisk -i 2 "$S/disk.img" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')" = "$P2E" ] \
    && ok "...and drive C: is its original size again" || bad "...and drive C: is its original size again" "$(sgdisk -i 2 "$S/disk.img" | grep Last)"
win_files_now "$S/disk.img" "after the failure"
# The image is not the one this installer was built for.
new_machine wrongimg
if run_engine AURBRIDGE_IMAGE_SHA256=0000000000000000000000000000000000000000000000000000000000000000; then
    bad "an image that is not the one this installer was built for: refused" "it armed"
else
    grep -q 'verdict=refused' "$S/out" && ok "an image that is not the one this installer was built for: refused" \
        || bad "a wrong image: refused" "$(cat "$S/out")"
fi
[ "$(sgdisk -i 2 "$S/disk.img" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')" = "$P2E" ] \
    && ok "...and drive C: is its original size again" || bad "...and drive C: is its original size again"
[ -z "$(find "$S/sim/esp" -type f 2>/dev/null)" ] && ok "...and nothing was put on the EFI partition" || bad "...and nothing was put on the EFI partition"
fi

# ═══════════════════════════════════════════════════════════════════
if want noroom; then
echo
echo "  when Windows cannot make enough room, nothing is changed"
new_machine noroom
BEFORE=$(whole_sha "$S/disk.img")
run_engine AURBRIDGE_MIN_ROOT_MB=2900
if grep -q 'verdict=armed' "$S/out" && ! grep -q 'aurbridge-report v1 fast=1' "$S/out"; then
    ok "no BitLocker: it falls back to the no-stick way, saying so"
    grep -q "the fast way is not possible" "$S/err" && ok "...in words" || bad "...in words" "$(grep -i fast "$S/err" | head -2)"
else bad "no BitLocker: it falls back" "$(cat "$S/out") $(tail -2 "$S/err")"; fi
new_machine noroomfve fve
BEFORE=$(whole_sha "$S/disk.img")
if run_engine AURBRIDGE_MIN_ROOT_MB=2900 AURBRIDGE_BITLOCKER_PAUSE=1; then bad "BitLocker: refused (the old way cannot install beside a locked drive)" "it armed"
else
    grep -q 'verdict=refused' "$S/out" && ok "BitLocker: refused (the old way cannot install beside a locked drive)" || bad "BitLocker: refused" "$(cat "$S/out")"
fi
[ "$(whole_sha "$S/disk.img")" = "$BEFORE" ] && ok "...with the disk byte for byte untouched" || bad "...with the disk byte for byte untouched"
fi

# ═══════════════════════════════════════════════════════════════════
if want cuts; then
echo
echo "  the power goes during staging's part of the fast path"
new_machine cuts
if ! run_engine || ! grep -q 'aurbridge-report v1 fast=1 ' "$S/out"; then
    bad "the engine arms the machine for the cuts" "$(tail -3 "$S/err")"
else
ARMED="$S/disk.img"; ORIGB=$(( (P2E-P2S+1) * 512 ))
SHRUNK_LAST=$(sgdisk -i 2 "$ARMED" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
nparts() { sgdisk -p "$1" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 | grep -c .; }
# What Windows' put-back task does when the room is free again: grow C: back.
# (Emulated here with the same helper the simulation uses for the shrink; the
# task's own script runs on real Windows in the e2e.)
putback_task() { sh "$S/sim/shrink.sh" resize "$1" 2 "$ORIGB" >/dev/null 2>&1; }
full_again() { # image label
    [ "$(sgdisk -i 2 "$1" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')" = "$P2E" ] \
        && ok "$2: drive C: is its original size again" || bad "$2: drive C: is its original size again" "$(sgdisk -i 2 "$1" | grep Last)"
    _t=$(ntfs_total "$1" "$P2S" | cut -d' ' -f1)
    [ $((NTFSTOT - _t)) -ge 0 ] && [ $((NTFSTOT - _t)) -le "$NTFSSPC" ] \
        && ok "$2: ...and so is its filesystem" || bad "$2: ...and so is its filesystem" "claims $_t, had $NTFSTOT"
    [ "$(nparts "$1")" = 3 ] && ok "$2: ...with the machine's own three partitions" || bad "$2: ...with the machine's own three partitions" "$(nparts "$1")"
    [ "$(extent_sha "$1" $((P1S*512)) $(((P1E-P1S+1)*512)))" = "$ESPSHA" ] \
        && ok "$2: ...and the EFI partition byte for byte what it was" || bad "$2: ...and the EFI partition byte for byte what it was"
    win_files_now "$1" "$2"
}
for pt in gate mirror-end commit-array commit-sector commit-backup boot-entry settle-end; do
    printf '\n  ── %s\n' "$pt"
    if boot_staging "$ARMED" "aurstage.install aurstage.min_gb=1 aurstage.die_at=$pt" "AURSTAGE-FAULT $pt" 600; then
        ok "the machine stops dead at $pt"
    else
        bad "the machine stops dead at $pt" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -3)"; continue
    fi
    cp --sparse=always "$MTMP/run.img" "$MTMP/cut.img"
    win_files_now "$MTMP/cut.img" "after the cut"
    case "$pt" in
      gate|mirror-end|commit-array)
        # The old table is in force; Windows is as Windows left it, C: smaller.
        [ "$(nparts "$MTMP/cut.img")" = 3 ] && ok "the old table is still the one in force (3 partitions)" \
            || bad "the old table is still the one in force (3 partitions)" "$(nparts "$MTMP/cut.img")"
        # (a) trying again installs.
        if boot_staging "$MTMP/cut.img" "aurstage.install aurstage.min_gb=1" "INSTALLTEST-AUROS-STARTED"; then
            ok "trying again installs AurOS"
            win_files_now "$MTMP/run.img" "after trying again"
        else bad "trying again installs AurOS" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"; fi
        # (b) and if she never tries again, Windows takes the room back.
        cp --sparse=always "$MTMP/cut.img" "$MTMP/pb.img"; putback_task "$MTMP/pb.img"
        full_again "$MTMP/pb.img" "and if she never does, Windows takes the room back"
        ;;
      *)
        # The new table is in force: Put Windows back from the copy on this
        # disk, then Windows' put-back task grows C: back.
        if boot_staging "$MTMP/cut.img" "aurstage.restore" "verdict=restored" 900; then
            ok "Put Windows back, from the copy on this disk, works"
            cp --sparse=always "$MTMP/run.img" "$MTMP/pb.img"
            [ "$(nparts "$MTMP/pb.img")" = 3 ] && ok "...the machine's own three partitions are back" || bad "...the machine's own three partitions are back" "$(nparts "$MTMP/pb.img")"
            putback_task "$MTMP/pb.img"
            full_again "$MTMP/pb.img" "then Windows' own task gives C: its room"
        else bad "Put Windows back, from the copy on this disk, works" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -5)"; fi
        ;;
    esac
done
fi
fi

# ═══════════════════════════════════════════════════════════════════
#  Put Windows back WITH HER FILES (docs/AURBRIDGE.md, "the handover").
#
#  AurOS has run: her files are in an exFAT volume that lives in a file in AurOS's root
#  (rootfs/usr/lib/auros/handover.sh, the REAL script; Ferry is a stand-in). The restore is
#  then booted with the record it was given, and must leave the machine's own three
#  partitions plus ONE more, AUROS-HANDOVER, over exactly those blocks -- and refuse, with
#  the disk byte for byte unchanged, anything that is not that.
# ═══════════════════════════════════════════════════════════════════
if want handover; then
echo
echo "  Put Windows back with her files: the handover volume becomes a partition"
modprobe exfat 2>/dev/null
command -v mkfs.exfat >/dev/null 2>&1 || { bad "mkfs.exfat is needed for this scenario (exfatprogs)"; WANT=""; }
new_machine handover
if [ -n "$WANT" ] && { ! run_engine || ! grep -q 'aurbridge-report v1 fast=1 ' "$S/out"; }; then
    bad "the engine arms the machine for the handover" "$(tail -3 "$S/err")"
elif [ -n "$WANT" ] && ! boot_staging "$S/disk.img" "aurstage.install aurstage.min_gb=1" "INSTALLTEST-AUROS-STARTED"; then
    bad "AurOS is installed for the handover test" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"
elif [ -n "$WANT" ]; then
cp --sparse=always "$MTMP/run.img" "$MTMP/inst.img"
# AurOS "running": its root mounted from a loop device with partitions, her files made, the
# real handover.sh run around a stand-in Ferry.
LD0=$(losetup -fP --show "$MTMP/inst.img"); lb=$(basename "$LD0")
for n in 1 2 3 4 5 6 7; do
    [ -e "/dev/${lb}p$n" ] || { [ -e "/sys/class/block/${lb}p$n/dev" ] && mknod "/dev/${lb}p$n" b $(sed 's/:/ /' "/sys/class/block/${lb}p$n/dev"); }
done
set -- $(mach_part "$MTMP/inst.img" AUROS-ROOT); RN=$1
mkdir -p "$MTMP/aroot" "$MTMP/chk" "$MTMP/her/Documents" "$MTMP/her/Pictures"
head -c 4000000 /dev/urandom > "$MTMP/her/Documents/book.pdf"
head -c 7000000 /dev/urandom > "$MTMP/her/Pictures/photo one.jpg"
printf 'hello\n' > "$MTMP/her/Documents/note.txt"
cp tools/fakeferry.sh "$MTMP/ferry"; chmod +x "$MTMP/ferry"
export FAKE_HOME="$MTMP/her"
if mount "/dev/${lb}p$RN" "$MTMP/aroot"; then
    mkdir -p "$MTMP/aroot/var/lib/auros"
    HENV="AUROS_STATE=$MTMP/aroot/var/lib/auros AUROS_FERRY=$MTMP/ferry AUROS_HANDOVER_RESERVE=1048576"
    HDEV=$(env $HENV sh rootfs/usr/lib/auros/handover.sh prepare 2>"$MTMP/h.err")
    if [ -n "$HDEV" ] && FERRY_HANDOVER_DEV="$HDEV" "$MTMP/ferry" back 2>>"$MTMP/h.err" &&
       REC=$(env $HENV sh rootfs/usr/lib/auros/handover.sh finish 2>>"$MTMP/h.err"); then
        ok "AurOS made the handover volume and gave its address ($REC)"
    else
        bad "AurOS made the handover volume and gave its address" "$(cat "$MTMP/h.err")" "device: ${HDEV:-none}; exfat: $(grep -c exfat /proc/filesystems) $(uname -r)"
        env $HENV sh rootfs/usr/lib/auros/handover.sh cancel 2>/dev/null; REC=""
    fi
    umount "$MTMP/aroot"
else bad "AurOS's root mounts for the handover test"; REC=""; fi
losetup -d "$LD0"
if [ -n "${REC:-}" ]; then
HFIRST=${REC%%,*}; _r=${REC#*,}; HCOUNT=${_r%%,*}; HSHA=${_r#*,}
INST_SHA=$(whole_sha "$MTMP/inst.img")
restore_end() { boot_staging "$1" "aurstage.restore $2" "aurstage-report v1 verdict=restore" 900; }

# (1) the way it is meant to go
if restore_end "$MTMP/inst.img" "aurstage.handover=$REC"; then
    grep -aq 'verdict=restored' "$MTMP/out.txt" && grep -aq 'handover=1' "$MTMP/out.txt" \
        && ok "Put Windows back keeps her files for Windows (verdict=restored, handover=1)" \
        || bad "Put Windows back keeps her files for Windows" "$(grep -a 'aurstage-report' "$MTMP/out.txt" | tail -2)"
else bad "the restore ends" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -4)"; fi
R="$MTMP/run.img"
set -- $(mach_part "$R" AUROS-HANDOVER)
if [ -n "${1:-}" ]; then
    [ "$2" = "$HFIRST" ] && [ $(( $3 - $2 + 1 )) = "$HCOUNT" ] \
        && ok "AUROS-HANDOVER is exactly the blocks AurOS named ($2..$3)" \
        || bad "AUROS-HANDOVER is exactly the blocks AurOS named" "table $2..$3, record $HFIRST+$HCOUNT"
else bad "AUROS-HANDOVER is in the table"; fi
[ "$(sgdisk -p "$R" 2>/dev/null | sed -n '/^Number/,$p' | tail -n +2 | grep -c .)" = 4 ] \
    && ok "...the machine's three partitions and that one, nothing of AurOS's else" \
    || bad "...the machine's three partitions and that one" "$(sgdisk -p "$R" 2>/dev/null | sed -n '/^Number/,$p')"
sgdisk -v "$R" 2>&1 | grep -q "No problems found" && ok "...the table is valid" || bad "...the table is valid" "$(sgdisk -v "$R" 2>&1 | head -3)"
[ "$(extent_sha "$R" $((HFIRST*512)) $((HCOUNT*512)))" = "$HSHA" ] \
    && ok "...and the blocks still hold exactly what AurOS hashed" || bad "...and the blocks still hold exactly what AurOS hashed"
if mount -o ro,loop,offset=$((HFIRST*512)),sizelimit=$((HCOUNT*512)) -t exfat "$R" "$MTMP/chk" 2>"$MTMP/h.err"; then
    d=0; ( cd "$MTMP/her" && find . -type f | while read -r f; do p=${f#./}
        cmp -s "$MTMP/her/$p" "$MTMP/chk/AurOS-handover/files/${p%%/*}/${p#*/}" || echo "DIFF $p"; done ) > "$MTMP/diff"
    [ -f "$MTMP/chk/AurOS-handover/DONE" ] && [ ! -s "$MTMP/diff" ] \
        && ok "...mounted as exFAT: DONE is there and her files are byte for byte" \
        || bad "...mounted as exFAT: DONE and her files" "$(cat "$MTMP/diff")"
    umount "$MTMP/chk"
else bad "...mounts as exFAT" "$(cat "$MTMP/h.err")"; fi
[ "$(extent_sha "$R" $((P1S*512)) $(((P1E-P1S+1)*512)))" = "$ESPSHA" ] \
    && ok "the machine's own EFI partition is byte for byte what it was" || bad "the machine's own EFI partition is byte for byte what it was"
win_files_now "$R" "after Put Windows back with her files"

# (2) refusals, each with the disk byte for byte unchanged
refuse() { # label  record-or-garbage  expect-text
    if restore_end "$MTMP/inst.img" "aurstage.handover=$2"; then
        grep -aq 'verdict=restore-failed' "$MTMP/out.txt" && ok "$1: refused" \
            || bad "$1: refused" "$(grep -a 'aurstage-report' "$MTMP/out.txt" | tail -2)"
        grep -aq "$3" "$MTMP/out.txt" && ok "...saying why, in words" || bad "...saying why, in words" "$(grep -a 'aurstage:' "$MTMP/out.txt" | tail -3)"
    else bad "$1: the restore ends" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -3)"; fi
    [ "$(whole_sha "$MTMP/run.img")" = "$INST_SHA" ] && ok "...and the disk is byte for byte untouched" || bad "...and the disk is byte for byte untouched"
}
flip=$(printf '%s' "$HSHA" | sed 's/^./0/'); [ "$flip" = "$HSHA" ] && flip=$(printf '%s' "$HSHA" | sed 's/^./1/')
refuse "a record whose hash is not what is on the disk" "$HFIRST,$HCOUNT,$flip" "not what AurOS checked"
set -- $(mach_part "$MTMP/inst.img" AUROS-ROOT)
refuse "blocks that begin before AurOS's own space" "$(( $2 - 8 )),$HCOUNT,$HSHA" "not inside AurOS's own space"
refuse "blocks inside Windows' own partition" "$(( P2S + 8 )),$HCOUNT,$HSHA" "not inside AurOS's own space"
refuse "a record that cannot be read" "nonsense" "cannot be read"
set -- $(mach_part "$MTMP/inst.img" AUROS-ROOT)
refuse "blocks that end after AurOS's own space" "$(( $3 - HCOUNT / 2 )),$HCOUNT,$HSHA" "not inside AurOS's own space"

# (3) the power goes at each instant; what is left is whole, and the restore can be run again
for pt in handover-checked restore-esp-mid restore-array restore-sector restore-backup; do
    printf '\n  ── %s\n' "$pt"
    if boot_staging "$MTMP/inst.img" "aurstage.restore aurstage.handover=$REC aurstage.die_at=$pt" "AURSTAGE-FAULT $pt" 900; then
        ok "the machine stops dead at $pt"
    else bad "the machine stops dead at $pt" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -3)"; continue; fi
    cp --sparse=always "$MTMP/run.img" "$MTMP/cut.img"
    case "$pt" in
      handover-checked|restore-esp-mid|restore-array)
        # AurOS is still in the table: the restore can simply be run again, and ends as it should.
        # (sgdisk checks the header's CRC and reads the spare table when the main one is torn,
        # as the kernel does; mach_part reads whatever the main header points at)
        sgdisk -p "$MTMP/cut.img" 2>/dev/null | grep -q AUROS-ROOT \
            && ok "AurOS is still in the table (and so are her files)" || bad "AurOS is still in the table"
        if restore_end "$MTMP/cut.img" "aurstage.handover=$REC" && grep -aq 'verdict=restored' "$MTMP/out.txt"; then
            ok "run again: it ends restored"
        else bad "run again: it ends restored" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -3)"; fi
        ;;
      *)
        # The new table is in force already: Windows starts, her files are in their partition.
        set -- $(mach_part "$MTMP/cut.img" AUROS-HANDOVER)
        [ -n "${1:-}" ] && [ "$2" = "$HFIRST" ] && ok "the new table is in force, with AUROS-HANDOVER over her files" \
            || bad "the new table is in force, with AUROS-HANDOVER over her files" "${1:-none}"
        ;;
    esac
    R="$MTMP/run.img"; [ "$pt" = restore-sector ] || [ "$pt" = restore-backup ] && R="$MTMP/cut.img"
    set -- $(mach_part "$R" AUROS-HANDOVER)
    [ -n "${1:-}" ] && [ "$2" = "$HFIRST" ] && [ "$(extent_sha "$R" $((HFIRST*512)) $((HCOUNT*512)))" = "$HSHA" ] \
        && ok "at the end of $pt: her files' partition is there, and intact" \
        || bad "at the end of $pt: her files' partition is there, and intact" "${1:-none}"
    win_files_now "$R" "at the end of $pt"
done
fi
losetup -a | grep -q "$MTMP" && losetup -a | grep "$MTMP" | cut -d: -f1 | while read -r l; do losetup -d "$l" 2>/dev/null; done
fi
fi

# ═══════════════════════════════════════════════════════════════════
#  The reason a refusal gives is left on the machine (src/aurstage/stagelog.c).
#
#  The staging environment says its reason to a screen and then restarts the computer by
#  itself, so a verdict word was all that survived. With \EFI\AurOS on the EFI partition (AurBridge
#  put it there) the words are written to \EFI\AurOS\stage.log -- and that is ALL that changes:
#  every other partition is byte for byte what it was, and the EFI partition holds the same files
#  plus that one. (aurstage.nolog, which the other scenarios do not need because their EFI
#  partition has no \EFI\AurOS, turns it off.)
# ═══════════════════════════════════════════════════════════════════
if want stagelog; then
echo
echo "  a refusal leaves its reason on the EFI partition"
command -v mmd >/dev/null 2>&1 || { bad "mtools are needed for this scenario"; WANT=""; }
new_machine stagelog
if [ -n "$WANT" ] && { ! run_engine || ! grep -q 'aurbridge-report v1 fast=1 ' "$S/out"; }; then
    bad "the engine arms the machine for the log test" "$(tail -3 "$S/err")"
elif [ -n "$WANT" ]; then
    # AurBridge's directory on the EFI partition, as the real engine leaves it
    ESPOFF=$((P1S*512))
    mmd -i "$S/disk.img@@$ESPOFF" ::/EFI ::/EFI/AurOS 2>/dev/null
    echo "AurOS was here" > "$MTMP/marker.txt"
    mcopy -i "$S/disk.img@@$ESPOFF" "$MTMP/marker.txt" ::/EFI/AurOS/marker.txt
    # a root partition that is not what Windows wrote: staging must refuse it
    REP=$(grep 'aurbridge-report v1 fast=1' "$S/out")
    RFIRST=$(printf '%s' "$REP" | sed -n 's/.* root=\([0-9]*\)\.\..*/\1/p')
    printf '\xff' | dd of="$S/disk.img" bs=1 seek=$((RFIRST*512 + 4096*50)) conv=notrunc status=none
    ESP_BEFORE=$(mdir -i "$S/disk.img@@$ESPOFF" -/ -b ::/ 2>/dev/null | sort)
    OTHERS_BEFORE=$(for part in 2 3; do f=$(sgdisk -i $part "$S/disk.img" | sed -n 's/^First sector: \([0-9]*\).*/\1/p'); l=$(sgdisk -i $part "$S/disk.img" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p'); extent_sha "$S/disk.img" $((f*512)) $(((l-f+1)*512)); done)
    TABLE_BEFORE=$(sgdisk -p "$S/disk.img" 2>/dev/null | sed 1d)
    boot_staging "$S/disk.img" "aurstage.install aurstage.min_gb=1" "aurstage-report v1 verdict=" 900 || true
    grep -aq "aurstage-report v1 verdict=" "$MTMP/out.txt" && ! grep -aq "INSTALLTEST-AUROS-STARTED" "$MTMP/out.txt" \
        && ok "staging refuses the damaged root" || bad "staging refuses the damaged root" "$(grep -a 'aurstage' "$MTMP/out.txt" | tail -3)"
    R="$MTMP/run.img"
    LOGTXT=$(mtype -i "$R@@$ESPOFF" ::/EFI/AurOS/stage.log 2>/dev/null)
    if [ -n "$LOGTXT" ]; then
        ok "...and \\EFI\\AurOS\\stage.log is there"
        echo "$LOGTXT" | grep -q "aurstage-report v1 verdict=" && ok "...with the verdict" || bad "...with the verdict"
        echo "$LOGTXT" | grep -q "WARNING: " && ok "...and the reason it was given in words" || bad "...and the reason it was given in words" "$(echo "$LOGTXT" | tail -4)"
        echo "$LOGTXT" | grep -q "AurOS staging environment" && ok "...and everything said before it" || bad "...and everything said before it"
    else
        bad "\\EFI\\AurOS\\stage.log is there"
    fi
    ESP_AFTER=$(mdir -i "$R@@$ESPOFF" -/ -b ::/ 2>/dev/null | sort)
    echo "$ESP_BEFORE" > "$MTMP/espb"; echo "$ESP_AFTER" > "$MTMP/espa"
    DIFF=$(diff "$MTMP/espb" "$MTMP/espa" | grep '^[<>]' | grep -vi 'stage.log' || true)
    [ -z "$DIFF" ] && ok "the EFI partition holds the same files, plus that one" || bad "the EFI partition holds the same files, plus that one" "$DIFF"
    mtype -i "$R@@$ESPOFF" ::/EFI/AurOS/marker.txt 2>/dev/null | grep -q "AurOS was here" \
        && ok "...and AurBridge's own file is untouched" || bad "...and AurBridge's own file is untouched"
    OTHERS_AFTER=$(for part in 2 3; do f=$(sgdisk -i $part "$R" | sed -n 's/^First sector: \([0-9]*\).*/\1/p'); l=$(sgdisk -i $part "$R" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p'); extent_sha "$R" $((f*512)) $(((l-f+1)*512)); done)
    [ "$OTHERS_BEFORE" = "$OTHERS_AFTER" ] && ok "Windows' partition and the recovery partition are byte for byte what they were" \
        || bad "Windows' partition and the recovery partition are byte for byte what they were"
    TABLE_AFTER=$(sgdisk -p "$R" 2>/dev/null | sed 1d)
    [ "$TABLE_AFTER" = "$TABLE_BEFORE" ] && ok "...and so is the partition table" \
        || { echo "$TABLE_BEFORE" > "$MTMP/tb"; echo "$TABLE_AFTER" > "$MTMP/ta"; bad "...and so is the partition table" "$(diff "$MTMP/tb" "$MTMP/ta" | head -6)"; }
    # and with aurstage.nolog nothing at all is written
    cp --sparse=always "$S/disk.img" "$MTMP/nolog.img"
    boot_staging "$MTMP/nolog.img" "aurstage.install aurstage.min_gb=1 aurstage.nolog" "aurstage-report v1 verdict=" 900 || true
    [ "$(whole_sha "$MTMP/run.img")" = "$(whole_sha "$MTMP/nolog.img")" ] \
        && ok "with aurstage.nolog the disk is byte for byte untouched" || bad "with aurstage.nolog the disk is byte for byte untouched"
fi
fi

echo
if [ "$fail" -gt 0 ]; then
    echo "$fail of $checked wrong."
    echo "Each is either a person whose Windows is smaller than it should be, or one whose install cannot be trusted."
    exit 1
fi
echo "$checked checks: the fast path writes only into the room Windows made, staging only"
echo "checks and commits, C: is never read or written by staging, and every way out gives the room back."
exit 0
