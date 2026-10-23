#!/bin/sh
# Build a finished handover volume (as a folder) from a made-up AurOS home
# full of awkward names, for testing anything that consumes one. The
# fixture the Windows side's checks are run against:
#
#   sh tests/mkhandover.sh OUTDIR       -> OUTDIR/AurOS-handover/...
#
# Uses the real ferry-handover; needs ferry-copy built (make).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
FERRY=$(dirname "$HERE")
OUT=${1:?usage: mkhandover.sh OUTDIR}
export PATH="$FERRY:$FERRY/build:$PATH" NO_COLOR=1
T=$(mktemp -d "${TMPDIR:-/tmp}/mkhandover.XXXXXX")
trap 'rm -rf "$T"' EXIT INT TERM HUP
H="$T/home"; mkdir -p "$H/Pictures/2024" "$H/Documents" "$H/Music" "$H/Desktop"
head -c 700000 /dev/urandom > "$H/Pictures/2024/holiday.jpg"
echo "my letter"          > "$H/Documents/letter.txt"
echo UPPER > "$H/Documents/Report.docx"; echo lower > "$H/Documents/report.docx"
echo 1 > "$H/Documents/a:b.txt"; echo 2 > "$H/Documents/CON.txt"; echo 3 > "$H/Documents/trailing."
echo 4 > "$H/Documents/what?.txt"; echo 5 > "$H/Music/caf$(printf '\303\251') song.mp3"
echo 6 > "$H/Documents/$(printf 'raw\377byte.txt')"
: > "$H/Documents/empty file"
mkdir -p "$OUT"
FERRY_STATE="$T/state" ferry-handover --dir "$OUT" --home "$H" --owner "$(id -un)" >/dev/null 2>&1
ls "$OUT/AurOS-handover"
