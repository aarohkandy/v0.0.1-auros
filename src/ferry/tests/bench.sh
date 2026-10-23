#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  How long her files take: a large fixture through `ferry-files`.
#
#  FILES small files (4 KB, 50 to a folder, three folders deep: the
#  shape of a camera roll) plus BIG files of 20 MB, in a fake Windows
#  profile's Pictures. Prints seconds, files per second and MB/s, once
#  cold-ish (first run) and once for a re-run (everything already here).
#
#    sh tests/bench.sh [FILES] [BIG]       default 5000 10
#  Budget: 5,000 files in well under a minute on a laptop disk; the
#  shell copier this replaced took 131 s for the small files alone.
# ═══════════════════════════════════════════════════════════════════
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
FERRY=$(dirname "$HERE")
export PATH="$FERRY:$FERRY/build:$PATH" FERRY_HIVE_BACKEND=hivepeek NO_COLOR=1
N=${1:-5000} BIG=${2:-10}
T=$(mktemp -d "${TMPDIR:-/tmp}/ferrybench.XXXXXX")
trap 'rm -rf "$T"' EXIT
sh "$HERE/mkfakewin.sh" "$T/win" >/dev/null
python3 - "$T/win/users/Bob/Pictures/Camera" "$N" "$BIG" <<'PY'
import os, sys
root, n, big = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
blob = os.urandom(4096)
for i in range(n):
    d = os.path.join(root, "%03d" % (i // 2500), "%03d" % (i // 50 % 50))
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "IMG_%05d.jpg" % i), "wb") as f:
        f.write(blob)
os.makedirs(os.path.join(root, "video"), exist_ok=True)
chunk = os.urandom(1 << 20)
for i in range(big):
    with open(os.path.join(root, "video", "clip%02d.mp4" % i), "wb") as f:
        for _ in range(20):
            f.write(chunk)
PY
sync -f "$T" 2>/dev/null || true
run() {
    _s=$(date +%s.%N)
    FERRY_STATE="$T/state" ferry-files --root "$T/win" --home "$T/home" \
        --owner "$(id -un)" --only Pictures >/dev/null 2>&1 || true
    _e=$(date +%s.%N)
    awk -v s="$_s" -v e="$_e" -v n="$N" -v b="$BIG" -v what="$1" 'BEGIN {
        t = e - s; mb = (n * 4096 + b * 20 * 1048576) / 1048576
        printf "%-26s %6.2f s   %6.0f files/s   %6.1f MB/s   (%d small + %d x 20 MB)\n",
               what, t, n / t, mb / t, n, b }'
}
run "first run:"
sync -f "$T" 2>/dev/null || true
run "re-run (already here):"
_got=$(find "$T/home/Pictures/Camera" -type f | wc -l | tr -d ' ')
echo "files in her Pictures/Camera: $_got (want $((N + BIG)))"
[ "$_got" = $((N + BIG)) ]
