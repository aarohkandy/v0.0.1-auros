#!/bin/sh
# sh tools/wishesoff.sh ROOTFS     the image has no Wishes in it (D14)
# sh tools/wishesoff.sh selftest   each check fails when what it checks is wrong
#
# D14: Wishes are NOT in the v0.1 image. build/forge reads `wishes=` from
# the profile (anything but "on" is off), compiles the shell with
# -DAUROS_WISHES=0|1 (src/aurshell/aurfeatures.h) and writes
# /etc/auros/features. A release image must come out with, and this
# asks the finished rootfs rather than the script that made it:
#   - /etc/auros/features says wishes=off;
#   - no aurwish program or unit anywhere in the image (stream W1's
#     engine and its user service);
#   - the shell has no "Make a wish" door: its home tile's text is not in
#     the binary (the tile is behind AUROS_WISHES, so a shell built with
#     it off carries no copy of that sentence).
# When Wishes ship, set wishes="on" in the profile AND delete the call to
# this script in tests.yml, in that order, in the commit that says so.
set -u
cd "$(dirname "$0")/.."
CANARY='Tell this computer what to do'

check() { # ROOTFS -> prints one line per failure, exit 1 if any
    r=$1; n=0
    f=$r/etc/auros/features
    if [ ! -f "$f" ]; then echo "no /etc/auros/features (build/forge writes it)"; n=$((n+1))
    elif ! grep -qx 'wishes=off' "$f"; then echo "/etc/auros/features says: $(grep '^wishes=' "$f" | head -1)"; n=$((n+1)); fi
    found=$(find "$r/usr" "$r/etc" "$r/opt" -name 'aurwish*' 2>/dev/null | head -3)
    if [ -n "$found" ]; then echo "an aurwish file is in the image: $found"; n=$((n+1)); fi
    sh=$r/usr/bin/aurshell
    if [ -f "$sh" ] && grep -aqF "$CANARY" "$sh"; then
        echo "the shell carries the Make a wish tile (built with AUROS_WISHES=1)"; n=$((n+1)); fi
    [ "$n" = 0 ]
}

if [ "${1:-}" = selftest ]; then
    t=$(mktemp -d "${TMPDIR:-/tmp}/wishesoff.XXXXXX"); trap 'rm -rf "$t"' EXIT
    fresh() { rm -rf "$t/r"; mkdir -p "$t/r/etc/auros" "$t/r/usr/bin"
              echo 'wishes=off' > "$t/r/etc/auros/features"; echo 'a shell' > "$t/r/usr/bin/aurshell"; }
    fails=0
    expect() { # name, want (0 pass / 1 refuse)
        check "$t/r" >/dev/null 2>&1; got=$?
        if [ "$got" = "$2" ]; then printf '    %-58s ok\n' "$1"
        else printf '    %-58s FAIL\n' "$1"; fails=$((fails+1)); fi; }
    fresh; expect "a clean image passes" 0
    fresh; echo 'wishes=on' > "$t/r/etc/auros/features"; expect "wishes=on is refused" 1
    fresh; rm "$t/r/etc/auros/features"; expect "no features file is refused" 1
    fresh; : > "$t/r/usr/bin/aurwish"; expect "an aurwish program is refused" 1
    fresh; mkdir -p "$t/r/etc/systemd/user"; : > "$t/r/etc/systemd/user/aurwish.service"; expect "an aurwish unit is refused" 1
    fresh; echo "x $CANARY y" > "$t/r/usr/bin/aurshell"; expect "a shell with the Make a wish tile is refused" 1
    [ "$fails" = 0 ] && echo "wishesoff selftest: ok" || echo "wishesoff selftest: $fails wrong"
    exit "$fails"
fi

[ -d "${1:-}" ] || { echo "usage: sh tools/wishesoff.sh ROOTFS | selftest" >&2; exit 2; }
if out=$(check "$1"); then echo "wishesoff: ok (no Wishes in this image)"; exit 0; fi
echo "$out" >&2; echo "wishesoff: REFUSED: Wishes are not in the v0.1 image (D14)" >&2; exit 1
