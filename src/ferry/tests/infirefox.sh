#!/bin/sh
# Run tests/realfirefox.sh in a throwaway container with Mozilla's own
# Firefox (packages.mozilla.org) and AurOS's Firefox policies, as the
# image ships them.
#
#   sh tests/infirefox.sh       (from src/ferry; needs docker and network)
set -eu
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
IMG=ferry-firefox:1
docker image inspect "$IMG" >/dev/null 2>&1 || docker build -q -t "$IMG" - <<'DOCKER'
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates wget \
      python3 gcc make libc6-dev procps \
 && install -d -m 0755 /etc/apt/keyrings \
 && wget -q https://packages.mozilla.org/apt/repo-signing-key.gpg \
      -O /etc/apt/keyrings/packages.mozilla.org.asc \
 && echo "deb [signed-by=/etc/apt/keyrings/packages.mozilla.org.asc] https://packages.mozilla.org/apt mozilla main" \
      > /etc/apt/sources.list.d/mozilla.list \
 && printf 'Package: *\nPin: origin packages.mozilla.org\nPin-Priority: 1000\n' \
      > /etc/apt/preferences.d/mozilla \
 && apt-get update && apt-get install -y --no-install-recommends firefox \
 && rm -rf /var/lib/apt/lists/*
DOCKER
exec docker run --rm --memory 3g --shm-size 1g -v "$REPO":/repo:ro "$IMG" sh -c '
    mkdir -p /etc/firefox/policies &&
    cp /repo/rootfs/etc/firefox/policies/policies.json /etc/firefox/policies/ &&
    cp -r /repo/src/ferry /tmp/f && cd /tmp/f && rm -rf build && make -s >/dev/null &&
    sh tests/realfirefox.sh'
