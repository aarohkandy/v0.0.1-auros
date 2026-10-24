#!/bin/sh
# renametest -- what she calls her programs, through build/forge's own rename code.
#
# journeytest and storetest draw the shell with a fixture .desktop that is already called
# "Internet"; nothing proved that the build makes it so. This takes the rename loop and its
# table out of build/forge (the lines themselves, not a copy), lays down stand-in .desktop
# files the way Ubuntu's packages write them (packagers' names, translations, comments), runs
# the loop, and reads what comes out: the names she is told in the installer and on the site
# (Internet, Your files, Get more programs, ...), no translated name left beside them, and the
# rest of the file untouched.
#
#   sh tools/renametest.sh        (from anywhere)
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
fail=0; n=0
ok()  { n=$((n+1)); printf '  %-70s ok\n' "$1"; }
bad() { n=$((n+1)); fail=$((fail+1)); printf '  %-70s FAIL\n' "$1"; shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

FORGE=${FORGE:-build/forge}
awk '/^    install -d "\$RFS\/usr\/local\/share\/applications"$/ { f = 1 } f { print } f && /^EOL$/ { exit }' "$FORGE" > "$T/loop.sh"
[ -s "$T/loop.sh" ] || { echo "renametest: no rename loop in $FORGE"; exit 2; }
# the table, as the loop reads it
awk '/^EOL$/ { exit } f { print } /<<.EOL.$/ { f = 1 }' "$T/loop.sh" > "$T/table"
rows=$(grep -c . "$T/table")
[ "$rows" -ge 10 ] || { echo "renametest: the table has $rows rows"; exit 2; }

RFS="$T/rfs"; mkdir -p "$RFS/usr/share/applications"
while IFS='|' read -r desk nm cm; do
    [ -n "$desk" ] || continue
    cat > "$RFS/usr/share/applications/$desk.desktop" <<DESK
[Desktop Entry]
Version=1.0
Type=Application
Name=Packager's Name For $desk
Name[fr]=Nom du paquet
GenericName[fr]=Generique
Comment=The packager's words
Comment[fr]=Les mots du paquet
Exec=/usr/bin/$desk %U
Icon=$desk
Categories=Utility;
DESK
done < "$T/table"
warn() { echo "WARN $*"; }
export RFS
sh -c "warn() { echo WARN \"\$*\"; }; . '$T/loop.sh'" > "$T/out" 2>&1 || { cat "$T/out"; echo "renametest: the loop failed"; exit 2; }

echo "what she calls her programs, as build/forge makes it"
while IFS='|' read -r desk nm cm; do
    [ -n "$desk" ] || continue
    f="$RFS/usr/local/share/applications/$desk.desktop"
    if [ -f "$f" ] && grep -qx "Name=$nm" "$f" && grep -qx "Comment=$cm" "$f"; then ok "$desk is \"$nm\""
    else bad "$desk is \"$nm\"" "$(grep -E '^(Name|Comment)=' "$f" 2>/dev/null | tr '\n' ' ')"; fi
    if [ -f "$f" ] && ! grep -qE '^(Name|Comment|GenericName)\[' "$f" && grep -qx "Exec=/usr/bin/$desk %U" "$f"; then :
    else bad "$desk keeps no translated name beside it, and its Exec" "$(grep -E '\[|^Exec' "$f" 2>/dev/null | tr '\n' ' ')"; fi
done < "$T/table"
# The names that are promised elsewhere (the installer's pictures, the site, the help): by name.
want() { # desk name
    if grep -qx "$1|$2|.*" "$T/table"; then ok "the table says $1 is \"$2\""; else bad "the table says $1 is \"$2\"" "$(grep "^$1|" "$T/table")"; fi
}
want firefox Internet
want thunar "Your files"
want org.gnome.Software "Get more programs"
want mpv "Music and video"
want org.xfce.ristretto Photos
want org.xfce.mousepad Notes
want xarchiver "Zip files"
want system-config-printer Printers
# a name in the table that matches no installed file renames nothing, and says so
rm -f "$RFS/usr/share/applications/firefox.desktop" "$RFS/usr/local/share/applications/firefox.desktop"
sh -c "warn() { echo WARN \"\$*\"; }; . '$T/loop.sh'" > "$T/out2" 2>&1
if grep -q "no firefox.desktop installed" "$T/out2" && [ ! -f "$RFS/usr/local/share/applications/firefox.desktop" ]; then ok "no firefox.desktop installed: said so, nothing pretended"
else bad "no firefox.desktop installed: said so, nothing pretended" "$(head -3 "$T/out2")"; fi
echo
if [ $fail = 0 ]; then echo "renametest: $n checks, all ok"; else echo "renametest: $fail of $n FAILED"; fi
exit $((fail > 0))
