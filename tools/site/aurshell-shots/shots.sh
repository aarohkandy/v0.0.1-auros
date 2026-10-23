#!/bin/bash
# The website's pictures of AurOS (website/assets/shots/look-*.jpg):
# aurshell --png in Sandstone, the theme every look gets (src/aurbridge/wizard.c).
# On a Linux box with docker, from the repository root:
#   docker run --rm --memory 4g -e HOST_UID=$(id -u) -e HOST_GID=$(id -g) \
#     -v "$PWD":/w -w /w ubuntu:24.04 bash /w/tools/site/aurshell-shots/shots.sh
# Output: /w/shots-out/*.png (SIZE, default 1280x800: the UI big enough to read
# in the page). The names below are build/forge's rename table, word for word.
# Convert to JPEG quality 82 for the site.
set -eu
export DEBIAN_FRONTEND=noninteractive
timeout 300 apt-get update -qq
timeout 300 apt-get install -y -qq --no-install-recommends gcc libc6-dev pkg-config \
    libwayland-dev libxkbcommon-dev libdbus-1-dev fonts-inter fonts-jetbrains-mono fonts-dejavu-core >/dev/null
OUT=${OUT:-/w/shots-out}; rm -rf $OUT; mkdir -p $OUT

# Fonts, the way build/forge resolves them (stage 3 resolve_font, RFS=/).
RFS= ; note() { echo "font: $*"; }
eval "$(sed -n '/^    resolve_font() {/,/^    }/p' /w/build/forge)"
install -d /usr/share/auros/fonts
resolve_font Inter Inter || resolve_font DejaVuSans Inter
resolve_font JetBrainsMono JetBrainsMono || resolve_font DejaVuSansMono JetBrainsMono
# Then every face the shipped themes name, weight by weight, as build/forge
# does (stage_overlay, after 890dd0e: headings SemiBold, text Regular).
for f in $(sed -nE 's/^font_(display|text|text_bold)="([^"]*)".*/\2/p' /w/themes/*.theme | sort -u); do
    resolve_font "$f" "$f" 1 || resolve_font "$f" "$f" || resolve_font DejaVuSans "$f" || true
done
ls -l /usr/share/auros/fonts

# shell.conf from Sandstone, through aurora, nowhere near /etc.
AURORA_THEMES=/w/themes AURORA_TEMPLATES=/w/themes/templates AURORA_STATE=/tmp/st \
AURORA_CACHE=/tmp/cache AURORA_OUTDIR=/tmp/conf AURORA_ONLY=shell.conf sh /w/src/aurora/aurora set sandstone
grep -E '^(theme_name|col_bg |col_accent |font_text )' /tmp/conf/shell.conf

# aurshell, the build/forge line.
cd /w/src
SRC=$(for f in aurshell/*.c aurshell/layouts/*.c; do grep -q '^int main(' $f && continue; printf '%s ' $f; done)
gcc -O2 -std=gnu11 -o /tmp/aurshell aurshell/main.c $SRC \
    aurwl/aurwl.c aurwl/gen/xdg-shell-protocol.c aurwl/gen/xdg-decoration-protocol.c \
    aurwl/gen/viewporter-protocol.c aurwl/gen/xdg-output-protocol.c \
    common/theme.c common/wall.c common/font.c common/png.c \
    -I aurwl/gen $(pkg-config --cflags dbus-1) -lwayland-server -lxkbcommon $(pkg-config --libs dbus-1) -lm

# The machine: this image's programs under build/forge's names (tools/storetest.c ship()).
A=/tmp/apps; mkdir -p $A
e() { printf '[Desktop Entry]\nType=Application\nName=%s\nComment=%s\nCategories=%s\nExec=%s\n' "$2" "$3" "$4" "$5" > $A/$1.desktop; }
e firefox "Internet" "Websites, news and shopping" "Network;WebBrowser;" "true firefox %u"
e thunar "Your files" "Everything you have saved, in folders" "System;FileTools;FileManager;" "true thunar"
e atril "Documents" "Read PDFs and other documents" "GTK;Office;Viewer;" "true atril %U"
e org.xfce.mousepad "Notes" "Write and read plain notes" "Utility;TextEditor;" "true mousepad %F"
e org.xfce.ristretto "Photos" "Look at your pictures" "Graphics;Viewer;" "true ristretto %U"
e mpv "Music and video" "Play a song or a film" "AudioVideo;Audio;Video;Player;" "true mpv %U"
e mate-calc "Calculator" "Add up, and work things out" "Utility;Calculator;" "true mate-calc"
e system-config-printer "Printers" "Set up a printer, or check one you have" "System;Settings;Printing;" "true printers"
e xarchiver "Zip files" "Open a zip file, or make one" "Archiving;Utility;" "true xarchiver %f"
e org.gnome.Software "Software" "Add, remove or update software on this computer" "GNOME;GTK;System;PackageManager;" "true org.gnome.Software %U"

export AUROS_APP_DIRS=$A HOME=/tmp/home AURSHELL_CLOCK=$(date -u -d '2025-10-23 09:41' +%s)
mkdir -p $HOME
cd /w
r() { /tmp/aurshell --shell shells/$2.shell --conf /tmp/conf/shell.conf --policy /nonexistent --png $OUT/$1.png --size ${SIZE:-1280 800} ${3:+--open $3}; }
r taskbar taskbar; r dock dock; r rail rail; r tiles tiles
chown -R ${HOST_UID}:${HOST_GID} $OUT
