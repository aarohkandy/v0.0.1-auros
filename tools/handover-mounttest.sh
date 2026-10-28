#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

tmp=$(mktemp -d "${TMPDIR:-/tmp}/handover-mounttest.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror -Isrc/aurstage \
    -o "$tmp/handover-mounttest" tools/handover-mounttest.c
"$tmp/handover-mounttest"
