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
IMAGE=auros-wl:dev; EXTRA=""
if [ "${1#flatpak}" != "${1:-}" ] || [ -n "${FPIMAGE:-}" ]; then
    # Flatpak needs bubblewrap's namespaces; the downloads (hundreds of MB) live in FPDIR, outside the work tree.
    # FPIMAGE=1 also gives any other mode (the store, say) this image.
    IMAGE=auros-wl-fp:dev
    docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -q -t "$IMAGE" -f "$HERE/wltest-apps/Dockerfile.flatpak" "$HERE/wltest-apps"
    FPDIR="${FPDIR:-$HOME/work/auros/wl-flatpak}"; mkdir -p "$FPDIR"
    EXTRA="--cap-add SYS_ADMIN --cap-add NET_ADMIN --security-opt apparmor=unconfined --security-opt systempaths=unconfined -v $FPDIR:/fp -e FLATPAK_SYSTEM_DIR=/fp -e FLATPAK_BWRAP=/w/tools/wltest-apps/bwrap-nonroot"
fi
# NET=none: a container with no network at all (tools/storeoffline.sh)
[ -n "${NET:-}" ] && EXTRA="$EXTRA --network $NET"
# shellcheck disable=SC2086
exec docker run --rm --memory 6g --shm-size 1g --security-opt seccomp=unconfined $EXTRA \
    -v "$ROOT":/w:ro -v "$OUT":/out -e APPID -e FPREF -e FPARGS -e FPAPPARGS -e CLICKS -e SCLICKS -e STYPE -e WAYLAND_DEBUG -e SHELLF -e REBUILD -e AUROS_XWM_DEBUG -e MOUSE -e XCMD -e KEYS -e TAIL -e TEXT_SCALE -e PROBE_POINTS -e DND_FROM -e DND_TO "$IMAGE" sh /w/tools/wltest-apps/inner.sh "$@"
