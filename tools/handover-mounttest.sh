#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

tmp=$(mktemp -d "${TMPDIR:-/tmp}/handover-mounttest.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror -Isrc/aurstage \
    -o "$tmp/handover-mounttest" tools/handover-mounttest.c
"$tmp/handover-mounttest"

grep -q 'stage_handover_set_cleanup(release_staging_mounts)' src/aurstage/main.c
grep -q 'if (stage_screen_stop() != 0) return -1;' src/aurstage/boot.c
grep -q 'if (forget_saved_scratch() != 0) goto handover_failed;' src/aurstage/boot.c

# answer.sh takes the handover only when [ -x /usr/lib/auros/handover.sh ];
# it was checked in 0644 from 2026-10-23, so every fast-path Put Windows
# back on a BitLocker PC refused (release run 41741946326). Every script
# the image carries as a program (under usr/) must keep its executable bit.
if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
    noexec=$(git ls-files -s rootfs/usr | awk '$1 == "100644" {print $4}' |
        while read -r f; do [ "$(head -c 2 "$f")" != '#!' ] || echo "$f"; done)
    [ -z "$noexec" ] || { echo "not executable in git, though a script: $noexec"; exit 1; }
    echo "every script under rootfs/usr is executable, handover.sh among them"
fi
