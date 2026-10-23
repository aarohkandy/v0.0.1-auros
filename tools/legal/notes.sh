#!/bin/sh
# Release-note text that carries the written offer of source, from the one
# file that holds it (rootfs/usr/share/doc/auros/SOURCE-OFFER), so the
# image, the sources release and the AurOS release say the same words.
#
#   sh tools/legal/notes.sh sources TAG COMMIT RUN_ID
#       the body of the release `sources-TAG`
#   sh tools/legal/notes.sh line TAG
#       the two lines the AurOS release `TAG` adds to its own notes
#
# Both ask tools/repo.sh where the releases are (never typed) and leave
# CONTACT_EMAIL as it stands in the tree: `python3 -I tools/legal/check.py set-contact
# ADDRESS` replaces it everywhere at once when the owner has a mailbox.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
offer="$root/rootfs/usr/share/doc/auros/SOURCE-OFFER"
[ -f "$offer" ] || { echo "notes.sh: no $offer" >&2; exit 1; }
rel=$(sh "$root/tools/repo.sh" releases)
case "${1:-}" in
sources)
    tag=${2:?TAG}; commit=${3:?COMMIT}; run=${4:?RUN_ID}
    repo=$(sh "$root/tools/repo.sh" repo)
    cat <<EOF
The source code of AurOS $tag: the Ubuntu source packages in the exact versions that are in the image, a pointer to Firefox's source, and AurOS's own code. Built from \`$commit\` by [Actions run $run](https://github.com/$repo/actions/runs/$run).

**What is here**

| File | What it is |
|---|---|
| \`sources-$tag-<n>of<N>.tar\` (as many parts as the names say) | The source packages. Each part unpacks on its own: \`tar -xf <part>\` makes \`sources/<program>/<files>\`. A program's \`.dsc\` file names the others it needs. |
| \`SOURCES.txt\` | Every program, its version, which part holds it, and the address of Firefox's source. |
| \`PACKAGES.txt\` | Every program in the image, and the source package it was built from. |
| \`SHA256SUMS\` | \`sha256sum -c SHA256SUMS\` checks every file here. |
| \`auros-source-*.tar.gz\` | AurOS's own code, at the commit the image was built from (MIT licence). |

This release is checked after it is published: every file is downloaded again and every program in the image must have its source here (\`build/sources verify\`). It is never deleted.

---

EOF
    sed "s|@AUROS_RELEASES@|$rel|g" "$offer"
    ;;
line)
    tag=${2:?TAG}
    cat <<EOF
Licences and the complete source code for this release: $rel/tag/sources-$tag . Our written offer of source is on the website's Licences page and, on an installed computer, in /usr/share/doc/auros/SOURCE-OFFER.
EOF
    ;;
*) echo "usage: sh tools/legal/notes.sh sources TAG COMMIT RUN_ID | line TAG" >&2; exit 2 ;;
esac
