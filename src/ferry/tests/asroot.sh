#!/bin/sh
# Run Ferry's suite as root, which is how Ferry really runs (it mounts a
# disk), in a throwaway container so nothing on the host is touched.
# The root-only checks -- root never writes into her home, nothing is
# written through a symlink she planted -- only run this way.
#
#   sh tests/asroot.sh          (from src/ferry; needs docker)
#   sh tests/asroot.sh mutants  (tests/mutants.sh as root, root mutants too)
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
IMG=ferry-test:1
docker image inspect "$IMG" >/dev/null 2>&1 || docker build -q -t "$IMG" - <<'DOCKER'
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc make libc6-dev python3 \
    && rm -rf /var/lib/apt/lists/*
DOCKER
WHAT=tests/run.sh
[ "${1:-}" = mutants ] && WHAT=tests/mutants.sh
exec docker run --rm --memory 2g -v "$HERE":/ferry:ro "$IMG" sh -c \
    "cp -r /ferry /tmp/f && cd /tmp/f && rm -rf build && make -s >/dev/null && sh $WHAT"
