#!/bin/sh
# sh tools/apt.sh PACKAGE...   (CI runners only: it uses sudo)
#
# apt-get update + install, every call bounded (timeout 300) and the
# whole thing tried 3 times. Twice on 2026-10-23 an unbounded
# `apt-get update` held a runner until the run was cancelled by hand;
# bounded, a stuck mirror costs minutes, not the run. A try that was
# killed mid-install can leave dpkg half-configured, so the next try
# finishes that first. A download that stalls (2026-10-23: the Azure
# mirror stopped answering in the middle of a 271 MB install, for ten
# minutes) is cut by apt itself after 30 s without data and retried,
# long before the 300 s bound.
#
# And the mirror itself: on 2026-10-23 azure.archive.ubuntu.com gave
# 40-200 KB/s for an hour (three tries of 5 minutes each moved 70 MB of a
# 278 MB install) while archive.ubuntu.com, which debootstrap in the same
# job used, gave 28 MB/s. So from the second try on, the runner's mirror
# list drops the Azure mirror (or, if it lists only that one, points it at
# archive.ubuntu.com). Partial downloads are kept, so no try starts over.
# The better fix is to ask for less: install only what is used (the 64-bit
# mingw compiler, not the 278 MB mingw-w64 package).
mirror_fallback() {
    f=/etc/apt/apt-mirrors.txt
    [ -f "$f" ] || return 0
    if grep -qv 'azure\.archive' "$f"; then
        sudo sed -i '/azure\.archive/d' "$f"
    else
        sudo sed -i 's|http://azure\.archive\.ubuntu\.com|http://archive.ubuntu.com|' "$f"
    fi
    echo "::warning::apt: the Azure mirror is too slow; using archive.ubuntu.com" >&2
}

for i in 1 2 3; do
    [ "$i" = 1 ] || mirror_fallback
    sudo timeout 300 apt-get update -q -o Acquire::Retries=3 -o Acquire::http::Timeout=30 &&
        sudo DEBIAN_FRONTEND=noninteractive timeout 300 \
            apt-get install -y -q --no-install-recommends \
            -o Acquire::Retries=3 -o Acquire::http::Timeout=30 "$@" && exit 0
    echo "::warning::apt try $i of 3 did not finish; trying again" >&2
    sleep 10
    sudo DEBIAN_FRONTEND=noninteractive timeout 300 dpkg --configure -a || true
done
echo "::error::apt-get did not finish in 3 tries: $*" >&2
exit 1
