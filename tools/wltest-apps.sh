#!/bin/sh
# wltest-apps.sh -- put a real program on the real shell and look at the result.
#
#   sh tools/wltest-apps.sh firefox|gtk4|qt|mpv [seconds]     (Linux with docker)
#
# Writes wltest-apps-out/<app>.png. Needs the auros-wl image (built from
# tools/wltest-apps/Dockerfile on first use; it starts from ferry-firefox:1).
# Run it on homebase, in a directory under ~/work/auros/<you>/ (FIELD-MANUAL §3).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT="${OUT:-$PWD/wltest-apps-out}"
mkdir -p "$OUT"
docker image inspect auros-wl:dev >/dev/null 2>&1 || docker build -q -t auros-wl:dev "$HERE/wltest-apps"
exec docker run --rm --memory 6g --shm-size 1g --security-opt seccomp=unconfined \
    -v "$ROOT":/w:ro -v "$OUT":/out -e SHELLF -e REBUILD -e MOUSE -e TEXT_SCALE -e PROBE_POINTS -e DND_FROM -e DND_TO auros-wl:dev sh /w/tools/wltest-apps/inner.sh "$@"
