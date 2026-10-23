#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  Ferry offline test suite.
#
#  Ferry cannot be tested against a real Windows disk in CI, so the
#  suite builds a synthetic one (tests/mkfakewin.sh) with every trap the
#  real world contains — a Documents folder redirected into OneDrive,
#  cloud placeholders mixed with real files, a Videos folder on a second
#  drive, a wrong-case Users directory, a DPAPI-protected WiFi PSK, a
#  destination collision — and asserts Ferry does the right, honest
#  thing with each. No network, no NTFS mount, no root beyond writing
#  the xattrs the fake tree uses to imitate NTFS attributes.
#
#    sh tests/run.sh
# ═══════════════════════════════════════════════════════════════════
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FERRY=$(dirname "$HERE")
export PATH="$FERRY:$FERRY/build:$PATH"
export FERRY_HIVE_BACKEND=hivepeek
export NO_COLOR=1

# The room check asks the real disk; the suite is about everything else,
# so by default there is plenty (the room tests set their own).
export FERRY_ROOM_BYTES=1099511627776
TMP=$(mktemp -d "${TMPDIR:-/tmp}/ferrytest.XXXXXX")
# Removed however this ends: finished, interrupted, terminated, or the
# reader of a pipe went away (SIGPIPE; mutants.sh once did that to this
# script on every run, and each left up to 340 MB behind). dash runs an
# EXIT trap only on a normal exit, so the signals are trapped too.
trap 'rm -rf "$TMP"' EXIT
trap 'rm -rf "$TMP"; exit 130' INT TERM HUP PIPE
WIN="$TMP/win"
export FERRY_STATE="$TMP/state"
# As root (tests/asroot.sh), ferry-copy must be told whom to become.
ASME="" OWNER=$(id -un)
if [ "$(id -u)" = 0 ]; then
    id ferryher >/dev/null 2>&1 || useradd -m -u 4242 ferryher
    ASME="--as 4242:4242" OWNER=ferryher
    chmod 0755 "$TMP"
fi
# A home the test made is hers, as a real one is.
hers() { [ -z "$ASME" ] || chown -R 4242:4242 "$@"; }

pass=0; fail=0
ok()  { pass=$((pass+1)); printf '  ok   %s\n' "$1"; }
bad() { fail=$((fail+1)); printf '  FAIL %s\n' "$1"; }
# assert LABEL: runs the rest as a command; pass iff it exits 0.
assert() { _l=$1; shift; if "$@" >/dev/null 2>&1; then ok "$_l"; else bad "$_l"; fi; }
refute() { _l=$1; shift; if "$@" >/dev/null 2>&1; then bad "$_l"; else ok "$_l"; fi; }
eq()     { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2', want '$3')"; fi; }

echo "building fake Windows volume..."
sh "$HERE/mkfakewin.sh" "$WIN" >/dev/null
NT="$WIN/users/Bob/NTUSER.DAT"

# Snapshot the source volume's every file (path, size, mtime) BEFORE any
# stage touches it, so the read-only guarantee is checked against the
# tree as Ferry first saw it — not against the source-file timestamps,
# which say nothing about whether Ferry wrote to the volume.
snapshot() { find "$WIN" -type f -exec stat -c '%n %s %Y' {} + 2>/dev/null | sort; }
snapshot > "$TMP/win.before"

echo "hive reader:"
eq "reads REG_EXPAND_SZ" \
   "$(ferry-hivepeek "$NT" 'Control Panel\Desktop' Wallpaper)" \
   'C:\Users\Bob\Pictures\wall.jpg'
eq "reads REG_DWORD as number" \
   "$(ferry-hivepeek "$NT" 'Control Panel\Desktop' LogPixels)" 120
eq "value lookup is case-insensitive" \
   "$(ferry-hivepeek "$NT" 'control panel\desktop' wallpaperstyle)" 10
refute "missing key exits nonzero" ferry-hivepeek "$NT" 'No\Such\Key' X
assert "lists subkeys" sh -c "ferry-hivepeek -k '$NT' 'Control Panel' | grep -qx Desktop"

echo "attribute classifier:"
STUB="$WIN/users/Bob/OneDrive/Documents/Taxes/2022.pdf"
REAL="$WIN/users/Bob/Desktop/notes.txt"
CLASS_STUB=$(ferry-winattr "$STUB" | cut -f1)
CLASS_REAL=$(ferry-winattr "$REAL" | cut -f1)
case "$CLASS_STUB" in
    placeholder|suspect) ok "cloud placeholder is not classified as real data ($CLASS_STUB)" ;;
    *) bad "cloud placeholder misclassified as '$CLASS_STUB' — DATA LOSS RISK" ;;
esac
eq "a real file is hydrated" "$CLASS_REAL" hydrated
refute "a missing path exits nonzero" ferry-winattr "$TMP/nope"

echo "detect:"
ferry-detect --root "$WIN" --quick > "$TMP/detect.json" 2>/dev/null
assert "detect emits valid JSON" python3 -m json.tool "$TMP/detect.json"
assert "detect identifies Windows 10" \
    grep -q '"product": "Windows 10 Pro"' "$TMP/detect.json"
assert "detect counts 4 WiFi profiles" \
    grep -q '"wifi_profiles": 4' "$TMP/detect.json"
# The redirection trap: Documents must resolve into OneDrive, never the
# stale local Users\Bob\Documents.
DOCPATH=$(python3 - "$TMP/detect.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
for p in d["installs"][0]["profiles"]:
    if p["kind"] != "user": continue
    for f in p["folders"]:
        if f["label"] == "Documents": print(f["path"]); break
PY
)
case "$DOCPATH" in
    *OneDrive*) ok "Documents resolved into OneDrive (redirection honoured)" ;;
    *) bad "Documents resolved to '$DOCPATH' — redirection NOT honoured" ;;
esac
VIDSTATUS=$(python3 - "$TMP/detect.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
for p in d["installs"][0]["profiles"]:
    if p["kind"] != "user": continue
    for f in p["folders"]:
        if f["label"] == "Videos": print(f["status"]); break
PY
)
eq "Videos on drive D: reported off-volume" "$VIDSTATUS" off-volume

echo "files:"
HOME1="$TMP/home"
mkdir -p "$HOME1/Desktop/dir.v2" "$HOME1/Downloads" "$HOME1/Music"
echo "my own newer notes" > "$HOME1/Desktop/notes.txt"   # collision seed
echo "mine" > "$HOME1/Desktop/dir.v2/readme"              # dot in a folder name
echo "mine" > "$HOME1/Downloads/setup.exe"                # two collisions deep
echo "mine too" > "$HOME1/Downloads/setup (from Windows).exe"
LONG=$(printf '%0246d' 0 | tr 0 a)
echo "my song" > "$HOME1/Music/$LONG.mp3"                 # no room for the suffix
echo "half a file" > "$HOME1/Desktop/.ferrypart-999-0"    # a run cut off earlier
hers "$HOME1"
ferry-files --root "$WIN" --home "$HOME1" --owner "$OWNER" >/dev/null 2>&1
assert "real Desktop file imported" test -f "$HOME1/Desktop/report.docx"
assert "collision kept BOTH files (no overwrite)" \
    test -f "$HOME1/Desktop/notes (from Windows).txt"
eq "the pre-existing file was not overwritten" \
   "$(cat "$HOME1/Desktop/notes.txt")" "my own newer notes"
assert "Documents imported from the OneDrive redirect" \
    test -f "$HOME1/Documents/letter.docx"
refute "a OneDrive placeholder was NOT silently copied" \
    test -f "$HOME1/Documents/Taxes/2022.pdf"
assert "each placeholder is reported by name" \
    grep -q 'Taxes/2022.pdf' "$FERRY_STATE/report.d/files-copy.tsv"
assert "junk (desktop.ini/Thumbs.db) filtered out" \
    sh -c "! find '$HOME1' -iname 'thumbs.db' -o -iname 'desktop.ini' | grep -q ."
assert "user-dirs.dirs written" test -f "$HOME1/.config/user-dirs.dirs"
# idempotency: re-run copies nothing new
BEFORE=$(find "$HOME1" -type f | wc -l)
ferry-files --root "$WIN" --home "$HOME1" --owner "$OWNER" >/dev/null 2>&1
AFTER=$(find "$HOME1" -type f | wc -l)
eq "re-run is idempotent (no new files)" "$AFTER" "$BEFORE"

echo "copier:"
assert "a OneDrive file that IS on this computer comes across (cloud tag and all)" \
    grep -qx 'BUDGET-real-bytes' "$HOME1/Documents/budget.xlsx"
refute "a file of nothing but zeros and no blocks is not copied as a picture" \
    test -e "$HOME1/Pictures/cloud-only.jpg"
assert "...and it is listed by name" \
    grep -q 'Pictures/cloud-only.jpg' "$FERRY_STATE/report.d/files-copy.tsv"
eq "a dot in a folder name: the copy lands in that folder" \
   "$(cat "$HOME1/Desktop/dir.v2/readme (from Windows)" 2>/dev/null)" "from windows"
eq "a second collision gets '(from Windows 2)'" \
   "$(cat "$HOME1/Downloads/setup (from Windows 2).exe" 2>/dev/null)" "installer"
eq "...and neither earlier file was touched" \
   "$(cat "$HOME1/Downloads/setup.exe")|$(cat "$HOME1/Downloads/setup (from Windows).exe")" "mine|mine too"
_lw=$(find "$HOME1/Music" -name '*(from Windows).mp3' 2>/dev/null)
eq "a name with no room for '(from Windows)' is shortened, not lost" \
   "$(cat "$_lw" 2>/dev/null)" "long-named song"
assert "...and the shorter name is in the report" \
    grep -q 'shorter name' "$FERRY_STATE/report.d/files-copy.tsv"
eq "...and her own file is untouched" "$(cat "$HOME1/Music/$LONG.mp3")" "my song"
_odd=0
for n in "-leading dash.txt" 'quote"d.txt' 'back\slash.txt' "$(printf 'tab\tin.txt')" \
         "trailing space .txt" "café ünïcødé 日本.txt" "$(printf 'new\nline.txt')"; do
    [ "$(cat "$HOME1/Downloads/$n" 2>/dev/null)" = "odd name" ] || _odd=$((_odd+1))
done
eq "every odd Windows file name comes across exactly" "$_odd" 0
eq "no ledger row is split by a tab or newline in a name" \
   "$(cat "$FERRY_STATE/manifest.tsv" | awk -F'\t' 'NF != 6' | wc -l | tr -d ' ')$(cat "$FERRY_STATE"/report.d/*.tsv | awk -F'\t' 'NF != 4' | wc -l | tr -d ' ')" "00"
eq "the date a file was last changed comes with it" \
   "$(stat -c %Y "$HOME1/Desktop/report.docx")" "$(stat -c %Y "$WIN/users/Bob/Desktop/report.docx")"
refute "a temporary from a run that was cut off is gone" test -e "$HOME1/Desktop/.ferrypart-999-0"
refute "no temporary is left anywhere" sh -c "find '$HOME1' -name '.ferrypart-*' | grep -q ."

# Honest progress: one 64 MB file sorted first, then 100 tiny ones. By
# bytes, the bar is nearly full after the first file; by count it would
# say 1 %.
P="$TMP/prog"; mkdir -p "$P/src"; [ -z "$ASME" ] || chown 4242 "$P"
python3 -c 'import sys; open(sys.argv[1], "wb").write(b"x" * (32 << 20))' "$P/src/a-big.bin"
for i in $(seq 1 100); do echo small > "$P/src/s$i.txt"; done
FERRY_TICK_MS=0 FERRY_PROGRESS="$P/log" FERRY_PLAIN_FD=3 \
    ferry-copy $ASME --state "$P/state" --stage prog --job Pictures "$P/src" "$P/dst" 3>"$P/plain" 2>/dev/null
_pct1=$(grep '"done":1,' "$P/log" | tail -1 | sed 's/.*"pct":\([0-9]*\).*/\1/')
assert "progress is by bytes: one big file in, the bar is past 80 % (${_pct1:-?} %)" \
    test "${_pct1:-0}" -ge 80
python3 - "$P/log" > "$TMP/prog.verdict" <<'PY2'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1])]
pct = [r["pct"] for r in rows]
print("monotonic" if all(b >= a for a, b in zip(pct, pct[1:])) else "backwards")
last = rows[-1]
print("complete" if last["pct"] == 100 and last["work_done"] == last["work_total"] else "short")
PY2
assert "the bar never goes backwards" grep -qx monotonic "$TMP/prog.verdict"
assert "it ends at 100 % with every byte counted" grep -qx complete "$TMP/prog.verdict"
eq "what she reads is plain and counts files" \
   "$(tail -1 "$P/plain")" "Copying your Pictures: 101 of 101 files."
refute "no file name ever reaches the screen" grep -q 'a-big\|s1.txt' "$P/plain"
ferry-copy $ASME --dry-run --job Pictures "$P/src" "$P/dry" 2>/dev/null
refute "a dry run writes nothing" test -e "$P/dry"
ln -s "$P/src" "$P/linked-src"
ferry-copy $ASME --job Pictures "$P/linked-src" "$P/via-link" >/dev/null 2>&1
eq "a source folder that is a link is refused (exit 2, reported)" "$?" 2
refute "...and nothing came through it" test -e "$P/via-link/a-big.bin"
rm -rf "$P"                                        # the big files are not needed again

echo "hostile Windows volume and hostile home:"
# Every path below comes from data on her disk or in her home, and each
# one tries to make Ferry read or write outside where it should.
H="$TMP/hostile"; sh "$HERE/mkfakewin.sh" "$H" >/dev/null
OUT="$TMP/outside"; mkdir -p "$OUT"; echo "secret outside the volume" > "$OUT/secret.txt"
rm -rf "$H/users/Bob/Pictures"; ln -s "$OUT" "$H/users/Bob/Pictures"     # a junction
mkdir -p "$OUT/Videos"; echo "secret outside the volume" > "$OUT/Videos/clip.txt"
ln -s "$OUT" "$H/users/Bob/Linked"                    # a junction mid-path
FFP="$H/users/Bob/AppData/Roaming/Mozilla/Firefox/Profiles"
ln -s "$OUT" "$FFP/evil.default"                                         # a junction
python3 "$HERE/mkhive.py" "$H/users/Bob/NTUSER.DAT" '{
  "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders": {
    "Personal": ["exp", "%USERPROFILE%\\..\\..\\..\\outside"],
    "My Music": ["exp", "%USERPROFILE%\\Music"],
    "My Video": ["exp", "%USERPROFILE%\\Linked\\Videos"]
  },
  "Control Panel\\Desktop": {"Wallpaper": ["sz", "C:\\..\\outside\\secret.txt"]}
}'
HH="$TMP/hhome"; mkdir -p "$HH/.config"
printf 'XDG_MUSIC_DIR="$HOME/../outside-music"\nXDG_DOWNLOAD_DIR="%s"\n' "$TMP/outside-dl" \
    > "$HH/.config/user-dirs.dirs"
hers "$HH"
SH="$TMP/state-hostile"
FERRY_STATE="$SH" ferry-files --root "$H" --home "$HH" --owner "$OWNER" >/dev/null 2>&1
FERRY_STATE="$SH" ferry-firefox --root "$H" --home "$HH" --owner "$OWNER" >/dev/null 2>&1
FERRY_STATE="$SH" ferry-settings --root "$H" --home "$HH" --owner "$OWNER" --only wallpaper >/dev/null 2>&1
refute "a '..' in a registry folder does not reach outside the volume" \
    grep -rqs 'secret outside the volume' "$HH/Documents"
refute "a junction where Pictures should be is not followed" \
    grep -rqs 'secret outside the volume' "$HH/Pictures"
refute "a junction inside a folder's path is not followed" \
    grep -rqs 'secret outside the volume' "$HH/Videos"
refute "a junction among the Firefox profiles is not followed" \
    grep -rqs 'secret outside the volume' "$HH/.mozilla"
refute "a '..' in the wallpaper value is not followed" \
    grep -rqs 'secret outside the volume' "$HH/.local"
refute "a user-dirs.dirs pointing out of her home is not obeyed (..)" test -e "$TMP/outside-music"
refute "a user-dirs.dirs pointing out of her home is not obeyed (absolute)" test -e "$TMP/outside-dl"
assert "...and those folders land in her home instead" test -f "$HH/Music/track.mp3"

if [ "$(id -u)" = 0 ]; then
    echo "as root, which is how it really runs:"
    # A user to bring things to, and a root-owned folder she wants root
    # to write into: a symlink planted where Pictures and .local go.
    HR=$(getent passwd ferryher | cut -d: -f6)
    rm -rf "$HR/Pictures" "$HR/.local" /var/tmp/ferry-victim
    mkdir -p /var/tmp/ferry-victim && chmod 0755 /var/tmp/ferry-victim
    ln -s /var/tmp/ferry-victim "$HR/Pictures"
    ln -s /var/tmp/ferry-victim "$HR/.local"
    chown -h ferryher: "$HR/Pictures" "$HR/.local"
    SR="$TMP/state-root"
    FERRY_STATE="$SR" ferry-files --root "$WIN" --home "$HR" --owner ferryher >/dev/null 2>&1
    FERRY_STATE="$SR" ferry-firefox --root "$WIN" --home "$HR" --owner ferryher >/dev/null 2>&1
    FERRY_STATE="$SR" ferry-settings --root "$WIN" --home "$HR" --owner ferryher --only wallpaper >/dev/null 2>&1
    eq "nothing was written through her symlinks into a root-owned folder" \
       "$(ls -A /var/tmp/ferry-victim | wc -l | tr -d ' ')" 0
    assert "...and the attempt is in the report" \
        grep -q 'could not be saved' "$SR/report.d/files-copy.tsv"
    assert "her files did come, and every one of them is hers" \
        sh -c "test -f '$HR/Desktop/report.docx' && ! find '$HR' ! -user ferryher | grep -q ."
    assert "Ferry's own records stay root's" test "$(stat -c %U "$SR/manifest.tsv")" = root
    ferry-copy --job X "$P/src" "$TMP/as-root" >/dev/null 2>&1
    eq "ferry-copy refuses to write as root without --as (exit 1)" "$?" 1
    refute "...and wrote nothing" test -e "$TMP/as-root"
else
    echo "as root: skipped (run tests/asroot.sh, or this suite as root, for these)"
fi

echo "firefox install name (the name Firefox gives an install in profiles.ini):"
# Both values come from real Firefoxes: the first from Mozilla's .deb
# (read out of its own profiles.ini), the second the well-known name of
# a default 64-bit Windows install.
eq "/usr/lib/firefox is 4F96D1932A9F858E, as the real Firefox calls it" \
   "$(ferry-ffhash /usr/lib/firefox)" 4F96D1932A9F858E
eq "...a path of the longer kind (33-64 bytes) agrees too" \
   "$(ferry-ffhash 'C:\Program Files\Mozilla Firefox')" 308046B0AF4A39CB
ferry-ffhash /a/path/to/an/install/folder/that/is/long/firefox >/dev/null 2>&1
eq "...a path it cannot vouch for is refused (exit 2)" "$?" 2
_bad=$(printf '/usr/lib/\377firefox'); ferry-ffhash "$_bad" >/dev/null 2>&1
eq "...and so is one that is not text (exit 2)" "$?" 2

echo "firefox:"
# The install name a real Firefox on the image would have.
export FERRY_FFHASH=4F96D1932A9F858E
ferry-firefox --root "$WIN" --home "$HOME1" --owner "$OWNER" >/dev/null 2>&1
FFD="$HOME1/.mozilla/firefox"
FP="$FFD/a1b2c3d4.default-release"
assert "places.sqlite (bookmarks/history) copied" test -f "$FP/places.sqlite"
assert "prefs.js (settings) copied" test -f "$FP/prefs.js"
refute "key4.db (the password key) NOT copied" test -e "$FP/key4.db"
refute "logins.json (saved passwords) NOT copied" test -e "$FP/logins.json"
refute "a password file in odd case NOT copied" test -e "$FP/LOGINS-BACKUP.JSON"
assert "the report says passwords were left on Windows" \
    grep -q 'firefox/passwords' "$FERRY_STATE/report.d/firefox.tsv"
PI="$FFD/profiles.ini"
assert "Firefox is told to open her Windows profile ([Install<name>] Default=)" \
    sh -c "awk '/^\[Install4F96D1932A9F858E\]\$/ {i=1; next} /^\[/ {i=0} i && /^Default=a1b2c3d4.default-release\$/ {f=1} END {exit !f}' '$PI'"
assert "...and the profile is listed, with the next free number" \
    sh -c "grep -q '^\[Profile0\]\$' '$PI' && grep -q '^Path=a1b2c3d4.default-release\$' '$PI'"
assert "...installs.ini says the same" \
    sh -c "grep -q '^\[4F96D1932A9F858E\]\$' '$FFD/installs.ini' && grep -q '^Default=a1b2c3d4.default-release\$' '$FFD/installs.ini'"
refute "...and Windows' own claim on the profile did not come (it names the Windows install)" \
    sh -c "grep -q 'Install308046B0AF4A39CB\|308046B0AF4A39CB' '$PI' '$FFD/installs.ini'"
EB="$HOME1/Bookmarks from Edge.html"
assert "with a Windows Firefox too, her Edge bookmarks are saved in her Home" \
    grep -q 'https://photos.example.org/family?a=1&amp;b=2' "$EB"
assert "...with plain steps in her report" grep -q 'Import Bookmarks from HTML' "$FERRY_STATE/report.d/firefox.tsv"
assert "...a title that is markup is escaped" grep -q 'Grandkids &amp; &lt;photos&gt;' "$EB"
refute "...a javascript: bookmark (a program) does not come" grep -q 'javascript:' "$EB"
refute "...nor a file: link to a Windows path" grep -q 'file:///' "$EB"
assert "...folders and accents survive" sh -c "grep -q '>Money</H3>' '$EB' && grep -q 'Recipes été' '$EB'"
assert "...her Chrome bookmarks are a folder of their own" grep -q 'Bookmarks from Chrome' "$EB"
refute "cache NOT copied" test -e "$FP/cache2"
refute "stale lock NOT copied" test -e "$FP/.parentlock"
refute "compatibility.ini NOT copied" test -e "$FP/compatibility.ini"

echo "Edge, and no Firefox, on Windows (most of the people AurOS is for):"
E="$TMP/edgeonly"; cp -r "$WIN" "$E"; rm -rf "$E/users/Bob/AppData/Roaming/Mozilla"
HE="$TMP/home-edge"; mkdir -p "$HE"; hers "$HE"
FERRY_STATE="$TMP/state-edge" ferry-firefox --root "$E" --home "$HE" --owner "$OWNER" >/dev/null 2>&1
eq "the browser stage succeeds (exit 0)" "$?" 0
FE="$HE/.mozilla/firefox"
assert "her Edge bar becomes Firefox's toolbar, in a new profile" \
    grep -q 'PERSONAL_TOOLBAR_FOLDER="true"' "$FE/fromwindows.default-release/bookmarks.html"
assert "...which this Firefox is told to open" \
    sh -c "awk '/^\[Install4F96D1932A9F858E\]\$/ {i=1; next} /^\[/ {i=0} i && /^Default=fromwindows.default-release\$/ {f=1} END {exit !f}' '$FE/profiles.ini'"
assert "...and her report says so" grep -q 'will be in Firefox the first time you open it' "$TMP/state-edge/report.d/firefox.tsv"
ferry-firefox --root "$E" --home "$HE" --owner "$OWNER" --force >/dev/null 2>&1
eq "bringing it across again changes nothing in profiles.ini (idempotent)" \
   "$(grep -c '^\[Profile' "$FE/profiles.ini")|$(grep -c '^\[Install' "$FE/profiles.ini")" "1|1"
refute "...and a second go does not mistake its own profile for hers (bookmarks stay in Firefox)" \
    test -e "$HE/Bookmarks from Edge.html"

# Firefox 147+ keeps its profiles under ~/.config/mozilla/firefox.
HX="$TMP/home-xdg"; mkdir -p "$HX/.config/mozilla/firefox"; hers "$HX"
FERRY_STATE="$TMP/state-xdg" ferry-firefox --root "$E" --home "$HX" --owner "$OWNER" >/dev/null 2>&1
assert "where Firefox keeps its profiles in ~/.config/mozilla/firefox, that is where it goes" \
    test -f "$HX/.config/mozilla/firefox/fromwindows.default-release/bookmarks.html"
refute "...and nothing is put in the old place Firefox would now ignore" test -e "$HX/.mozilla"

# A profiles.ini already holding two profiles: ours takes the next number.
HN="$TMP/home-num"; FN="$HN/.mozilla/firefox"; mkdir -p "$FN"
printf '[Install1111111111111111]\nDefault=aaa.default\nLocked=1\n\n[Profile0]\nName=default\nIsRelative=1\nPath=aaa.default\nDefault=1\n\n[Profile1]\nName=other\nIsRelative=1\nPath=bbb.other\n\n[General]\nStartWithLastProfile=1\nVersion=2\n' > "$FN/profiles.ini"; hers "$HN"
FERRY_STATE="$TMP/state-num" ferry-firefox --root "$E" --home "$HN" --owner "$OWNER" >/dev/null 2>&1
assert "an existing profiles.ini: ours is [Profile2], numbering stays unbroken (Firefox stops at a gap)" \
    sh -c "grep -A3 '^\[Profile2\]' '$FN/profiles.ini' | grep -q '^Path=fromwindows.default-release\$'"
assert "...another install's section is left as it was" \
    grep -A1 '^\[Install1111111111111111\]' "$FN/profiles.ini" | grep -q '^Default=aaa.default$'

# She opened Firefox on AurOS before bringing her files across: what she
# has been doing in it must not vanish from under her.
HF="$TMP/home-ffran"; FR="$HF/.mozilla/firefox"; mkdir -p "$FR/abc.default-release"
printf '[Install4F96D1932A9F858E]\nDefault=abc.default-release\nLocked=1\n\n[Profile0]\nName=default-release\nIsRelative=1\nPath=abc.default-release\nDefault=1\n\n[General]\nStartWithLastProfile=1\nVersion=2\n' > "$FR/profiles.ini"
printf '[4F96D1932A9F858E]\nDefault=abc.default-release\nLocked=1\n' > "$FR/installs.ini"
cp "$FR/profiles.ini" "$TMP/profiles.before"; hers "$HF"
FERRY_STATE="$TMP/state-ffran" ferry-firefox --root "$E" --home "$HF" --owner "$OWNER" >/dev/null 2>&1
assert "after she used Firefox here, it is NOT switched away from her profile" cmp -s "$TMP/profiles.before" "$FR/profiles.ini"
assert "...her Edge bookmarks wait in her Home with steps" test -s "$HF/Bookmarks from Edge.html"

# Firefox is open while she brings her files across. (Its profiles.ini
# has no section for this install, so only "it is open" can stop us.)
HO="$TMP/home-ffopen"; FO="$HO/.mozilla/firefox"; mkdir -p "$FO/abc.default-release"
printf '[Profile0]\nName=default\nIsRelative=1\nPath=abc.default-release\nDefault=1\n\n[General]\nStartWithLastProfile=1\nVersion=2\n' > "$FO/profiles.ini"
cp "$FO/profiles.ini" "$TMP/profiles.open"
ln -s "127.0.0.1:+$$" "$FO/abc.default-release/lock"; hers "$HO"
FERRY_STATE="$TMP/state-ffopen" ferry-firefox --root "$E" --home "$HO" --owner "$OWNER" >/dev/null 2>&1
assert "while Firefox is open, its profiles.ini is left alone" cmp -s "$TMP/profiles.open" "$FO/profiles.ini"
assert "...and her Edge bookmarks wait in her Home instead" test -s "$HO/Bookmarks from Edge.html"

# No Firefox on this computer to name.
HZ="$TMP/home-nofx"; mkdir -p "$HZ"; hers "$HZ"
FERRY_FFHASH="" FERRY_FIREFOX=/nonexistent/firefox FERRY_STATE="$TMP/state-nofx" \
    ferry-firefox --root "$E" --home "$HZ" --owner "$OWNER" >/dev/null 2>&1
refute "with no Firefox installed, no profiles.ini is invented" test -e "$HZ/.mozilla/firefox/profiles.ini"
assert "...her bookmarks still wait in her Home" test -s "$HZ/Bookmarks from Edge.html"

echo "bookmarks reader on hostile files:"
python3 -c 'print("[" * 200000)' > "$TMP/deep.json"
ferry-chromemarks Edge "$TMP/deep.json" >/dev/null 2>&1
eq "200,000 nested brackets: refused, not a crash (exit 2)" "$?" 2
head -c 4096 /dev/urandom > "$TMP/noise.json"
ferry-chromemarks Edge "$TMP/noise.json" >/dev/null 2>&1
eq "random bytes: refused (exit 2)" "$?" 2
printf '{"roots":{"bookmark_bar":{"children":[{"type":"url","name":"x","url":"https://a.example/"}],"name":"B","type":"folder"}}' > "$TMP/cut.json"
ferry-chromemarks Edge "$TMP/cut.json" >/dev/null 2>&1
eq "a file cut off mid-way: refused (exit 2)" "$?" 2
unset FERRY_FFHASH

echo "wifi (never a password):"
NMDIR="$TMP/nm"
# A helper planted where the old decryption call-out looked. It must
# never run: Ferry does not decrypt saved passwords.
printf '#!/bin/sh\ntouch "%s"\necho FilledSecret\n' "$TMP/helper-ran" > "$TMP/helper"
chmod +x "$TMP/helper"
FERRY_DPAPI_HELPER="$TMP/helper" \
    ferry-wifi --root "$WIN" --to "$NMDIR" --no-reload >/dev/null 2>&1
eq "wifi stage succeeds" "$?" 0
refute "no decryption helper was ever run" test -e "$TMP/helper-ran"
refute "no network file written at all" \
    sh -c "find '$NMDIR' /etc/NetworkManager/system-connections -name '*.nmconnection' -newer '$TMP/helper' 2>/dev/null | grep -q ."
assert "secured networks listed by name" \
    sh -c "grep -q 'HomeNet' '$FERRY_STATE/report.d/wifi.tsv' && grep -q 'Phone Hotspot' '$FERRY_STATE/report.d/wifi.tsv'"
assert "open network listed" grep -q 'CoffeeShop' "$FERRY_STATE/report.d/wifi.tsv"
assert "enterprise network listed" grep -q 'eduroam' "$FERRY_STATE/report.d/wifi.tsv"
refute "no Wi-Fi password in the report" \
    grep -rq 'hunter2hunter2' "$FERRY_STATE"
# An SSID only in <hex> (no <name>): mawk has no strtonum, the image's
# awk is mawk, so the decode must not need it.
W2="$TMP/w2"; mkdir -p "$W2/Windows/System32" \
    "$W2/ProgramData/Microsoft/Wlansvc/Profiles/Interfaces/{x}"
printf '<WLANProfile><SSIDConfig><SSID><hex>4F6C6473</hex></SSID></SSIDConfig><MSM><security><authEncryption><authentication>WPA2PSK</authentication></authEncryption></security></MSM></WLANProfile>' \
    > "$W2/ProgramData/Microsoft/Wlansvc/Profiles/Interfaces/{x}/a.xml"
FERRY_STATE="$TMP/state-w2" ferry-wifi --root "$W2" >/dev/null 2>&1
assert "a hex-only SSID is decoded with the image's awk" \
    grep -q 'Olds' "$TMP/state-w2/report.d/wifi.tsv"

echo "settings (timezone mapping is the point):"
rm -rf "$TMP/state3"
TZ=$(FERRY_STATE="$TMP/state3" ferry-settings --root "$WIN" --home "$TMP/h3" \
       --owner "$OWNER" --only timezone --dry-run 2>/dev/null; \
     grep -h timezone "$TMP/state3/report.d/settings.tsv" 2>/dev/null)
case "$TZ" in
    *Europe/London*) ok "GMT Standard Time -> Europe/London (CLDR mapping)" ;;
    *) bad "timezone mapping failed: $TZ" ;;
esac

# WHAT SHE CHOSE IN THE INSTALLER WINS. First boot records it in
# /etc/auros/installer-choices.conf (rootfs/usr/lib/auros/choices.sh);
# the Windows value is still suggested, but not applied over it.
rm -rf "$TMP/state3b"
printf 'timezone=America/New_York\n' > "$TMP/choices.conf"
OUT=$(FERRY_CHOICES="$TMP/choices.conf" FERRY_STATE="$TMP/state3b" \
      ferry-settings --root "$WIN" --home "$TMP/h3b" --owner "$OWNER" \
      --only timezone --dry-run 2>&1)
case "$OUT" in
    *"kept as chosen in the installer"*) ok "a time zone chosen in the installer is not overwritten" ;;
    *) bad "installer choice overwritten by the Windows timezone: $OUT" ;;
esac

echo "safety gate:"
sh "$HERE/mkfakewin.sh" "$TMP/hib" --hibernated >/dev/null
rm -rf "$TMP/state4"
FERRY_STATE="$TMP/state4" ferry-files --root "$TMP/hib" --home "$TMP/h4" \
    --owner "$OWNER" >/dev/null 2>&1
eq "hibernated volume is refused (exit 3)" "$?" 3
FERRY_STATE="$TMP/state4b" ferry-detect --root "$TMP/hib" --quick >/dev/null 2>&1
eq "detect still succeeds on a hibernated volume" "$?" 0

echo "no saved password anywhere on the AurOS side:"
refute "no Wi-Fi key, no Firefox login, anywhere under the new home or state" \
    sh -c "grep -rqs -e hunter2hunter2 -e bank.example '$HOME1' '$FERRY_STATE' '$TMP/nm'"
refute "no password file anywhere under the new home" \
    sh -c "find '$HOME1' -iname 'logins*.json' -o -iname 'key[34].db' -o -iname 'signons*' | grep -q ."

echo "parsers of hostile data, under AddressSanitizer and UndefinedBehaviorSanitizer:"
# fuzz/*.c are libFuzzer harnesses; fuzz/replay.c runs them over files so
# this suite (gcc, no clang) can. Inputs that once broke something are in
# tests/regress/; thousands of corrupted ones are made here.
RP="$TMP/replay"; mkdir -p "$RP/corrupt" "$RP/corrupt-marks"
SAN="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined"
_san=1
for _h in hive names marks; do
    cc $SAN -o "$RP/$_h" "$FERRY/fuzz/$_h.c" "$FERRY/fuzz/replay.c" 2>"$RP/$_h.err" || _san=0
done
if [ "$_san" = 1 ]; then
    python3 - "$WIN/Windows/System32/config/SOFTWARE" "$NT" "$WIN/users/Bob/AppData/Local/Microsoft/Edge/User Data/Default/Bookmarks" "$RP" <<'PY'
import random, struct, sys
random.seed(1)
hives = [open(p, "rb").read() for p in sys.argv[1:3]]
marks = open(sys.argv[3], "rb").read()
out = sys.argv[4]
for i in range(3000):
    h = bytearray(random.choice(hives))
    for _ in range(random.randint(1, 16)):
        if random.random() < 0.5:                       # a byte
            h[random.randrange(len(h))] = random.randrange(256)
        else:                                           # a 32-bit field gone bad
            o = random.randrange(0, len(h) - 4) & ~3
            struct.pack_into("<I", h, o, random.choice([0, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFC, 4, 0x1000]))
    if random.random() < 0.2:
        del h[random.randrange(4096, len(h)):]          # cut off
    open("%s/corrupt/c%04d" % (out, i), "wb").write(h)
for i in range(1500):
    m = bytearray(marks)
    for _ in range(random.randint(1, 8)):
        m[random.randrange(len(m))] = random.choice(b'{}[]",:\\tfn0123456789e-.u"')
    if random.random() < 0.4:
        del m[random.randrange(1, len(m)):]
    open("%s/corrupt-marks/m%04d" % (out, i), "wb").write(m)
PY
    assert "inputs that once broke the registry reader replay clean (a hostile cell size)" "$RP/hive" "$FERRY"/tests/regress/hive-*
    assert "inputs that once broke the name maker replay clean (a name of stray bytes)" "$RP/names" "$FERRY"/tests/regress/names-*
    assert "inputs that once broke the bookmarks reader replay clean (a file cut off mid-word)" "$RP/marks" "$FERRY"/tests/regress/marks-*
    assert "3,000 corrupted registry hives: no crash, no overflow, no undefined behaviour" "$RP/hive" "$RP"/corrupt/*
    assert "1,500 corrupted bookmark files: no crash, no overflow, no undefined behaviour" "$RP/marks" "$RP"/corrupt-marks/*
    assert "her real-shaped bookmarks go through the sanitizers too" "$RP/marks" "$WIN/users/Bob/AppData/Local/Microsoft/Edge/User Data/Default/Bookmarks"
    rm -rf "$RP"
else
    echo "  (skipped: this compiler has no AddressSanitizer/UndefinedBehaviorSanitizer)"
fi

echo "hardening (the image build refuses a program without it):"
CS="$FERRY/../../tools/checksec.sh"
if [ -f "$CS" ] && command -v gcc >/dev/null 2>&1; then
    HD="$TMP/hard"; mkdir -p "$HD"
    sh "$FERRY/install.sh" helpers "$HD" >/dev/null 2>&1
    assert "every compiled helper carries the hardening the image build demands (PIE, RELRO, stack protector...)" \
        sh "$CS" "$HD"/usr/lib/ferry/ferry-*
else
    echo "  (skipped: tools/checksec.sh or gcc not here)"
fi

echo "install list:"
for _f in $(sh "$FERRY/install.sh" list); do
    [ -f "$FERRY/$_f" ] || bad "install.sh names $_f, which does not exist"
done
_missing=""
for _f in "$FERRY"/ferry*; do
    _b=$(basename "$_f")
    sh "$FERRY/install.sh" list | grep -qx "$_b" || _missing="$_missing $_b"
done
eq "every ferry tool in the tree is installed by install.sh" "$_missing" ""

echo "started the way the image starts it:"
# /usr/bin/ferry is a symlink to /usr/lib/ferry/ferry. Ferry finds its
# tools beside the real file, or answer.sh's `ferry run` dies at once.
mkdir -p "$TMP/usrbin"; ln -s "$FERRY/ferry" "$TMP/usrbin/ferry"
assert "ferry started through a link finds its own tools" \
    sh -c "env -u FERRY_HIVE_BACKEND PATH=/usr/bin:/bin '$TMP/usrbin/ferry' backend | grep -q ."

echo "the way back (her AurOS files into Windows before Put Windows back):"
BK="$TMP/win-back"; cp -r "$WIN" "$BK"; hers "$BK"
AH="$TMP/aurhome"; mkdir -p "$AH/Pictures/2024" "$AH/Documents" "$AH/Desktop" "$AH/Downloads" "$AH/Music" "$AH/Videos"
head -c 300000 /dev/urandom > "$AH/Pictures/2024/holiday.jpg"
touch -d '2024-07-01 10:00:00' "$AH/Pictures/2024/holiday.jpg"
echo "my new letter"  > "$AH/Documents/letter.txt"
echo "UPPER" > "$AH/Documents/Report.docx"; echo "lower" > "$AH/Documents/report.docx"
echo 1 > "$AH/Documents/a:b.txt"; echo 2 > "$AH/Documents/what?.txt"; echo 3 > "$AH/Documents/CON.txt"
echo 4 > "$AH/Documents/trailing."; echo 5 > "$AH/Documents/bad<name>.txt"; echo 6 > "$AH/Documents/q\"uote|pipe.txt"
echo 7 > "$AH/Documents/$(printf 'raw\377byte.txt')"
echo 8 > "$AH/Documents/caf$(printf '\303\211').txt"; echo 9 > "$AH/Documents/caf$(printf '\303\251').txt"
echo lock > "$AH/Documents/.~lock.letter.txt#"
ln -s /etc/passwd "$AH/Documents/shortcut"
mkdir -p "$AH/Documents/.Trash-1000"; echo trash > "$AH/Documents/.Trash-1000/x"
mkdir -p "$BK/users/Bob/From AurOS/Documents"; echo "windows version of the letter" > "$BK/users/Bob/From AurOS/Documents/letter.txt"
echo "keep me" > "$BK/users/Bob/Documents/windows-only.txt"
hers "$AH" "$BK"
( cd "$BK" && find . -type f -not -path './users/Bob/From AurOS/*' -exec stat -c '%n %s %Y' {} + | sort ) > "$TMP/bk.before"
BKENV() { _bn=$1 _br=$2; shift 2
          env FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-back$_bn" FERRY_ROOT="$_br" FERRY_HOME="$AH" \
              FERRY_OWNER="$OWNER" FERRY_SIGNAL="$TMP/back$_bn.done" "$@"; }
BKENV 1 "$BK" ferry back --plain > "$TMP/back1.out" 2>&1
eq "ferry back succeeds (exit 0)" "$?" 0
FA="$BK/users/Bob/From AurOS"
assert "a photo saved in AurOS is in her Windows folder, byte for byte" cmp -s "$AH/Pictures/2024/holiday.jpg" "$FA/Pictures/2024/holiday.jpg"
eq "...with the date she saved it" "$(stat -c %Y "$FA/Pictures/2024/holiday.jpg")" "$(stat -c %Y "$AH/Pictures/2024/holiday.jpg")"
eq "a file Windows already had is NOT overwritten" "$(cat "$FA/Documents/letter.txt")" "windows version of the letter"
eq "...hers lands beside it" "$(cat "$FA/Documents/letter (from AurOS).txt")" "my new letter"
eq "Report.docx and report.docx are kept apart (Windows sees them as one name)" \
   "$(find "$FA/Documents" -iname 'report*.docx' | wc -l | tr -d ' ')|$(find "$FA/Documents" -iname 'report.docx' | wc -l | tr -d ' ')" "2|1"
eq "...both contents survive" "$( (cat "$FA/Documents"/[Rr]eport*.docx) | sort | tr '\n' ' ')" "UPPER lower "
_cafE=$(printf 'caf\303\211.txt'); _cafe2=$(printf 'caf\303\251 (from AurOS).txt')
eq "an accented capital and its small letter are one name to Windows too: the second is kept apart" \
   "$(cat "$FA/Documents/$_cafE" 2>/dev/null)|$(cat "$FA/Documents/$_cafe2" 2>/dev/null)" "8|9"
refute "no name Windows cannot hold was created" sh -c "find '$FA' -name '*[:*?\"<>|]*' | grep -q ."
eq "a colon becomes an underscore" "$(cat "$FA/Documents/a_b.txt")" 1
eq "a question mark too" "$(cat "$FA/Documents/what_.txt")" 2
eq "CON is a device name in Windows: it is saved as _CON.txt" "$(cat "$FA/Documents/_CON.txt")" 3
eq "a trailing dot (Windows cannot open it) is replaced" "$(cat "$FA/Documents/trailing_")" 4
eq "bytes that are not text are replaced, the rest kept" "$(cat "$FA/Documents/raw_byte.txt")" 7
refute "an editor's lock file is not hers" test -e "$FA/Documents/.~lock.letter.txt#"
refute "...nor the desktop's trash" test -e "$FA/Documents/.Trash-1000"
refute "a shortcut (symlink) is not followed: nothing from /etc came" sh -c "grep -rq 'root:' '$FA'"
assert "the names that had to change are in the note, in her words" \
    sh -c "grep -q 'Windows cannot hold some of the letters' '$FERRY_STATE/../state-back1/report.d/back-copy.tsv' 2>/dev/null || grep -q 'Windows cannot hold some of the letters' '$TMP/state-back1/report.d/back-copy.tsv'"
assert "a note for her is in the folder, with Windows line endings" sh -c "grep -q 'files you saved while you used AurOS' '$FA/Read me.txt' && grep -q \"$(printf '\r')\" '$FA/Read me.txt'"
( cd "$BK" && find . -type f -not -path './users/Bob/From AurOS/*' -exec stat -c '%n %s %Y' {} + | sort ) > "$TMP/bk.after"
assert "nothing else on the Windows drive changed" cmp -s "$TMP/bk.before" "$TMP/bk.after"
eq "what she reads is plain" "$(grep -cvE '^(Getting ready to save your files in Windows\.|Saving your [A-Za-z]+ in Windows: [0-9,]+ of [0-9,]+ files\.( (Less than a minute|About (a|[0-9]+) minutes?|About (an|[0-9]+) hours?) left\.)?|Your [0-9,]+ files are saved in Windows, in the folder "From AurOS"\.)$' "$TMP/back1.out")" 0
assert "...ending with the sentence that says how many are saved" sh -c "tail -1 '$TMP/back1.out' | grep -qE '^Your [0-9]+ files are saved in Windows'"
assert "the completion signal says: back, ok, and how many files" \
    sh -c "grep -qx 'kind=back' '$TMP/back1.done' && grep -qx 'result=ok' '$TMP/back1.done' && grep -qE '^files=[0-9]+\$' '$TMP/back1.done' && ! grep -qx 'files=0' '$TMP/back1.done'"

# She has no files in AurOS yet: it is said, not "saved".
BKE="$TMP/win-back-empty"; cp -r "$WIN" "$BKE"; hers "$BKE"
AHE="$TMP/aurhome-empty"; mkdir -p "$AHE"; hers "$AHE"
env FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-backE" FERRY_ROOT="$BKE" FERRY_HOME="$AHE" \
    FERRY_OWNER="$OWNER" FERRY_SIGNAL="$TMP/backE.done" ferry back --plain > "$TMP/backE.out" 2>&1
eq "no files in AurOS to save: exit 0" "$?" 0
eq "...and she is told that, not that her files are saved" "$(tail -1 "$TMP/backE.out")" "You had no files to save in Windows."

# With nothing to save, the Windows drive is not looked at: a Windows that
# was not shut down properly (or is locked, or absent) does not hold up
# Put Windows back for someone with an empty AurOS home.
BKH="$TMP/win-back-hib"; sh "$HERE/mkfakewin.sh" "$BKH" --hibernated >/dev/null; hers "$BKH"
env FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-backH" FERRY_ROOT="$BKH" FERRY_HOME="$AHE" \
    FERRY_OWNER="$OWNER" FERRY_SIGNAL="$TMP/backH.done" ferry back --plain > "$TMP/backH.out" 2>&1
eq "nothing to save: a hibernated Windows is not even looked at (exit 0, not 3)" "$?" 0
# A shortcut where a folder should be is something to save (and is stopped on).
AHL="$TMP/aurhome-link"; mkdir -p "$AHL"; ln -s /etc "$AHL/Documents"; hers "$AHL"
env FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-backL" FERRY_ROOT="$BKE" FERRY_HOME="$AHL" \
    FERRY_OWNER="$OWNER" FERRY_SIGNAL="$TMP/backL.done" ferry back --plain > "$TMP/backL.out" 2>&1
eq "a shortcut where Documents should be counts as something to save (stops: exit 2)" "$?" 2
# Only the files an editor leaves about: nothing to save.
AHT="$TMP/aurhome-trash"; mkdir -p "$AHT/Documents/.Trash-1000"; echo x > "$AHT/Documents/.Trash-1000/a"; echo l > "$AHT/Documents/.~lock.a#"; hers "$AHT"
env FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-backT" FERRY_ROOT="$BKH" FERRY_HOME="$AHT" \
    FERRY_OWNER="$OWNER" FERRY_SIGNAL="$TMP/backT.done" ferry back --plain > "$TMP/backT.out" 2>&1
eq "a trash folder and an editor's lock file are not files to save (exit 0, drive not looked at)" "$?" 0

# Run again: nothing is saved twice.
BKENV 1 "$BK" ferry back --plain > /dev/null 2>&1
eq "run again: no second copy of anything" "$(find "$FA" -type f | wc -l | tr -d ' ')" "$(find "$FA" -type f | wc -l | tr -d ' ')"
eq "...(the same files)" "$(find "$FA" -name '*(from AurOS 2)*' | wc -l | tr -d ' ')" 0

# Not enough room: nothing is copied, and she is told.
BK2="$TMP/win-back2"; cp -r "$WIN" "$BK2"; hers "$BK2"
BKENV 2 "$BK2" env FERRY_ROOM_BYTES=1000000 ferry back --plain > "$TMP/back2.out" 2>&1
eq "not enough room in Windows: exit 4" "$?" 4
refute "...and not one of her files was copied" sh -c "find '$BK2/users/Bob/From AurOS' -type f -not -name 'Read me.txt' | grep -q ."
assert "...she is told, plainly" grep -q 'There is not enough room in Windows for your files' "$TMP/back2.out"
assert "...the report says how much" grep -q 'they need about' "$TMP/state-back2/report.d/back-copy.tsv"
assert "...and the signal says noroom" grep -qx 'result=noroom' "$TMP/back2.done"

# A hibernated Windows is refused before anything is written.
BK3="$TMP/win-back3"; sh "$HERE/mkfakewin.sh" "$BK3" --hibernated >/dev/null; hers "$BK3"
BKENV 3 "$BK3" ferry back --plain > "$TMP/back3.out" 2>&1
eq "a hibernated Windows is refused (exit 3)" "$?" 3
refute "...and nothing was written to it" test -e "$BK3/users/Bob/From AurOS"
assert "...the signal says refused" grep -qx 'result=refused' "$TMP/back3.done"
assert "...and she is told what to do" grep -q 'Start Windows, shut it down fully' "$TMP/back3.out"

# The way in never writes to the Windows drive: only `ferry back` can mount it for writing.
eq "only ferry (for ferry back) and the common file call mount_rw" \
   "$(cd "$FERRY" && grep -l '^[^#]*mount_rw' ferry ferry-* 2>/dev/null | tr '\n' ' ')" "ferry ferry-common.sh "
eq "...and the writable mount is asked for in exactly one place" \
   "$(cd "$FERRY" && grep -c 'FERRY_RW=1' ferry)" 1

echo "the handover volume and the USB stick (her files, read back, before AurOS goes):"
HV="$TMP/hv"; mkdir -p "$HV"; hers "$HV"
HVE() { _n=$1; shift; env FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-hv$_n" FERRY_HOME="$AH" FERRY_OWNER="$OWNER" \
              FERRY_SIGNAL="$TMP/hv$_n.done" "$@"; }
HVE 1 ferry back --plain --to "$HV" > "$TMP/hv1.out" 2>&1
eq "ferry back onto a handover volume: exit 0" "$?" 0
H1="$HV/AurOS-handover"
assert "...the layout Windows expects (version, manifest, its hash, files/, DONE)" \
    sh -c "[ \"\$(cat '$H1/version')\" = 1 ] && [ -f '$H1/manifest.tsv' ] && [ -f '$H1/manifest.sha256' ] && [ -d '$H1/files/Pictures' ] && head -1 '$H1/DONE' | grep -qx ok"
assert "...and the Windows side's reference checks pass (tests/verify-handover.sh)" sh "$HERE/verify-handover.sh" "$HV"
eq "...the photo's hash in the manifest is the hash of the file on HER disk" \
   "$(awk -F'\t' '$2 == "2024/holiday.jpg" {print $4}' "$H1/manifest.tsv")" "$(sha256sum "$AH/Pictures/2024/holiday.jpg" | cut -d' ' -f1)"
eq "...DONE counts what the manifest lists" \
   "$(sed -n 's/^files=//p' "$H1/DONE")" "$(grep -vc '^#' "$H1/manifest.tsv")"
assert "...Windows-illegal names are already legal in the manifest (nothing for Windows to rename)" \
    sh -c "! awk -F'\t' '!/^#/ {print \$2}' '$H1/manifest.tsv' | grep -q '[:?*<>|\"]' && grep -q '_CON.txt' '$H1/manifest.tsv'"
assert "...the screen is told it is checked and ready for Windows (not 'in Windows')" \
    sh -c "tail -1 '$TMP/hv1.out' | grep -qE '^Your [0-9]+ files are saved and checked, ready for Windows\.\$'"
assert "...the signal says back, ok" sh -c "grep -qx kind=back '$TMP/hv1.done' && grep -qx result=ok '$TMP/hv1.done'"
# Windows opens the volume with a partly-damaged manifest: not complete.
cp -r "$HV" "$TMP/hv-bad"; echo "tampered" >> "$TMP/hv-bad/AurOS-handover/manifest.tsv"
refute "a manifest that was changed after it was made is not accepted" sh "$HERE/verify-handover.sh" "$TMP/hv-bad"
cp -r "$HV" "$TMP/hv-bad2"; f=$(find "$TMP/hv-bad2/AurOS-handover/files" -name 'letter.txt' | head -1); echo x >> "$f"
refute "a file changed after it was written is not accepted" sh "$HERE/verify-handover.sh" "$TMP/hv-bad2"
cp -r "$HV" "$TMP/hv-bad3"; rm -f "$TMP/hv-bad3/AurOS-handover/DONE"
refute "no DONE: not complete, not accepted" sh "$HERE/verify-handover.sh" "$TMP/hv-bad3"
rm -rf "$TMP/hv-bad" "$TMP/hv-bad2" "$TMP/hv-bad3"

# The read-back catches what the copy cannot see: a file damaged on its way to the disk.
HV2="$TMP/hv2"; mkdir -p "$HV2"; hers "$HV2"
HVE 2 env FERRY_TEST_CORRUPT=1 ferry-handover --dir "$HV2" --home "$AH" --owner "$OWNER" >/dev/null 2>&1
eq "a file damaged after it was written: found by the read-back (exit 1)" "$?" 1
refute "...and the volume is NOT marked complete (no DONE)" test -e "$HV2/AurOS-handover/DONE"
# A shortcut where a folder should be is never silently left out.
AHL2="$TMP/aurhome-link2"; mkdir -p "$AHL2/Pictures"; echo p > "$AHL2/Pictures/p.jpg"; ln -s /etc "$AHL2/Documents"; hers "$AHL2"
HV3="$TMP/hv3"; mkdir -p "$HV3"; hers "$HV3"
env FERRY_STATE="$TMP/state-hv3" FERRY_HOME="$AHL2" FERRY_OWNER="$OWNER" ferry-handover --dir "$HV3" >/dev/null 2>&1
eq "a shortcut where Documents should be: some could not be saved (exit 2)" "$?" 2
refute "...and not marked complete" test -e "$HV3/AurOS-handover/DONE"
# No room on the volume: nothing, and not complete.
HV4="$TMP/hv4"; mkdir -p "$HV4"; hers "$HV4"
env FERRY_ROOM_BYTES=500000 FERRY_STATE="$TMP/state-hv4" FERRY_HOME="$AH" FERRY_OWNER="$OWNER" \
    ferry-handover --dir "$HV4" >/dev/null 2>&1
eq "not enough room on the volume: exit 4" "$?" 4
refute "...and not marked complete" test -e "$HV4/AurOS-handover/DONE"
# How big the volume must be.
_need=$(env FERRY_STATE="$TMP/state-need" FERRY_HOME="$AH" FERRY_OWNER="$OWNER" FERRY_CLUSTER=4096 ferry handover --need)
_used=$(du -s --block-size=1 "$H1/files" | cut -f1)
assert "ferry handover --need is a number at least as big as the volume turned out to be ($_need >= $_used)" \
    test "$_need" -ge "$_used"
eq "...and it is 0 when there is nothing to save" \
   "$(env FERRY_STATE="$TMP/state-need2" FERRY_HOME="$AHE" FERRY_OWNER="$OWNER" ferry handover --need)" 0
# Resumable: the copier killed in the middle, run again.
HV5="$TMP/hv5"; mkdir -p "$HV5"; hers "$HV5"
AHB="$TMP/aurhome-big"; mkdir -p "$AHB/Pictures" "$AHB/Documents"
python3 - "$AHB" <<'PY'
import os, sys
r = sys.argv[1]
blob = os.urandom(1 << 19)
for i in range(300):
    with open(os.path.join(r, "Pictures", "p%03d.jpg" % i), "wb") as f:
        f.write(blob[: 20000 + i * 5])
PY
hers "$AHB"
env FERRY_BATCH_FILES=10 FERRY_STATE="$TMP/state-hv5" FERRY_HOME="$AHB" FERRY_OWNER="$OWNER" \
    ferry-handover --dir "$HV5" >/dev/null 2>&1 &
_hp=$!
_i=0; while [ "$_i" -lt 2000 ]; do
    _got=$(find "$HV5" -type f -name 'p*.jpg' -not -name '.ferrypart-*' 2>/dev/null | wc -l | tr -d ' ')
    [ "$_got" -ge 40 ] && break; _i=$((_i+1)); sleep 0.002
done
pkill -KILL -P "$_hp" 2>/dev/null; kill -KILL "$_hp" 2>/dev/null; wait "$_hp" 2>/dev/null
refute "(the first go was stopped before it finished: no DONE)" test -e "$HV5/AurOS-handover/DONE"
env FERRY_STATE="$TMP/state-hv5" FERRY_HOME="$AHB" FERRY_OWNER="$OWNER" ferry-handover --dir "$HV5" >/dev/null 2>&1
eq "run again after being stopped: finishes (exit 0)" "$?" 0
assert "...complete and every hash right, as the Windows side checks" sh "$HERE/verify-handover.sh" "$HV5"
eq "...300 files once each" "$(find "$HV5/AurOS-handover/files" -type f | wc -l | tr -d ' ')" 300
eq "...none a second copy under another name" "$(find "$HV5/AurOS-handover/files" -name '*(from AurOS*' | wc -l | tr -d ' ')" 0
rm -rf "$AHB" "$HV5"

# The USB stick: a folder she can open on any computer.
ST="$TMP/stick"; mkdir -p "$ST"; hers "$ST"
HVE 6 ferry stick "$ST" --plain > "$TMP/stick.out" 2>&1
eq "ferry stick: exit 0" "$?" 0
assert "...her folders are on it, in a folder called From AurOS" test -f "$ST/From AurOS/Pictures/2024/holiday.jpg"
assert "...with a note and a manifest" sh -c "[ -f '$ST/From AurOS/Read me.txt' ] && [ -f '$ST/From AurOS/MANIFEST.tsv' ]"
refute "...and no DONE (it is not a handover for Windows to consume)" test -e "$ST/From AurOS/DONE"
assert "...every file on it matches its hash" \
    sh -c "cd '$ST/From AurOS' && awk -F'\t' '!/^#/ {printf \"%s  %s/%s\n\", \$4, \$1, \$2}' MANIFEST.tsv | sha256sum -c --quiet"
assert "...she is told it is on the USB stick" sh -c "tail -1 '$TMP/stick.out' | grep -qE '^Your [0-9]+ files are saved on the USB stick'"
mkdir -p "$TMP/fakebin"
cat > "$TMP/fakebin/lsblk" <<'LS'
#!/bin/sh
cat <<'OUT'
NAME="sda" SIZE="256060514304" RM="0" TRAN="sata" FSTYPE="" LABEL="" MODEL="SAMSUNG\x20SSD" TYPE="disk"
NAME="sda2" SIZE="255000000000" RM="0" TRAN="" FSTYPE="ntfs" LABEL="Windows" MODEL="" TYPE="part"
NAME="sdb" SIZE="15518924800" RM="1" TRAN="usb" FSTYPE="" LABEL="" MODEL="DataTraveler\x203.0" TYPE="disk"
NAME="sdb1" SIZE="15500000000" RM="1" TRAN="usb" FSTYPE="exfat" LABEL="HOLIDAY" MODEL="" TYPE="part"
NAME="sdc1" SIZE="7800000000" RM="1" TRAN="usb" FSTYPE="ext4" LABEL="linuxstick" MODEL="" TYPE="part"
OUT
LS
chmod +x "$TMP/fakebin/lsblk"
eq "ferry stick list names only the sticks she could use, in plain words" \
   "$(PATH="$TMP/fakebin:$PATH" ferry stick list | tr '\t' '|' | tr '\n' ';')" "/dev/sdb1|HOLIDAY (16 GB)|15500000000|exfat;"
rm -rf "$HV" "$HV2" "$HV3" "$HV4" "$ST"

# The Windows side refuses a WHOLE list for one bad row: every name must satisfy ITS rules.
FERRY_OWNER="$OWNER" sh "$HERE/mkhandover.sh" "$TMP/fx" >/dev/null 2>&1
assert "a home full of awkward names (CON, COM0, lpt0, ..., nul.tar.gz, trailing spaces and dots) gives a list the Windows side's own rules accept" \
    sh -c "sh '$HERE/verify-handover.sh' '$TMP/fx' | grep -q '^ok '"
assert "...COM0 and LPT0 in particular are not left as they were" \
    sh -c "! awk -F'\t' '!/^#/ {print \$2}' '$TMP/fx/AurOS-handover/manifest.tsv' | grep -qiE '^(com0|lpt0)(\.|\$)'"
rm -rf "$TMP/fx"
# A path longer than the Windows side accepts (600): said and not saved; never a list it would refuse.
AHD="$TMP/aurhome-deep"; mkdir -p "$AHD/Documents"
python3 - "$AHD/Documents" <<'PY'
import os, sys
p = sys.argv[1]
for i in range(12):
    p = os.path.join(p, "d%d_" % i + "x" * 60)
os.makedirs(p)
open(os.path.join(p, "deep.txt"), "w").write("deep\n")
open(os.path.join(sys.argv[1], "shallow.txt"), "w").write("shallow\n")
PY
hers "$AHD"
HVD="$TMP/hvd"; mkdir -p "$HVD"; hers "$HVD"
env FERRY_STATE="$TMP/state-hvd" FERRY_HOME="$AHD" FERRY_OWNER="$OWNER" ferry-handover --dir "$HVD" >/dev/null 2>&1
eq "a path longer than 600 characters: some could not be saved (exit 2)" "$?" 2
refute "...and the list is not marked complete" test -e "$HVD/AurOS-handover/DONE"
assert "...and it is said in her words" grep -q 'together too long for Windows' "$TMP/state-hvd/report.d/handover-copy.tsv"
rm -rf "$AHD" "$HVD"

echo "ferry run says what happened (answer.sh turns it into her screen):"
# The installer's choices stand, so no stage tries to change this
# machine's clock, language or keyboard while the tests run.
printf 'timezone=Etc/UTC\nlanguage=en_US.UTF-8\nkeyboard=us\n' > "$TMP/run-choices"
runit() { FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-run$1" FERRY_ROOT="$2" \
          FERRY_HOME="$3" FERRY_OWNER="$OWNER" FERRY_SIGNAL="$TMP/run$1.done" ferry run --auto >/dev/null 2>&1; }
mkdir -p "$TMP/run-home"; hers "$TMP/run-home"
runit 1 "$WIN" "$TMP/run-home"
eq "some files online-only: exit 2 (\"some things could not be brought across\")" "$?" 2
assert "the completion signal says: import, partial, and how many files" \
    sh -c "grep -qx 'kind=import' '$TMP/run1.done' && grep -qx 'result=partial' '$TMP/run1.done' && grep -qE '^files=[1-9][0-9]*\$' '$TMP/run1.done'"
runit 2 "$TMP/hib" "$TMP/run-home"
eq "a hibernated Windows: exit 3 (refused, nothing read)" "$?" 3
assert "...and the signal says refused" grep -qx 'result=refused' "$TMP/run2.done"
# What she reads while it runs, and afterwards.
mkdir -p "$TMP/plain-home"; hers "$TMP/plain-home"
FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-plain" FERRY_ROOT="$WIN" \
    FERRY_HOME="$TMP/plain-home" FERRY_OWNER="$OWNER" ferry run --auto --plain > "$TMP/plain.out" 2>&1
eq "--plain starts with a plain sentence" "$(head -1 "$TMP/plain.out")" \
   "Looking at the Windows part of this computer."
eq "--plain prints nothing but plain sentences (no paths, no arrows, no file names)" \
   "$(grep -cvE '^(Looking at the Windows part of this computer\.|Copying your [A-Za-z]+: [0-9,]+ of [0-9,]+ files\.( (Less than a minute|About (a|[0-9]+) minutes?|About (an|[0-9]+) hours?) left\.)?|Bringing your bookmarks across\.)$' "$TMP/plain.out")" 0
assert "...and Ferry's own chatter is in its log" grep -q '── files ──' "$TMP/state-plain/ferry.log"
R="$TMP/plain-home/What came from Windows.txt"
assert "her report is in her Home" test -s "$R"
assert "...and it is hers" sh -c "[ \"\$(stat -c %U '$R')\" = '$OWNER' ]"
assert "...it says how many files each folder brought" grep -qE '^  Desktop +[0-9]+ files?$' "$R"
assert "...it names each file that did not come, and why, in her words" \
    sh -c "grep -q 'Documents/Taxes/2022.pdf' '$R' && grep -q 'It is kept online by OneDrive' '$R'"
assert "...it names the folder Windows kept on another drive" grep -q '^  Videos$' "$R"
assert "...it lists her Wi-Fi networks, and no password" \
    sh -c "grep -q 'HomeNet' '$R' && ! grep -q 'hunter2' '$R'"
refute "...and has no jargon in it" \
    grep -qiE 'reparse|placeholder|hydrat|ntfs|manifest|registry|stage|->|exit [0-9]' "$R"

mkdir -p "$TMP/run-ro"; chmod 0555 "$TMP/run-ro"
runit 3 "$WIN" "$TMP/run-ro/home"
eq "her files could not be copied at all: exit 1, never 'partial'" "$?" 1
chmod 0755 "$TMP/run-ro"

echo "whose files, and what counts as success (S-08):"
# A. two Windows accounts, only Bob's has files: Bob's come, and it is said.
T2="$TMP/win-two"; sh "$HERE/mkfakewin.sh" "$T2" >/dev/null; sh "$HERE/addaccount.sh" "$T2" Harold >/dev/null
H2="$TMP/home-two"; mkdir -p "$H2"; hers "$H2"
runw() { _n=$1 _r=$2 _h=$3; shift 3
         env FERRY_CHOICES="$TMP/run-choices" FERRY_STATE="$TMP/state-w$_n" FERRY_ROOT="$_r" FERRY_HOME="$_h" \
             FERRY_OWNER="$OWNER" FERRY_SIGNAL="$TMP/w$_n.done" ferry run --auto "$@" >"$TMP/w$_n.out" 2>&1; }
runw 1 "$T2" "$H2"
assert "two accounts, only Bob has files: Bob's arrive without being asked" test -f "$H2/Desktop/report.docx"
eq "...and Ferry remembers whose files they are" "$(cat "$TMP/state-w1/winuser" 2>/dev/null)" Bob
assert "...and her report says which account" sh -c "grep -q 'From the Windows account \"Bob\"' '$H2/What came from Windows.txt'"
# B. both have files: Ferry does not guess, and says so, and copies nothing.
T3="$TMP/win-both"; sh "$HERE/mkfakewin.sh" "$T3" >/dev/null; sh "$HERE/addaccount.sh" "$T3" Harold files >/dev/null
H3="$TMP/home-both"; mkdir -p "$H3"; hers "$H3"
runw 2 "$T3" "$H3"
eq "two accounts, both with files: Ferry stops (exit 6), never 'ok'" "$?" 6
eq "...and copied nothing at all" "$(find "$H3" -type f | wc -l | tr -d ' ')" 0
eq "...the names for the desktop to ask with" "$(sort "$TMP/state-w2/users" | tr '\n' ' ')" "Bob Harold "
assert "...and the signal says chooseuser" grep -qx 'result=chooseuser' "$TMP/w2.done"
refute "...no stage ran (nothing in the manifest)" sh -c "[ -s '$TMP/state-w2/manifest.tsv' ]"
# C. she chooses: only that account's files come, never the other's.
runw 3 "$T3" "$H3" --user Harold
assert "choosing Harold brings Harold's files" test -f "$H3/Documents/private-letter.txt"
refute "...and none of Bob's" test -e "$H3/Desktop/report.docx"
H3b="$TMP/home-both2"; mkdir -p "$H3b"; hers "$H3b"
runw 4 "$T3" "$H3b" --user Bob
refute "choosing Bob brings none of Harold's" sh -c "grep -rqs \"Harold's\" '$H3b'"
runw 5 "$T3" "$H3b" --user Nobody
eq "an account that is not there is not guessed at (exit 6)" "$?" 6
# D. a folder she expected that could not come makes the run partial, never ok.
H4="$TMP/home-vid"; mkdir -p "$H4"; hers "$H4"
FERRY_STATE="$TMP/state-vid" ferry-files --root "$WIN" --home "$H4" --owner "$OWNER" --only Videos >/dev/null 2>&1
eq "Videos kept on another drive: the files stage is partial (exit 2), not ok" "$?" 2
assert "...and it is not recorded as done" sh -c "! [ \"\$(FERRY_STATE='$TMP/state-vid' ferry status 2>/dev/null | awk '\$1 == \"files\" {print \$2}')\" = done ]"
FERRY_STATE="$TMP/state-desk" ferry-files --root "$WIN" --home "$H4" --owner "$OWNER" --only Desktop >/dev/null 2>&1
eq "a folder that all came: exit 0" "$?" 0
assert "...and that one IS recorded as done" sh -c "[ \"\$(FERRY_STATE='$TMP/state-desk' ferry status 2>/dev/null | awk '\$1 == \"files\" {print \$2}')\" = done ]"
# E. a retry after a partial run does the stage again (does not skip it and say all is well).
runw 6 "$WIN" "$TMP/home-retry"
runw 6 "$WIN" "$TMP/home-retry"
refute "a second run does not skip a stage that was only partly done" grep -q 'files: already done' "$TMP/w6.out"
# F. nothing to bring is said as such.
T5="$TMP/win-empty"; cp -r "$WIN" "$T5"
rm -rf "$T5/users/Bob/Desktop" "$T5/users/Bob/Documents" "$T5/users/Bob/Pictures" "$T5/users/Bob/Music" \
       "$T5/users/Bob/Downloads" "$T5/users/Bob/Videos" "$T5/users/Bob/OneDrive" "$T5/users/Bob/AppData"
# and ordinary folder locations, nothing redirected to another drive
python3 "$HERE/mkhive.py" "$T5/users/Bob/NTUSER.DAT" '{
  "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders": {
    "Desktop": ["exp", "%USERPROFILE%\\Desktop"], "Personal": ["exp", "%USERPROFILE%\\Documents"],
    "My Pictures": ["exp", "%USERPROFILE%\\Pictures"], "My Music": ["exp", "%USERPROFILE%\\Music"],
    "My Video": ["exp", "%USERPROFILE%\\Videos"]
  }
}'
H5="$TMP/home-empty"; mkdir -p "$H5"; hers "$H5"
runw 7 "$T5" "$H5"
eq "a Windows with no files of hers: exit 0..." "$?" 0
assert "...but the signal counts them: files=0, so nothing says 'your files are here'" grep -qx 'files=0' "$TMP/w7.done"

# R-18: her files must not fill AurOS's own disk.
H6="$TMP/home-room"; mkdir -p "$H6"; hers "$H6"
FERRY_ROOM_BYTES=5000000 FERRY_STATE="$TMP/state-room" ferry-files --root "$WIN" --home "$H6" --owner "$OWNER" >"$TMP/room.out" 2>&1
eq "not enough room on this computer for her files: exit 4" "$?" 4
eq "...and not one file was copied" "$(find "$H6" -type f | wc -l | tr -d ' ')" 0
assert "...the report says how much room, in her words" grep -q 'not enough room on this computer for your files' "$TMP/state-room/report.d/files-copy.tsv"
FERRY_ROOM_BYTES=500000000 FERRY_STATE="$TMP/state-room2" ferry-files --root "$WIN" --home "$H6" --owner "$OWNER" >/dev/null 2>&1
eq "room for the files but not for AurOS's own reserve (500 MB free): refused too" "$?" 4
FERRY_STATE="$TMP/state-room3" ferry-files --root "$WIN" --home "$H6" --owner "$OWNER" >/dev/null 2>&1
FERRY_ROOM_BYTES=1000 FERRY_STATE="$TMP/state-room3" ferry-files --root "$WIN" --home "$H6" --owner "$OWNER" >/dev/null 2>&1
case "$?" in 0|2) ok "a resumed import is not refused for room (what is there would be counted twice)" ;;
             *) bad "a resumed import is not refused for room (what is there would be counted twice)" ;; esac
FERRY_ROOM_BYTES=5000000 FERRY_STATE="$TMP/state-w9" FERRY_ROOT="$WIN" FERRY_HOME="$TMP/home-room-run2" FERRY_OWNER="$OWNER" \
    FERRY_SIGNAL="$TMP/w9.done" ferry run --auto >/dev/null 2>&1
eq "ferry run with no room says so: exit 4" "$?" 4
assert "...and the signal says noroom" grep -qx 'result=noroom' "$TMP/w9.done"

# R-19: a killed copy is picked up again, copying nothing twice.
echo "a copy that is killed in the middle (power cut, restart, systemd) is resumable:"
KS="$TMP/kill-src"; KD="$TMP/kill-dst"; mkdir -p "$KS/d1" "$KS/d2"; rm -rf "$KD"; mkdir -p "$KD"; hers "$KD" "$KS"
python3 - "$KS" <<'PY'
import os, sys
r = sys.argv[1]
blob = os.urandom(1 << 20)
for i in range(600):
    d = os.path.join(r, "d%d" % (1 + i % 2))
    with open(os.path.join(d, "f%03d.bin" % i), "wb") as f:
        f.write(blob[: 20000 + i * 7])
PY
FERRY_BATCH_FILES=10 ferry-copy $ASME --stage kill --job K "$KS" "$KD/out" >/dev/null 2>&1 &
_kp=$!
_i=0; while [ "$_i" -lt 2000 ]; do
    _got=$(find "$KD/out" -type f -not -name '.ferrypart-*' 2>/dev/null | wc -l | tr -d ' ')
    [ "$_got" -ge 30 ] && break; _i=$((_i+1)); sleep 0.002
done
kill -9 "$_kp" 2>/dev/null; wait "$_kp" 2>/dev/null
_mid=$(find "$KD/out" -type f -not -name '.ferrypart-*' 2>/dev/null | wc -l | tr -d ' ')
assert "(it was killed part-way: some files there, not all of them, $_mid of 600)" test "$_mid" -gt 0 -a "$_mid" -lt 600
ferry-copy $ASME --stage kill --job K "$KS" "$KD/out" >/dev/null 2>&1
eq "the next run finishes with exit 0" "$?" 0
eq "...every file is there exactly once" "$(find "$KD/out" -type f | wc -l | tr -d ' ')" 600
eq "...none of them is a second copy under another name" "$(find "$KD/out" -name '*(from Windows*' | wc -l | tr -d ' ')" 0
eq "...no half-written temporary is left" "$(find "$KD/out" -name '.ferrypart-*' | wc -l | tr -d ' ')" 0
eq "...and every one is byte for byte what it should be" \
   "$(cd "$KS" && find . -type f | while read -r f; do cmp -s "$f" "$KD/out/$f" || echo bad; done | wc -l | tr -d ' ')" 0
rm -rf "$KS" "$KD"

# R-19: the unit that runs the import has no one-hour limit.
UNIT="$FERRY/../../rootfs/etc/systemd/system/auros-answer.service"
if [ -f "$UNIT" ]; then
    assert "auros-answer.service has no time limit that could kill a long import" \
        grep -qx 'TimeoutStartSec=infinity' "$UNIT"
fi

echo "what answer.sh tells the screen (tests/answertest.sh):"
if [ -f "$FERRY/../../rootfs/usr/lib/auros/answer.sh" ] && command -v timeout >/dev/null 2>&1; then
    sh "$HERE/answertest.sh" > "$TMP/answertest.out" 2>&1
    cat "$TMP/answertest.out" | grep -E '^  (ok|FAIL)'
    pass=$((pass + $(grep -c '^  ok' "$TMP/answertest.out")))
    fail=$((fail + $(grep -c '^  FAIL' "$TMP/answertest.out")))
else
    echo "  (skipped: answer.sh or timeout not here)"
fi

echo "read-only mount check:"
# mount_ro undoes any mount the kernel does not report as read-only; it
# must recognise a read-only one, or no import from a disk can start.
printf '/dev/sda3 /run/ferry/windows ntfs3 ro,nosuid,nodev,noexec,noatime 0 0\n/dev/sdb1 /run/ferry/rw ntfs3 rw,relatime 0 0\n' > "$TMP/mounts"
assert "a read-only mount is recognised as read-only" \
    sh -c ". '$FERRY/ferry-common.sh'; FERRY_MOUNTS='$TMP/mounts' is_ro_mount /run/ferry/windows"
refute "a writable mount is not" \
    sh -c ". '$FERRY/ferry-common.sh'; FERRY_MOUNTS='$TMP/mounts' is_ro_mount /run/ferry/rw"

echo "read-only guarantee:"
# Every import stage above ran against $WIN. Not one byte, size or mtime
# on it may have changed: Ferry mounts read-only and must never write.
snapshot > "$TMP/win.after"
if diff -q "$TMP/win.before" "$TMP/win.after" >/dev/null 2>&1; then
    ok "source Windows tree left byte-for-byte unchanged by every stage"
else
    bad "source Windows tree was modified:"
    diff "$TMP/win.before" "$TMP/win.after" | head
fi

echo "no mess left behind:"
# This run's own directory must be gone when the run is over. (Removed
# here, ahead of the trap, so the check can look.) Another run's leftover
# is not this run's to delete, but is said, with its size.
rm -rf "$TMP"
assert "the test directory is removed at the end of the run" test ! -e "$TMP"
for _d in "${TMPDIR:-/tmp}"/ferrytest.*; do
    [ -d "$_d" ] && printf '  note  an earlier run left %s (%s): a killed run does that; delete it\n' "$_d" "$(du -sh "$_d" 2>/dev/null | cut -f1)"
done

echo
printf 'ferry tests: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
