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
| \`auros-source-*.tar.gz\` | AurOS's own code, at the commit the image was built from (MIT license). |
| \`NOTICES.txt\` | The license texts (GPL 2 and 3, LGPL 2.1, MPL 2.0, Apache 2.0, Unicode), the notices that travel with AurOS's programs, and the copyright files of the programs inside the installer. |
| \`firefox-*.source.tar.xz\` | A copy of Mozilla's source code of the Firefox in this release, checked against Mozilla's own SHA-256 (Mozilla also publishes it). |

This release is checked after it is published: every file is downloaded again and every program in the image must have its source here (\`build/sources verify\`). It is never deleted.

---

EOF
    sed "s|@AUROS_RELEASES@|$rel|g" "$offer"
    ;;
line)
    tag=${2:?TAG}
    cat <<EOF
Licenses and the complete source code for this release: $rel/tag/sources-$tag . Our written offer of source is in the notes of that release, on the website's Licenses page and, on an installed computer, in /usr/share/doc/auros/SOURCE-OFFER.
EOF
    ;;
contact-in-releases)
    # After the owner has a mailbox (python3 -I tools/legal/check.py set-contact ADDRESS): the releases already
    # published carry the placeholder in their notes. Replace it in every release's notes (gh must be logged in).
    # The NOTICES.txt asset of a sources release is only refreshed by `[sources TAG RUN-ID again]` while the build run's
    # artifacts live (14 days); older ones keep the placeholder in that one file and say so in their notes.
    addr=${2:?ADDRESS}; dry=${3:-}
    case "$addr" in *[!A-Za-z0-9._%+@-]*|*@*@*|@*|*@|"") echo "notes.sh: not an address" >&2; exit 1 ;; esac
    repo=$(sh "$root/tools/repo.sh" repo)
    for tag in $(gh release list -R "$repo" --limit 200 --json tagName --jq '.[].tagName'); do
        body=$(gh release view "$tag" -R "$repo" --json body --jq .body)
        case "$body" in *CONTACT_EMAIL*) ;; *) continue ;; esac
        echo "$tag: CONTACT_EMAIL -> $addr"
        [ "$dry" = --dry-run ] || printf '%s\n' "$body" | sed "s|CONTACT_EMAIL|$addr|g" | gh release edit "$tag" -R "$repo" --notes-file - >/dev/null
    done
    ;;
*) echo "usage: sh tools/legal/notes.sh sources TAG COMMIT RUN_ID | line TAG | contact-in-releases ADDRESS [--dry-run]" >&2; exit 2 ;;
esac
