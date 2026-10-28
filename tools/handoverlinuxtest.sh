#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  handoverlinuxtest — AurOS's half of "Put Windows back with her files"
#
#  rootfs/usr/lib/auros/handover.sh turns a FILE in AurOS's root into the
#  exFAT volume her files are saved in, and says where on the DISK that file
#  lies, so that the staging environment can turn exactly those blocks into
#  a partition. Everything here depends on that address being true, so the
#  test never believes the script's arithmetic: it reads the DISK IMAGE
#  (not the loop device the script used) at the address it printed, and
#  mounts what it finds there as exFAT.
#
#    - a real GPT disk with an ext4 root; Ferry is a stand-in that writes the
#      layout docs/FERRY.md describes (the real one is tested by Ferry's own
#      suite)
#    - prepare / (Ferry) / finish prints FIRST,COUNT,SHA256; the disk bytes there
#      hash to SHA256 and mount as exFAT holding her files byte for byte, DONE
#      included
#    - no room, a root in pieces, a root on an encrypted volume: each refused
#      with its own exit status and nothing left behind
#    - a handover file that is sparse (never written) is caught by the disk read
#
#  Needs root, loop devices and exfatprogs. In the homebase container:
#    docker run --rm --privileged a2-kvm:exfat sh tools/handoverlinuxtest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-70s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-70s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

for t in losetup sgdisk mkfs.ext4 mkfs.exfat filefrag python3 sha256sum; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
T=$(mktemp -d "${TMPDIR:-/tmp}/handoverlinux.XXXXXX")
LOOP=""
cleanup() {
    umount "$T/root" 2>/dev/null; umount "$T/vol" 2>/dev/null; umount "$T/chk" 2>/dev/null
    [ -n "$LOOP" ] && losetup -d "$LOOP" 2>/dev/null
    for d in $(losetup -j "$T/disk.img" 2>/dev/null | cut -d: -f1); do losetup -d "$d" 2>/dev/null; done
    rm -rf "$T"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$T/root" "$T/vol" "$T/chk"

echo
echo "Does AurOS leave her files where the staging environment will look?"
echo

# ── a disk: ESP, then AurOS's ext4 root ─────────────────────────────
truncate -s 1G "$T/disk.img"
sgdisk -n 1:2048:+64M -t 1:ef00 -c 1:AUROS-BOOT -n 2:0:0 -t 2:8304 -c 2:AUROS-ROOT "$T/disk.img" >/dev/null 2>&1
LOOP=$(losetup -fP --show "$T/disk.img")
lb=$(basename "$LOOP")
# No udev here: make the partition's node from what the kernel says it is.
for n in 1 2; do
    d="/sys/class/block/${lb}p$n/dev"
    [ -e "/dev/${lb}p$n" ] || mknod "/dev/${lb}p$n" b $(sed 's/:/ /' "$d") 2>/dev/null
done
PART="/dev/${lb}p2"
mkfs.ext4 -q -F "$PART" || { echo "mkfs.ext4 failed"; exit 2; }
mount "$PART" "$T/root" || { echo "mount failed"; exit 2; }
STATE="$T/root/var/lib/auros"
mkdir -p "$STATE"

# ── her files, and a stand-in for Ferry ─────────────────────────────
mkdir -p "$T/home/Documents" "$T/home/Pictures"
head -c 3000000 /dev/urandom > "$T/home/Documents/book.pdf"
head -c 9000000 /dev/urandom > "$T/home/Pictures/photo one.jpg"
printf 'hello\n' > "$T/home/Documents/note.txt"
cp tools/fakeferry.sh "$T/ferry"
chmod +x "$T/ferry"
export FAKE_HOME="$T/home"
H="rootfs/usr/lib/auros/handover.sh"
# (the real reserve is 2 GiB; this disk is 1 GiB)
run() { AUROS_HANDOVER_RESERVE=${AUROS_HANDOVER_RESERVE:-67108864} AUROS_STATE="$STATE" AUROS_FERRY="$T/ferry" "$@"; }

# ── the path that has to work ───────────────────────────────────────
DEV=$(run sh "$H" prepare 2>"$T/err"); rc=$?
[ "$rc" = 0 ] && [ -b "$DEV" ] && ok "prepare: a file in AurOS's root, and a loop device on it ($DEV)" \
    || { bad "prepare: a file in AurOS's root, and a loop device on it" "exit $rc: $(cat "$T/err")"; exit 1; }
[ "$(blkid -o value -s LABEL "$DEV")" = AUROSHANDOV ] && [ "$(blkid -o value -s TYPE "$DEV")" = exfat ] \
    && ok "...it is exFAT, labelled AUROSHANDOV" || bad "...it is exFAT, labelled AUROSHANDOV" "$(blkid "$DEV")"
SZ=$(stat -c %s "$STATE/handover.img")
[ "$SZ" -ge $((32*1048576)) ] && [ $((SZ % 1048576)) = 0 ] && ok "...a whole number of MiB, at least 32 ($SZ)" || bad "...a whole number of MiB, at least 32" "$SZ"
FERRY_HANDOVER_DEV="$DEV" "$T/ferry" back
REC=$(run sh "$H" finish 2>"$T/err"); rc=$?
[ "$rc" = 0 ] && ok "finish: gives a record ($REC)" || { bad "finish: gives a record" "exit $rc: $(cat "$T/err")"; exit 1; }
[ -z "$(losetup -j "$STATE/handover.img" | cut -d: -f1)" ] && ok "...and the loop device is gone" || bad "...and the loop device is gone"
FIRST=${REC%%,*}; REST=${REC#*,}; COUNT=${REST%%,*}; SHA=${REST#*,}
umount "$T/root"          # AurOS is "off": nothing but the disk image is read from here on
losetup -d "$LOOP"; LOOP=""
# The script's own proof is not the test. This reads the DISK IMAGE FILE.
GOT=$(dd if="$T/disk.img" bs=512 skip="$FIRST" count="$COUNT" status=none | sha256sum | cut -d' ' -f1)
[ "$GOT" = "$SHA" ] && ok "the disk image, read at that address, hashes to the record" || bad "the disk image, read at that address, hashes to the record" "$GOT vs $SHA"
if mount -o ro,loop,offset=$((FIRST*512)),sizelimit=$((COUNT*512)) -t exfat "$T/disk.img" "$T/chk" 2>"$T/err"; then
    ok "...and what is there mounts as exFAT"
    R="$T/chk/AurOS-handover"
    [ -f "$R/DONE" ] && ok "...with Ferry's DONE" || bad "...with Ferry's DONE"
    d=0; ( cd "$T/home" && find . -type f | while read -r f; do
        p=${f#./}; cmp -s "$T/home/$p" "$R/files/${p%%/*}/${p#*/}" || echo "DIFF $p"; done ) > "$T/diff"
    [ ! -s "$T/diff" ] && ok "...and her files are there, byte for byte" || bad "...and her files are there, byte for byte" "$(cat "$T/diff")"
    ( cd "$R" && sha256sum -c --quiet manifest.sha256 ) >/dev/null 2>&1 && ok "...and the manifest is the one Ferry hashed" || bad "...and the manifest is the one Ferry hashed"
    umount "$T/chk"
else
    bad "...and what is there mounts as exFAT" "$(cat "$T/err")"
fi
# The address is inside AurOS's root partition and nowhere else.
RS=$(sgdisk -i 2 "$T/disk.img" | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
RE=$(sgdisk -i 2 "$T/disk.img" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
[ "$FIRST" -ge "$RS" ] && [ $((FIRST + COUNT - 1)) -le "$RE" ] && ok "the address lies wholly inside AUROS-ROOT ($RS..$RE)" \
    || bad "the address lies wholly inside AUROS-ROOT" "$FIRST+$COUNT vs $RS..$RE"
[ $((FIRST % 8)) = 0 ] && ok "...and is aligned to 4096 bytes" || bad "...and is aligned to 4096 bytes" "$FIRST"

# ── refusals ────────────────────────────────────────────────────────
LOOP=$(losetup -fP --show "$T/disk.img"); lb=$(basename "$LOOP")
for n in 1 2; do [ -e "/dev/${lb}p$n" ] || mknod "/dev/${lb}p$n" b $(sed 's/:/ /' "/sys/class/block/${lb}p$n/dev") 2>/dev/null; done
PART="/dev/${lb}p2"
mount "$PART" "$T/root"
rm -f "$STATE/handover.img" "$STATE/handover.dev"

# ── a file that was never written is caught by reading the disk ───────
# Stale bytes on the disk first, so that "allocated but never written" differs from zeros.
head -c 100000000 /dev/urandom > "$STATE/junk" 2>/dev/null; sync; rm -f "$STATE/junk"; sync
sed 's#^    dd if=/dev/zero of="\$IMG" bs=1M count=\$((size / MIB)) conv=fsync status=none#    fallocate -l $size "$IMG"#' "$H" > "$T/h-sparse.sh"
cmp -s "$H" "$T/h-sparse.sh" && bad "setup: the sparse variant differs from the script" "the dd line was not found"
DEV=$(run sh "$T/h-sparse.sh" prepare 2>"$T/err") || true
if [ -n "${DEV:-}" ]; then
    FERRY_HANDOVER_DEV="$DEV" "$T/ferry" back
    # What a never-written block holds on the disk is whatever was there before. Put
    # something there, at the very end of the file's blocks, behind the filesystem's back.
    set -- $(filefrag -v "$STATE/handover.img" | awk '/^ *[0-9]+:/ { gsub(/[.:]+/, " "); s = $4; e = $5; n++ } END { print s, e, n }')
    ENDBLK=$2
    head -c 1048576 /dev/urandom | dd of="$LOOP" bs=4096 seek=$(( $(cat /sys/class/block/${lb}p2/start) / 8 + ENDBLK - 255 )) conv=notrunc status=none
    run sh "$T/h-sparse.sh" finish >/dev/null 2>"$T/err"; rc=$?
    [ "$rc" != 0 ] && ok "a sparse handover file: finish refuses (exit $rc), the disk does not hold it" \
        || bad "a sparse handover file: finish refuses" "it printed a record"
else
    bad "a sparse handover file: the variant could not even start" "$(cat "$T/err" 2>/dev/null)"
fi
run sh "$H" cancel

AUROS_HANDOVER_RESERVE=999999999999 run sh "$H" prepare >/dev/null 2>&1; rc=$?
[ "$rc" = 3 ] && ok "no room beyond AurOS's reserve: exit 3" || bad "no room beyond AurOS's reserve: exit 3" "exit $rc"
[ ! -e "$STATE/handover.img" ] && ok "...and nothing is left behind" || bad "...and nothing is left behind"

# A root in pieces: fill the free space with small files and free every other one.
# (Reserve 0 so the room is judged only by the pieces.)
python3 - "$STATE" <<'EOPY'
import os, sys
d = sys.argv[1]; names = []
try:
    i = 0
    while True:
        p = "%s/f%d" % (d, i)
        with open(p, "wb") as f: f.write(b"\0" * 4096)
        names.append(p); i += 1
except OSError:
    pass
for p in names[::2]:
    os.unlink(p)
EOPY
sync
AUROS_HANDOVER_RESERVE=0 FAKE_NEED=300000000 run sh "$H" prepare >/dev/null 2>&1; rc=$?
[ "$rc" = 4 ] || [ "$rc" = 3 ] && ok "a root whose free space is in pieces: refused (exit $rc)" || bad "a root whose free space is in pieces: refused" "exit $rc"
[ ! -e "$STATE/handover.img" ] && ok "...and nothing is left behind" || bad "...and nothing is left behind"
rm -f "$STATE"/f*

# An encrypted root: the file would be ciphertext on the disk.
mkdir -p "$T/shim"
printf '#!/bin/sh\necho /dev/mapper/cryptroot\n' > "$T/shim/findmnt"; chmod +x "$T/shim/findmnt"
PATH="$T/shim:$PATH" run sh "$H" prepare >/dev/null 2>&1; rc=$?
[ "$rc" = 5 ] && ok "a root on an encrypted volume: exit 5 (the USB path)" || bad "a root on an encrypted volume: exit 5" "exit $rc"
[ ! -e "$STATE/handover.img" ] && ok "...and nothing was made" || bad "...and nothing was made"

run sh "$H" cancel

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong. Each is a person whose files would not be where Windows looks."; exit 1; fi
echo "$checked checks: the file is written, in one piece, at the address AurOS gives, and that address is true."
