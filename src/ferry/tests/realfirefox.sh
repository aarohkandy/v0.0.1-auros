#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  Her bookmarks in a REAL Firefox, not in a file we hope it reads.
#
#  Runs ferry-firefox as root (as Ferry runs it), then starts Mozilla's
#  Firefox -- headless, as her -- and reads its places.sqlite back:
#
#    1. Edge only, Firefox never started on AurOS: Firefox opens the
#       profile Ferry made (the [Install<hash>] it was told, hash from
#       ferry-ffhash) and imports her Edge bar into its toolbar.
#    2. A real Windows Firefox profile (one Firefox itself wrote): Linux
#       Firefox opens IT, not a new empty one, with her bookmarks, and
#       no password file came. This is the case a copied Windows
#       profiles.ini used to lose.
#    3. She had already used Firefox here: it is left alone, her own
#       profile still opens.
#
#  Needs root and Mozilla's firefox (what AurOS ships). tests/infirefox.sh
#  runs it in a container that has them. Firefox 147+ keeps its profiles
#  in ~/.config/mozilla/firefox; older ones in ~/.mozilla/firefox; with
#  only the old folder present Firefox 158 uses it (this test checks).
# ═══════════════════════════════════════════════════════════════════
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FERRY=$(dirname "$HERE")
export PATH="$FERRY:$FERRY/build:$PATH" FERRY_HIVE_BACKEND=hivepeek NO_COLOR=1
pass=0; fail=0
ok()  { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad() { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; }
check() { _l=$1; shift; if "$@" >/dev/null 2>&1; then ok "$_l"; else bad "$_l"; fi; }

[ "$(id -u)" = 0 ] && command -v firefox >/dev/null || { echo "realfirefox.sh: needs root and firefox"; exit 2; }
T=$(mktemp -d /tmp/realff.XXXXXX); chmod 0755 "$T"
trap 'rm -rf "$T"' EXIT
trap 'rm -rf "$T"; exit 130' INT TERM HUP PIPE
sh "$HERE/mkfakewin.sh" "$T/win" >/dev/null
E="$T/edge"; cp -r "$T/win" "$E"; rm -rf "$E/users/Bob/AppData/Roaming/Mozilla"

newher() { id "$1" >/dev/null 2>&1 || useradd -m "$1"; getent passwd "$1" | cut -d: -f6; }

# Start Firefox as her, headless, let it settle, close it. The flags
# are what a bare container needs; none changes where it looks for
# profiles or what it imports.
firefox_as() { # USER HOME SECONDS
    mkdir -p "/run/user/$(id -u "$1")"; chown "$1" "/run/user/$(id -u "$1")"; chmod 700 "/run/user/$(id -u "$1")"
    setpriv --reuid="$(id -u "$1")" --regid="$(id -g "$1")" --clear-groups -- \
        env HOME="$2" XDG_RUNTIME_DIR="/run/user/$(id -u "$1")" MOZ_HEADLESS=1 \
            MOZ_CRASHREPORTER_DISABLE=1 firefox --headless --no-remote about:blank \
        >"$T/ff-$1.log" 2>&1 &
    _p=$!
    sleep "$3"
    kill "$_p" 2>/dev/null; sleep 3
    pkill -9 -u "$1" 2>/dev/null; wait "$_p" 2>/dev/null; sleep 1
}
# "folder<TAB>url" for every bookmark a profile has (copied first: the
# live database has a write-ahead log).
marks() {
    _d=$(mktemp -d "$T/m.XXXXXX"); cp "$1/places.sqlite"* "$_d"/ 2>/dev/null || return 1
    python3 - "$_d/places.sqlite" <<'PY'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
q = """SELECT f.guid, p.url FROM moz_bookmarks b JOIN moz_places p ON b.fk = p.id
       JOIN moz_bookmarks f ON b.parent = f.id"""
for guid, url in db.execute(q):
    print("%s\t%s" % (guid, url))
PY
}
run_ferry() { # WINDOWS HOME USER
    FERRY_STATE="$T/state-$3" ferry-firefox --root "$1" --home "$2" --owner "$3" >"$T/ferry-$3.log" 2>&1
}
profile_with_places() { # HOME -> the profile folder holding the newest places.sqlite
    ls -t "$1"/.mozilla/firefox/*/places.sqlite "$1"/.config/mozilla/firefox/*/places.sqlite 2>/dev/null | head -1 | xargs -r dirname
}

H=$(ferry-ffhash "$(dirname "$(readlink -f "$(command -v firefox)")")")
echo "this Firefox is install $H ($(firefox --version 2>/dev/null))"

echo "1. Edge only; Firefox never opened on AurOS:"
H1=$(newher ffone)
run_ferry "$E" "$H1" ffone
firefox_as ffone "$H1" 30
P1="$H1/.mozilla/firefox/fromwindows.default-release"
marks "$P1" > "$T/m1" 2>"$T/m1.err"
check "Firefox opened the profile Ferry made" test -f "$P1/places.sqlite"
check "...not a new empty one beside it" sh -c "[ \"\$(ls -d '$H1'/.mozilla/firefox/*/places.sqlite | wc -l)\" = 1 ]"
check "her Edge bar is Firefox's toolbar" grep -qx 'toolbar_____	https://photos.example.org/family?a=1&b=2' "$T/m1"
check "...folders inside it came" grep -q 'https://savings.example.com/' "$T/m1"
check "...her 'Other favorites' came" grep -q 'https://recipes.example.net/' "$T/m1"
check "...her Chrome bookmarks came too" grep -q 'https://news.example.com/' "$T/m1"
if grep -q 'javascript:' "$T/m1"; then bad "no bookmarklet came"; else ok "no bookmarklet came"; fi
[ -s "$T/m1" ] || { echo "  --- Firefox said:"; head -5 "$T/m1.err"; tail -5 "$T/ff-ffone.log"; }

echo "2. A real Windows Firefox profile:"
# Case 1's profile is one Firefox itself wrote; it plays her Windows one.
W2="$T/w2"; cp -r "$T/win" "$W2"
WP="$W2/users/Bob/AppData/Roaming/Mozilla/Firefox/Profiles/a1b2c3d4.default-release"
rm -rf "$WP"; cp -r "$P1" "$WP"; rm -f "$WP/lock" "$WP/.parentlock" "$WP/compatibility.ini"
echo '{"logins":[{"hostname":"https://bank.example"}]}' > "$WP/logins.json"
H2=$(newher fftwo)
run_ferry "$W2" "$H2" fftwo
firefox_as fftwo "$H2" 30
P2="$H2/.mozilla/firefox/a1b2c3d4.default-release"
marks "$P2" > "$T/m2" 2>/dev/null
check "Linux Firefox opened her Windows profile, not a new empty one" test -f "$P2/compatibility.ini"
check "...her bookmarks are there" grep -q 'https://photos.example.org/' "$T/m2"
check "...and it is the only profile with a history" sh -c "[ \"\$(ls -d '$H2'/.mozilla/firefox/*/places.sqlite | wc -l)\" = 1 ]"
if [ -e "$P2/logins.json" ]; then bad "no saved password came"; else ok "no saved password came"; fi

echo "3. She had already used Firefox here:"
H3=$(newher ffthree)
firefox_as ffthree "$H3" 20
before=$(profile_with_places "$H3")
run_ferry "$E" "$H3" ffthree
firefox_as ffthree "$H3" 25
after=$(profile_with_places "$H3")
check "(Firefox made its own profile first)" test -n "$before"
check "Firefox still opens her own profile" test "$before" = "$after"
check "...and her Edge bookmarks are waiting in her Home" test -s "$H3/Bookmarks from Edge.html"

echo
printf 'real Firefox tests: %d passed, %d failed (%s)\n' "$pass" "$fail" "$(firefox --version 2>/dev/null)"
[ "$fail" -eq 0 ]
