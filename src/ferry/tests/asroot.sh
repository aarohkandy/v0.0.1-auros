#!/bin/sh
# Run Ferry's suite as root, which is how Ferry really runs (it mounts a
# disk), in a throwaway container so nothing on the host is touched.
# The root-only checks -- root never writes into her home, nothing is
# written through a symlink she planted -- only run this way.
#
#   sh tests/asroot.sh          (from src/ferry; needs docker)
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
IMG=ferry-test:1
docker image inspect "$IMG" >/dev/null 2>&1 || docker build -q -t "$IMG" - <<'DOCKER'
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc make libc6-dev python3 \
    && rm -rf /var/lib/apt/lists/*
DOCKER
exec docker run --rm --memory 2g -v "$HERE":/ferry:ro "$IMG" sh -c \
    'cp -r /ferry /tmp/f && cd /tmp/f && make -s >/dev/null && sh tests/run.sh'
