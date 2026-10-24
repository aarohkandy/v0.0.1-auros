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
