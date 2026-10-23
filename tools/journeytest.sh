#!/bin/sh
# journeytest.sh — her first ten minutes, in every look, at both sizes.
#
# Builds tools/journey.c and walks it through the first start ("does
# everything work?"), her desktop, the Internet panel, a program
# installed from "Get more programs" ("VLC media player is ready") and
# that program opening -- then reads back what each screen SAID.
#
# The checks are the failures it was written for: in "One thing at a
# time" a program opened by the store or the ready card stayed behind
# the page of buttons, and in "Like Windows" the program list stayed
# open on top of it (neither archetype had a present()).
#
#   sh tools/journeytest.sh [shots-dir]     (repository root; needs cc,
#                                            pkg-config and libdbus-1-dev)
set -u
cd "$(dirname "$0")/.." || exit 2
T=$(mktemp -d "${TMPDIR:-/tmp}/journey.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM
OUT=${1:-$T/shots}
mkdir -p "$OUT" "$T/apps" "$T/vlc" "$T/st/c"

cc -O2 -std=gnu11 -o "$T/journey" tools/journey.c \
    -DWELCOME_STATE="\"$T/state\"" \
    src/aurshell/welcome.c src/aurshell/run.c src/aurshell/notify.c src/aurshell/osd.c src/aurshell/foot.c \
    src/aurshell/draw.c src/aurshell/shellcommon.c src/aurshell/anim.c \
    src/aurshell/apps.c src/aurshell/layouts/*.c src/common/theme.c \
    src/common/wall.c src/common/font.c src/common/png.c \
    $(pkg-config --cflags --libs dbus-1) -lm || { echo "journey did not build"; exit 2; }

# What build/forge ships, by the names it gives them (tools/storetest.c).
e() { printf '[Desktop Entry]\nType=Application\nName=%s\nComment=%s\nCategories=%s\nExec=true %s\n' \
          "$2" "$3" "$4" "$1" > "$5/$1.desktop"; }
e firefox "Internet" "Websites, news and shopping" "Network;WebBrowser;" "$T/apps"
e thunar "Your files" "Everything you have saved" "System;FileTools;FileManager;" "$T/apps"
e org.gnome.Evince "Documents" "Read PDFs and documents" "GNOME;GTK;Office;Viewer;Graphics;" "$T/apps"
e org.xfce.mousepad "Notes" "Write and read plain notes" "Utility;TextEditor;" "$T/apps"
e org.xfce.ristretto "Photos" "Look at your pictures" "Graphics;Viewer;Photography;" "$T/apps"
e mpv "Music and video" "Play a song or a film" "AudioVideo;Audio;Video;Player;" "$T/apps"
e mate-calc "Calculator" "Add up, and work things out" "Utility;Calculator;" "$T/apps"
e system-config-printer "Printers" "Set up or check a printer" "System;Settings;Printing;" "$T/apps"
e xarchiver "Zip files" "Open or make a zip file" "Archiving;Utility;" "$T/apps"
e org.gnome.Software "Software" "Add, remove or update software" "GNOME;GTK;System;PackageManager;" "$T/apps"
e org.videolan.VLC "VLC media player" "Read, capture, broadcast your multimedia streams" "AudioVideo;Player;" "$T/vlc"

printf 'sandstone\n' > "$T/st/theme"
tail -n +2 themes/templates/shell.conf.tpl | \
    AURORA_THEMES=themes AURORA_TEMPLATES=themes/templates \
    AURORA_STATE="$T/st" AURORA_CACHE="$T/st/c" ./src/aurora/aurora render \
    > "$T/look.conf" 2>/dev/null

fail=0 n=0
ok() { n=$((n + 1)); if [ "$2" = 0 ]; then echo "    ok    $1"; else echo "    FAIL  $1"; fail=1; fi; }
# (A card's words wrap at 22 px type: the sentence is on screen even when it
# is on two lines, so those checks read the shot joined into one line.)
said1() { said "$@" | grep -v '^## ' | tr '\n' ' ' | tr -s ' '; }
said() { awk -v s="$2" -v t="${3:-@@}" '/^== /{on = index($0, s) > 0 || index($0, t) > 0; next} on' "$1"; }

for look in taskbar dock rail tiles; do
    for sz in "1366 768" "1024 600"; do
        set -- $sz
        log="$OUT/$look-$1x$2.txt"
        HOME="$T/home" AUROS_APP_DIRS="$T/apps" AURSHELL_CLOCK=1761206400 \
            "$T/journey" "$T/look.conf" "$look" "$1" "$2" "$OUT" "$T/vlc" > "$log" 2>&1 \
            || { ok "$look $1x$2: the journey ran" 1; continue; }
        at="$look $1x$2"
        said "$log" "-asks.png"    | grep -qx '   Does everything work?';     ok "$at: the first start asks" $?
        # Her files (docs/FERRY.md): whose, while they come, none; the USB stick; why Windows
        # could not hold them. Every sentence on them is read at 18 px or more.
        said "$log" "-whose.png"   | grep -qx '   Whose files?';              ok "$at: several Windows people: Whose files?" $?
        said "$log" "-whose.png"   | grep -qx '   Bob' && said "$log" "-whose.png" | grep -qx '   Harold'
        ok "$at: ...a button for each person" $?
        said1 "$log" "-bringing.png" | grep -q 'Copying your Pictures: 1,200 of 5,000 files. About 4 minutes left.'
        ok "$at: while they come, it says what is happening" $?
        said "$log" "-empty.png"   | grep -qx '   No files to bring across';  ok "$at: nothing to bring: a calm screen, not an error" $?
        ! said "$log" "-empty.png" | grep -q 'did not work';                 ok "$at: ...that does not say it did not work" $?
        said "$log" "-stick-plug.png" | grep -qx '   Plug in a USB stick';    ok "$at: the stick screen asks for the stick" $?
        said "$log" "-stick-pick.png" | grep -qx '   Which USB stick?';       ok "$at: ...and which one" $?
        said "$log" "-stick-pick.png" | grep -q 'SanDisk (16 GB)' && said "$log" "-stick-pick.png" | grep -q 'BACKUP (500 GB)'
        ok "$at: ...each stick by its name and size" $?
        said1 "$log" "-stick-saving.png" | grep -q 'Saving your Pictures: 1,200 of 5,000 files'
        ok "$at: saving says how far it is" $?
        said1 "$log" "-stick-saving.png" | grep -q 'Leave the USB stick in and the computer on'
        ok "$at: ...and to leave the stick in" $?
        said "$log" "-stick-done.png" | grep -qx '   Your files are on the USB stick'; ok "$at: done: the files are on the stick" $?
        said1 "$log" "-stick-done.png" | grep -q 'safe to take the stick out'; ok "$at: ...and it is safe to take it out" $?
        said1 "$log" "-stick-done.png" | grep -q 'Windows is still not put back';  ok "$at: ...and Windows is still not put back (said plainly)" $?
        said "$log" "-blocked.png" | grep -qx '   Windows was not put back';  ok "$at: Windows could not hold her files: it says so" $?
        said1 "$log" "-blocked.png" | grep -q 'too many to carry across to Windows this way'; ok "$at: ...and why" $?
        said "$log" "-blocked.png" | grep -qx '   Save my files to a USB stick first'; ok "$at: ...and offers the stick" $?
        said "$log" "-blocked.png" | grep -qx '   Keep AurOS';                ok "$at: ...or to keep things as they are" $?
        said1 "$log" "-blocked.png" | grep -q 'Nothing was removed'; ok "$at: ...and says nothing was removed" $?
        small=$(for s in whose bringing empty stick-plug stick-pick stick-saving stick-done blocked; do said "$log" "-$s.png" | awk '/^## /{ if ($2 < 21) print }'; done | grep -v "Help\|Wi-Fi\|Settings\|Smaller\|Bigger\|Turn off\|Close this" | wc -l)
        [ "$small" -eq 0 ];                                              ok "$at: nothing on those screens is under 18 px" $?
        said "$log" "-sure.png"    | grep -qx '   Start AurOS from now on';   ok "$at: Yes asks once more before it decides" $?
        ! said "$log" "-asks.png" "-sure.png" | grep -q '…';             ok "$at: the question is never cut short" $?
        # What each answer does is read at 18 px or more (22 tall drawn;
        # a 12 px line is 14), on the screen that decides for good.
        small=$(said "$log" "-asks.png" | awk '/^## /{ if ($2 < 21) print }' | grep -v "Help\|Wi-Fi\|Settings\|Smaller\|Bigger\|Turn off" | wc -l)
        [ "$small" -eq 0 ];                                              ok "$at: nothing on the question is under 18 px" $?
        said "$log" "-asks.png" | grep -q '^   Windows starts the next time this computer starts'
        ok "$at: what No does is on the screen" $?
        said "$log" "-online.png"  | grep -qx '   Press the name of your Wi-Fi.'
        ok "$at: the Wi-Fi page says Wi-Fi, the one name" $?
        # (A dock label wraps onto two lines: both halves are on screen.)
        { said "$log" "-desktop.png" | grep -qx '   Get more programs'; } ||
        { said "$log" "-desktop.png" | grep -qx '   Get more' && said "$log" "-desktop.png" | grep -qx '   programs'; }
        ok "$at: Get more programs is on her desktop" $?
        said "$log" "-desktop.png" | grep -qx '   Wi-Fi';                     ok "$at: the band says Wi-Fi (one name per thing)" $?
        # (The dock draws its favourites without names until C P1-12.)
        [ $look = dock ] || { said "$log" "-desktop.png" | grep -qx '   Internet'
                              ok "$at: the browser is called Internet" $?; }
        said1 "$log" "-ready.png"  | grep -q 'VLC media player is ready';    ok "$at: VLC is ready, and it says so" $?
        said "$log" "-opens.png"   | grep -q 'VLC media player';              ok "$at: VLC opens" $?
        # Nothing vanishes, nothing piles up (DESIGN-DIRECTION §5 C6).
        said "$log" "-ready.png"   | grep -qx '   OK';                        ok "$at: the card stays until she presses OK" $?
        said "$log" "-messages.png" | grep -qx '   3 messages';               ok "$at: three become one card that says 3 messages" $?
        [ "$(said1 "$log" "-messages.png" | grep -o 'is ready\|taken out\|getting low' | wc -l | tr -d ' ')" = 1 ]
                                                                               ok "$at: ...showing only the newest" $?
        said "$log" "-list.png"    | grep -qx '   Messages';                  ok "$at: the clock opens Messages" $?
        # (Newest first, as many as fit: on a short screen the oldest is one press
# of Older away, and the button for it is on the page.)
        said1 "$log" "-list.png"   | grep -q 'VLC media player is ready\|Older';    ok "$at: ...with what was said before in it" $?
        said "$log" "-sound.png"   | grep -qx '   Sound 45%';                 ok "$at: the sound line is on screen" $?
        ! grep -q 'PRESSABLE under\|OVER THE CLOCK\|OVER THE BAND' "$log"
                                                                               ok "$at: the card and the line cover no button, the clock or the band" $?
        grep -q '\[the card at' "$log" && grep -q '\[the line at' "$log"
                                                                               ok "$at: (both were measured)" $?
        case $look in
            taskbar) ! said "$log" "-opens.png" | grep -qx '   Everything on this computer'
                     ok "$at: the program list gets out of its way" $?
                     said "$log" "-desktop.png" | grep -qx '   All my programs'
                     ok "$at: the button is called All my programs" $? ;;
            tiles)   ! said "$log" "-opens.png" | grep -qx '   Find and add programs'
                     ok "$at: it fills the screen, not hidden behind the buttons" $? ;;
        esac
    done
done
[ $fail = 0 ] && echo "journeytest: $n checks, all good" || echo "journeytest: FAILED"
exit $fail
