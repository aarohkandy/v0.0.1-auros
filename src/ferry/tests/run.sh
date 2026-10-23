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

TMP=$(mktemp -d "${TMPDIR:-/tmp}/ferrytest.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
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
python3 -c 'import sys; open(sys.argv[1], "wb").write(b"x" * (64 << 20))' "$P/src/a-big.bin"
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
eq "what she reads is plain" "$(grep -cvE '^(Getting ready to save your files in Windows\.|Saving your [A-Za-z]+ in Windows: [0-9,]+ of [0-9,]+ files\.( (Less than a minute|About (a|[0-9]+) minutes?|About (an|[0-9]+) hours?) left\.)?|Your files are saved in Windows, in the folder "From AurOS"\.)$' "$TMP/back1.out")" 0
assert "...ending with the sentence that says it is done" sh -c "tail -1 '$TMP/back1.out' | grep -q 'Your files are saved in Windows'"
assert "the completion signal says: back, ok, and how many files" \
    sh -c "grep -qx 'kind=back' '$TMP/back1.done' && grep -qx 'result=ok' '$TMP/back1.done' && grep -qE '^files=[0-9]+\$' '$TMP/back1.done' && ! grep -qx 'files=0' '$TMP/back1.done'"

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

echo
printf 'ferry tests: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
