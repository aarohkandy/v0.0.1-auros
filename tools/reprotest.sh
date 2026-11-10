#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  reprotest -- is AurOS-Setup.exe the same bytes every time it is built?
#
#  The PE linker writes the link time into the header unless told not to
#  (-Wl,--no-insert-timestamp, build/cflags and build/aurbridge-stub), so
#  two builds of one source differed in every .exe. A build nobody can
#  reproduce is a build nobody can check: "the file I downloaded is the
#  file the source makes" needs the second half to be repeatable.
#
#    sh tools/reprotest.sh              build twice, same bytes or red
#    sh tools/reprotest.sh --selftest   and prove it goes red: build with
#                                       the flag taken out, bytes must differ
#
#  It builds build/aurbridge in two clean copies of the tree at DIFFERENT
#  paths (so an absolute path leaking into the file shows up too), with
#  a stand-in staging environment (small fixed files for the kernel and
#  the initramfs, so the resource path is exercised without the real
#  100 MB). Needs mingw-w64, as build/aurbridge does.
# ═══════════════════════════════════════════════════════════════════
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
MODE=${1:-}
T=$(mktemp -d "${TMPDIR:-/tmp}/auros-repro.XXXXXX")
trap 'rm -rf "$T"' EXIT HUP INT TERM

build_one() { # name  strip-flag(0|1)
    d="$T/$1/deeper-$1/auros"
    mkdir -p "$d"
    tar -C "$ROOT" --exclude=./.git --exclude=./out --exclude=./work -cf - . | tar -C "$d" -xf -
    if [ "$2" = 1 ]; then
        sed 's/,--no-insert-timestamp//' "$d/build/cflags" > "$d/build/cflags.new" && mv "$d/build/cflags.new" "$d/build/cflags"
        sed 's/,--no-insert-timestamp//' "$d/build/aurbridge-stub" > "$d/build/stub.new" && mv "$d/build/stub.new" "$d/build/aurbridge-stub"
        grep -q ',--no-insert-timestamp' "$d/build/cflags" "$d/build/aurbridge-stub" && { echo "reprotest: mutant strip failed" >&2; exit 1; }
    fi
    mkdir -p "$d/out"
    printf 'stand-in kernel\n' > "$d/out/auros-staging-vmlinuz"
    printf 'stand-in initramfs\n' > "$d/out/auros-staging.img"
    ( cd "$d" && env -u AUROS_STAGING_KERNEL -u AUROS_STAGING_INITRD -u AUROS_RELEASE AUROS_REPO="${AUROS_REPO:-aarohkandy/v0.0.1-auros}" \
          sh build/aurbridge >"$T/$1.log" 2>&1 ) \
        || { cat "$T/$1.log" >&2; echo "reprotest: build $1 failed" >&2; exit 1; }
    ( cd "$d/out" && for f in *.exe *.efi; do [ -f "$f" ] && sha256sum "$f"; done; true ) > "$T/$1.sums"
}

run() { # strip
    build_one a "$1"
    sleep 2    # a link-time stamp has one-second resolution
    build_one b "$1"
    [ -s "$T/a.sums" ] || { echo "reprotest: no .exe was built" >&2; exit 1; }
    grep -q aurbridge-setup.exe "$T/a.sums" || { echo "reprotest: AurOS-Setup stub was not built" >&2; exit 1; }
    cmp -s "$T/a.sums" "$T/b.sums"
}

if [ "$MODE" = --selftest ]; then
    if run 1; then echo "reprotest: FAIL, builds without --no-insert-timestamp came out identical; this test cannot see the problem" >&2; exit 1; fi
    echo "reprotest selftest ok: without the flag the builds differ"
    diff "$T/a.sums" "$T/b.sums" | grep '^[<>]' | head -6 || true
    exit 0
fi
if run 0; then
    echo "reprotest ok: two builds in different directories, same bytes:"
    sed 's/^/    /' "$T/a.sums"
else
    echo "reprotest: FAIL, two builds of one source differ:" >&2
    diff "$T/a.sums" "$T/b.sums" >&2 || true
    exit 1
fi
