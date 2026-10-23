#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  Her bookmarks in a REAL Firefox, not in a file we hope it reads.
#
#  Runs ferry-firefox as root (as Ferry runs), then starts Mozilla's
#  Firefox -- headless, as her, with AurOS's own policies.json -- and
#  reads its places.sqlite back:
#
#    1. Edge only, Firefox never started on AurOS: Firefox opens the
#       profile Ferry made and imports her Edge bar into its toolbar.
#    2. Edge only, but she had already opened Firefox on AurOS: Firefox
#       is switched to the profile from Windows and imports it.
#    3. A real Windows Firefox profile (made by case 1's Firefox): Linux
#       Firefox opens IT, not an empty new one (docs FERRY finding 6),
#       with her bookmarks, and no password file came.
#
#  Needs root, python3 and firefox. tests/infirefox.sh runs it in a
#  container that has them (Firefox from packages.mozilla.org).
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
sh "$HERE/mkfakewin.sh" "$T/win" >/dev/null
E="$T/edge"; cp -r "$T/win" "$E"; rm -rf "$E/users/Bob/AppData/Roaming/Mozilla"

newher() { # NAME -> home path
    id "$1" >/dev/null 2>&1 || useradd -m "$1"
    getent passwd "$1" | cut -d: -f6
}
# Start Firefox as her, headless, let it settle, close it.
firefox_as() { # USER HOME SECONDS
    setpriv --reuid="$(id -u "$1")" --regid="$(id -g "$1")" --clear-groups -- \
        env HOME="$2" MOZ_HEADLESS=1 firefox --headless --no-remote about:blank \
        >"$T/ff-$1.log" 2>&1 &
    _p=$!
    sleep "$3"
    kill "$_p" 2>/dev/null
    for _i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$_p" 2>/dev/null || break; sleep 1; done
    kill -9 "$_p" 2>/dev/null; wait "$_p" 2>/dev/null
    pkill -9 -u "$1" 2>/dev/null; sleep 1
}
# What Firefox has as bookmarks in a profile: "folder<TAB>url" lines.
marks() {
    python3 - "$1/places.sqlite" <<'PY'
import sqlite3, sys
db = sqlite3.connect("file:%s?mode=ro" % sys.argv[1], uri=True)
q = """SELECT f.guid, p.url FROM moz_bookmarks b JOIN moz_places p ON b.fk = p.id
       JOIN moz_bookmarks f ON b.parent = f.id"""
for guid, url in db.execute(q):
    print("%s\t%s" % (guid, url))
PY
}
run_ferry() { # WINDOWS HOME USER
    FERRY_STATE="$T/state-$3" ferry-firefox --root "$1" --home "$2" --owner "$3" >"$T/ferry-$3.log" 2>&1
}

echo "1. Edge only; Firefox never opened on AurOS:"
H1=$(newher ffone)
run_ferry "$E" "$H1" ffone
firefox_as ffone "$H1" 25
P1="$H1/.mozilla/firefox/fromwindows.default-release"
marks "$P1" > "$T/m1" 2>"$T/m1.err"
check "Firefox opened the profile Ferry made" test -f "$P1/compatibility.ini"
check "her Edge bar is Firefox's toolbar" \
    grep -qx 'toolbar_____	https://photos.example.org/family?a=1&b=2' "$T/m1"
check "...folders inside it came" grep -q 'https://savings.example.com/' "$T/m1"
check "...her 'Other favorites' came" grep -q 'https://recipes.example.net/' "$T/m1"
check "...her Chrome bookmarks came too" grep -q 'https://news.example.com/' "$T/m1"
if grep -q 'javascript:' "$T/m1"; then bad "no bookmarklet came"; else ok "no bookmarklet came"; fi
[ -s "$T/m1" ] || { echo "  --- places:"; cat "$T/m1.err"; tail -5 "$T/ff-ffone.log"; }

echo "2. Edge only; she had opened Firefox on AurOS first:"
H2=$(newher fftwo)
firefox_as fftwo "$H2" 15
check "(Firefox made its own profile and claimed it)" grep -q '^\[Install' "$H2/.mozilla/firefox/profiles.ini"
run_ferry "$E" "$H2" fftwo
firefox_as fftwo "$H2" 25
P2="$H2/.mozilla/firefox/fromwindows.default-release"
marks "$P2" > "$T/m2" 2>/dev/null
check "Firefox now opens the profile from Windows" test -f "$P2/compatibility.ini"
check "...with her Edge bookmarks in it" grep -q 'https://photos.example.org/' "$T/m2"
check "...and her AurOS profile is still there" test -f "$H2/.mozilla/firefox/profiles.ini.before-windows"

echo "3. A real Windows Firefox profile:"
# Case 1's profile is a real one Firefox wrote; it plays her Windows one.
W3="$T/w3"; cp -r "$T/win" "$W3"
WP="$W3/users/Bob/AppData/Roaming/Mozilla/Firefox/Profiles/a1b2c3d4.default-release"
rm -rf "$WP"; cp -r "$P1" "$WP"; rm -f "$WP/lock" "$WP/.parentlock" "$WP/compatibility.ini"
echo '{"logins":[{"hostname":"https://bank.example"}]}' > "$WP/logins.json"
H3=$(newher ffthree)
firefox_as ffthree "$H3" 15
run_ferry "$W3" "$H3" ffthree
firefox_as ffthree "$H3" 25
P3="$H3/.mozilla/firefox/Profiles/a1b2c3d4.default-release"
marks "$P3" > "$T/m3" 2>/dev/null
check "Linux Firefox opened her Windows profile, not a new empty one" test -f "$P3/compatibility.ini"
check "...her bookmarks are there" grep -q 'https://photos.example.org/' "$T/m3"
if [ -e "$P3/logins.json" ]; then bad "no saved password came"; else ok "no saved password came"; fi

echo
printf 'real Firefox tests: %d passed, %d failed (%s)\n' "$pass" "$fail" "$(firefox --version 2>/dev/null)"
[ "$fail" -eq 0 ]
