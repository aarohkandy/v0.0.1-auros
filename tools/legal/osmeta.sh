#!/bin/sh
# osmeta -- what the source release of a CUSTOM OS reads from its image (make-os.yml).
#
#   sh tools/legal/osmeta.sh WORK STAGING-IMG OUTDIR
#
#   WORK         the build's work tree, work/forge/<profile>: it holds rootfs/, the tree the
#                image was copied from. Called right after build/make-os and BEFORE the
#                proofs, because nosticktest takes a package out of that tree.
#   STAGING-IMG  out/auros-staging.img (the installer's small system)
#   OUTDIR       gets rootfs.tar.zst, host-sources.txt, auros-staging.img, packages.count
#
# sources.yml makes the source release of a desktop build from build.yml's artifacts
# auros-rootfs (a tar of the whole rootfs; it reads a handful of files out of it) and
# auros-installer (host-sources.txt, auros-staging.img). A custom OS has no such
# artifacts, so this writes the same shapes with only the files sources.yml reads:
# the dpkg database (the list of what the image holds, and so of what source is owed),
# the file lists of its packages (the check of the installer's small system), the
# image's own name and browser records. Under other artifact names on purpose:
# stagekvm.yml and tests.yml pick auros-rootfs and auros-installer by name from the
# newest run of ANY workflow, and this partial copy must never be picked for a test.
#
# It refuses (a source offer made from nothing is worse than none): no dpkg database,
# no installed package, no file list, no staging image, or no record of the glibc of
# the machine that links the installer's tools.
set -eu
work=${1:?work tree (work/forge/PROFILE)}; staging=${2:?staging image}; out=${3:?output directory}
fail() { echo "osmeta: $*" >&2; exit 1; }
rfs="$work/rootfs"
[ -s "$rfs/var/lib/dpkg/status" ] || fail "no dpkg database in $rfs"
n=$(grep -c '^Status: install ok installed' "$rfs/var/lib/dpkg/status" || true)
[ "$n" -gt 0 ] || fail "the dpkg database in $rfs holds no installed package"
[ -d "$rfs/var/lib/dpkg/info" ] && [ -n "$(find "$rfs/var/lib/dpkg/info" -name '*.list' | head -1)" ] \
    || fail "no package file lists in $rfs/var/lib/dpkg/info"
[ -s "$staging" ] || fail "no staging image $staging"
host=$(dpkg-query -W -f='${source:Package} ${source:Version}\n' libc6-dev | sort -u)
[ -n "$host" ] || fail "cannot tell which glibc this machine links the installer's tools with"

mkdir -p "$out"
list="$out/.files"
{
    echo rootfs/var/lib/dpkg/status
    ( cd "$work" && find rootfs/var/lib/dpkg/info -name '*.list' )
    for f in etc/auros/build-warnings opt/firefox/application.ini usr/lib/os-release etc/issue etc/lsb-release; do
        if [ -e "$rfs/$f" ] || [ -L "$rfs/$f" ]; then echo "rootfs/$f"; fi
    done
} > "$list"
tar -C "$work" -cf "$out/rootfs.tar" -T "$list"
zstd -q -3 -f "$out/rootfs.tar" -o "$out/rootfs.tar.zst"
rm -f "$out/rootfs.tar" "$list"
printf '%s\n' "$host" > "$out/host-sources.txt"
cp "$staging" "$out/auros-staging.img"
echo "$n" > "$out/packages.count"
echo "osmeta: $n packages installed in the image, $(ls "$out" | wc -l | tr -d ' ') files in $out"
