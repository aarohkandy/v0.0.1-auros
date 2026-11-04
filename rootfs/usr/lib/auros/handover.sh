#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  handover.sh — AurOS's half of "Put Windows back with her files"
#
#  docs/AURBRIDGE.md, "Put Windows back with her files: the handover".
#  Her files must reach Windows before AurOS is removed, and AurOS must
#  never write into the Windows volume (on a BitLocker PC it cannot even
#  open it). So they are copied into a small exFAT volume that lives in
#  a FILE in AurOS's own root. After the restart the staging environment
#  turns exactly the disk blocks that file occupies into a partition,
#  and Windows' own driver copies from there into her profile.
#
#    handover.sh prepare   make the file and a loop device on it; print
#                          the device. Ferry then fills it (`ferry back`
#                          with FERRY_HANDOVER_DEV set to that device).
#    handover.sh finish    after Ferry: detach, hash the file, find where
#                          on the disk it lies and prove it by reading
#                          the DISK there; print  FIRST,COUNT,SHA256  --
#                          first block and count in 512-byte units.
#    handover.sh cancel    detach and remove the file.
#
#  exit  0 done          2 failed        3 not enough room in AurOS
#        4 the file is not in one run of blocks on the disk (too big for the
#          filesystem's layout): the USB stick path instead
#        5 this root cannot be handed over (encrypted, not a plain ext4
#          partition): the USB stick path instead
#
#  THE FILE IS WRITTEN, NOT SPARSE: a block that is allocated but never
#  written reads as zeros through the file and as whatever was on the disk
#  before when the staging environment reads the disk itself. And it must
#  lie in ONE RUN of blocks on the disk, because a partition is one run.
#
#  Overridable for tools/handoverlinuxtest.sh and for nothing else.
# ═══════════════════════════════════════════════════════════════════
set -u
STATE=${AUROS_STATE:-/var/lib/auros}
FERRY=${AUROS_FERRY:-/usr/bin/ferry}
RESERVE=${AUROS_HANDOVER_RESERVE:-2147483648}      # AurOS keeps this much free
IMG="$STATE/handover.img"
DEV="$STATE/handover.dev"
MIB=1048576
LABEL=AUROSHANDOV

say() { printf '%s\n' "$*" >&2; }
detach() {
    if [ -f "$DEV" ]; then
        d=$(cat "$DEV" 2>/dev/null)
        [ -n "$d" ] && losetup -d "$d" 2>/dev/null
        rm -f "$DEV"
    fi
}
cancel() { detach; rm -f "$IMG"; }

# How many PHYSICAL pieces the file is in, where the first one is (in filesystem blocks),
# and the block size: "N START BS". ext4 never makes one extent longer than 128 MiB, so a
# big file is listed as several extents even when they follow each other on the disk; what
# matters is whether each starts where the one before it ended.
extents() {
    filefrag -v "$1" 2>/dev/null | awk '
        /blocks of [0-9]+ bytes/ { for (i = 1; i <= NF; i++) if ($i == "of") bs = $(i + 1) }
        /^ *[0-9]+:/ { gsub(/[.:]+/, " ")
                       if (n == 0) start = $4
                       else if ($4 != pend + 1) pieces++
                       n++; pend = $5 }
        END { if (n > 0) print pieces + 1, start, bs }'
}

# The partition this file lies in and where that starts, in 512-byte units;
# refuses anything that is not a plain partition holding ext4.
where() {
    fstype=$(stat -f -c %T "$1" 2>/dev/null)
    case "$fstype" in ext2/ext3|ext4) ;; *) say "the AurOS root is $fstype, not ext4"; return 1 ;; esac
    src=$(findmnt -no SOURCE -T "$1" 2>/dev/null)
    case "$src" in
        /dev/mapper/*|/dev/dm-*) say "the AurOS root is on an encrypted or mapped volume"; return 1 ;;
    esac
    node=${src#/dev/}
    [ -r "/sys/class/block/$node/start" ] || { say "the AurOS root is not on a plain partition"; return 1; }
    echo "$src $(cat "/sys/class/block/$node/start")"
}

case "${1:-}" in
prepare)
    cancel
    [ -x "$FERRY" ] || { say "no Ferry"; exit 2; }
    where "$STATE" >/dev/null || exit 5
    need=$("$FERRY" handover --need 2>/dev/null)
    case "$need" in ''|*[!0-9]*) say "Ferry did not say how much room the files need"; exit 2 ;; esac
    # 3% on Ferry's number (Windows appends to the volume) and a little to spare; at least
    # 32 MiB. NOT more: the file has to lie in one run of blocks on the disk, and a bigger
    # file is more likely to be broken by the filesystem's own metadata (ext4 puts backup
    # descriptors and bitmaps between block groups, so a run is rarely longer than a
    # gigabyte or two). That limit is real, and it is why big collections go to a USB stick.
    size=$(( (need + need / 32 + 16 * MIB + MIB - 1) / MIB * MIB ))
    [ "$size" -lt $((32 * MIB)) ] && size=$((32 * MIB))
    avail=$(df -B1 --output=avail "$STATE" 2>/dev/null | tail -1 | tr -d ' ')
    case "$avail" in ''|*[!0-9]*) exit 2 ;; esac
    if [ "$avail" -lt $((size + RESERVE)) ]; then
        say "needs $size bytes free beyond $RESERVE, has $avail"
        exit 3
    fi
    dd if=/dev/zero of="$IMG" bs=1M count=$((size / MIB)) conv=fsync status=none || { cancel; exit 2; }
    set -- $(extents "$IMG")
    if [ "${1:-0}" != 1 ]; then
        command -v e4defrag >/dev/null 2>&1 && e4defrag "$IMG" >/dev/null 2>&1
        set -- $(extents "$IMG")
        [ "${1:-0}" = 1 ] || { cancel; say "the file is in ${1:-?} pieces"; exit 4; }
    fi
    mkfs.exfat -L "$LABEL" -c 131072 "$IMG" >/dev/null 2>&1 || { cancel; say "mkfs.exfat failed"; exit 2; }
    d=$(losetup -f --show "$IMG" 2>/dev/null) || { cancel; say "no loop device"; exit 2; }
    echo "$d" > "$DEV"
    echo "$d"
    ;;
finish)
    [ -f "$IMG" ] || { say "no handover file"; exit 2; }
    if [ -f "$DEV" ]; then
        d=$(cat "$DEV")
        if grep -q "^$d " /proc/mounts 2>/dev/null; then say "$d is still mounted"; exit 2; fi
    fi
    detach
    sync
    set -- $(extents "$IMG")
    [ "${1:-0}" = 1 ] || { say "the file is in ${1:-?} pieces"; exit 4; }
    start=$2; bs=$3
    w=$(where "$IMG") || exit 5
    set -- $w
    src=$1; pstart=$2
    bytes=$(stat -c %s "$IMG")
    [ $((bytes % 512)) = 0 ] && [ $((bs % 512)) = 0 ] || exit 2
    first=$(( pstart + start * (bs / 512) ))
    count=$(( bytes / 512 ))
    sha=$(sha256sum "$IMG" | cut -d' ' -f1)
    # PROVED BY READING THE DISK, not by trusting the arithmetic: the same bytes the
    # staging environment will read, straight from the device, bypassing every cache.
    disk=$(lsblk -no PKNAME "$src" 2>/dev/null | head -1)
    [ -n "$disk" ] || exit 2
    got=$(dd if="/dev/$disk" bs=512 skip="$first" count="$count" iflag=direct status=none 2>/dev/null | sha256sum | cut -d' ' -f1)
    if [ "$got" != "$sha" ]; then say "the disk does not hold the file where it was expected"; exit 2; fi
    echo "$first,$count,$sha"
    ;;
cancel)
    cancel
    ;;
*)
    echo "usage: handover.sh prepare | finish | cancel" >&2
    exit 2
    ;;
esac
