#!/bin/sh
# The copyright files of the programs inside AurOS-Setup.exe, for its NOTICES
# (docs/legal/notices/NOTICES.txt). Run on an Ubuntu 24.04 machine that has
# those packages installed (homebase has):
#
#     sh tools/legal/gather-copyrights.sh > docs/legal/licenses/component-copyrights.txt
#
# The list is what tools/legal/stagingcheck.py finds in the real staging cpio (and fails on if one is
# missing) plus the three boot files and the fonts. It was the closure of what build/staging copies into the small system
# that runs after the restart (modprobe, ntfsresize, ntfsinfo, e2fsck,
# resize2fs, with the libraries `ldd` finds for them), the DejaVu fonts, and
# the three boot files the installer carries (Linux, GRUB, shim). The exact
# versions of a given release are in the sources release (PACKAGES.txt); a
# license does not change between two builds of one version series.
set -eu
PKGS="linux-image-generic grub-common grub-efi-amd64-signed shim-signed kmod ntfs-3g libntfs-3g89t64 e2fsprogs libext2fs2t64 libcom-err2 libblkid1 libuuid1 libc6 libssl3t64 libzstd1 liblzma5 fonts-dejavu-core"
echo "# Copyright files of the programs inside AurOS-Setup.exe's small system and boot files"
echo "# (taken from Ubuntu 24.04 LTS; the licenses they name are in the full texts that follow)."
seen=""
for p in $PKGS; do
    f=/usr/share/doc/$p/copyright
    label=$p
    # linux-image-generic is a metapackage: its image package carries the kernel's copyright
    if [ ! -e "$f" ] && [ "$p" = linux-image-generic ]; then
        k=$(ls -d /usr/share/doc/linux-image-[0-9]* 2>/dev/null | sort -V | tail -1)
        [ -n "$k" ] && f=$k/copyright
        label="linux (the kernel; package linux-image-*)"
    fi
    [ -e "$f" ] || { echo "gather-copyrights: no copyright file for $p" >&2; continue; }
    printf '\n==================================================================\n%s\n==================================================================\n\n' "$label"
    sum=$(sha256sum "$f" | cut -d' ' -f1)
    # several packages of one source share one copyright file: say so once
    same=$(printf '%s\n' "$seen" | awk -v s="$sum" '$1 == s { print $2; exit }')
    if [ -n "$same" ]; then
        echo "(the same copyright file as $same)"
    else
        cat "$f"
        seen="$seen
$sum $p"
    fi
done
