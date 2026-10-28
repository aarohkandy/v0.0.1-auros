#!/bin/sh
# regen.sh -- remake hw/compat.json from an Ubuntu 24.04 tree that has the
# image's hardware packages installed. For a host WITH DOCKER (homebase).
#
#   sh tools/hw/regen.sh                 fresh container, ~2 GB for the few minutes it runs, removed after
#   sh tools/hw/regen.sh --reuse NAME    a container that already has them (saves the download)
#
# Which packages: `packages_hardware` in profiles/desktop.profile, plus the
# kernel the image gets (linux-image-generic). On CI the unit job has the real
# rootfs instead: `python3 -I tools/hw/gen-compat.py work/forge/desktop/rootfs`.
# Disk rule (docs/handoff/FIELD-MANUAL.md section 3): refuses when fewer than
# 30 GB would be left.
set -eu
cd "$(dirname "$0")/../.."
REUSE=""
[ "${1:-}" = "--reuse" ] && REUSE="${2:?--reuse needs a container name}"

if [ -z "$REUSE" ]; then
    free_gb=$(df -k / | awk 'NR==2 {printf "%d", $4/1048576}')
    [ "$free_gb" -ge 33 ] || { echo "regen: only $free_gb GB free; the rule is to keep 30 GB" >&2; exit 1; }
    NAME=hw-regen-$$
    docker run -d --name "$NAME" --memory 4g ubuntu:24.04 sleep 3600 >/dev/null
    trap 'docker rm -f "$NAME" >/dev/null 2>&1' EXIT INT TERM
    PK=$(sh -c '. ./profiles/desktop.profile; echo $packages_hardware')
    docker exec "$NAME" sh -c "export DEBIAN_FRONTEND=noninteractive; \
        for i in 1 2 3; do timeout 300 apt-get update -q && break; done; \
        timeout 1500 apt-get install -y -q --no-install-recommends $PK kmod python3-minimal ca-certificates" >/dev/null
else
    NAME="$REUSE"
fi

docker exec "$NAME" sh -c 'rm -rf /regen && mkdir -p /regen/tools /regen/hw'
docker cp tools/hw "$NAME":/regen/tools/hw
docker cp hw/seeds.json "$NAME":/regen/hw/seeds.json
docker exec "$NAME" python3 -I /regen/tools/hw/gen-compat.py / -o /regen/compat.json
docker cp "$NAME":/regen/compat.json hw/compat.json
docker exec "$NAME" rm -rf /regen
git diff --stat hw/compat.json || true
echo "regen: hw/compat.json is made from $(grep -o '"kernel": "[^"]*"' hw/compat.json | head -1)"
