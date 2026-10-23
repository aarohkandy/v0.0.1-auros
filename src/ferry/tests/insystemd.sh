#!/bin/sh
# Run tests/sandbox.sh where it can run: as root, with systemd as PID 1,
# loop devices and FUSE -- a throwaway privileged container with its
# own cgroup namespace (its systemd never sees the host's).
#
#   sh tests/insystemd.sh       (from src/ferry; needs docker)
set -eu
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
IMG=ferry-systemd:4
docker image inspect "$IMG" >/dev/null 2>&1 || docker build -q -t "$IMG" - <<'DOCKER'
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends systemd systemd-sysv \
    dbus ntfs-3g dosfstools exfatprogs gcc make libc6-dev python3 && rm -rf /var/lib/apt/lists/*
DOCKER
C=$(docker run -d --privileged --cgroupns=private --tmpfs /run --tmpfs /run/lock \
    --memory 2g -v "$REPO":/repo:ro "$IMG" /sbin/init)
trap 'docker rm -f "$C" >/dev/null 2>&1' EXIT INT TERM
i=0
until docker exec "$C" systemctl is-system-running 2>/dev/null | grep -qE 'running|degraded'; do
    i=$((i+1)); [ "$i" -lt 60 ] || { echo "systemd did not come up"; exit 2; }; sleep 1
done
# UNIT=path (relative to the repository) tests another unit file.
docker exec -e UNIT="${UNIT:+/tmp/r/$UNIT}" "$C" sh -c 'cp -r /repo /tmp/r && cd /tmp/r/src/ferry &&
    rm -rf build && make -s >/dev/null && sh tests/sandbox.sh'
