# (g) A double-click, and a link in a page, open something she has a word for. No picture, no
# window: what the desktop's own file database says opens each kind of file she will meet.
# gio answers it the way Your files does, and the way xdg-open (ours) hands every program's request on.
. /usr/lib/auros-parity/lib.sh

# the name she sees for a launcher: the same search order as the desktop's own (/usr/local first)
nameof() { for d in /usr/local/share/applications /usr/share/applications; do
               [ -f "$d/$1" ] && { sed -n 's/^Name=//p' "$d/$1" | head -1; return; }; done; }

# group | the name her screen shows for what opens it | the types in it
types() {
    g=$1; want=$2; shift 2
    bad=""; n=0
    for t in "$@"; do
        n=$((n+1))
        id=$(gio mime "$t" 2>&1 | sed -n 's/^Default application for.*: //p' | head -1)
        got=$(nameof "$id")
        [ "$got" = "$want" ] || bad="$bad $t=${id:-nothing}(${got:-no name})"
    done
    if [ -z "$bad" ]; then result "types.$g" pass "all $n kinds open \"$want\""
    else result "types.$g" fail "should open \"$want\":$bad"; fi
}

types pictures  "Photos" image/jpeg image/png image/gif image/bmp image/tiff image/webp image/heic
types pdf       "Documents" application/pdf
types letters   "Letters" application/vnd.openxmlformats-officedocument.wordprocessingml.document \
                application/msword application/vnd.oasis.opendocument.text application/rtf
types tables    "Spreadsheets" application/vnd.openxmlformats-officedocument.spreadsheetml.sheet \
                application/vnd.ms-excel application/vnd.oasis.opendocument.spreadsheet text/csv
types notes     "Notes" text/plain
# a Windows Media file is as likely in her folders as an mp3: .wmv, .wma, .3gp (a phone), .flv, .aac
types music     "Music and video" video/mp4 video/x-matroska video/webm video/quicktime video/x-msvideo \
                video/x-ms-wmv video/3gpp video/x-flv audio/mpeg audio/mp4 audio/x-m4a audio/aac audio/x-wav \
                audio/flac audio/ogg audio/x-ms-wma
types zip       "Zip files" application/zip application/gzip application/x-7z-compressed
# a program's installer, and a presentation (no Impress here): the store, never the inside of the file
types installer "Get more programs" application/vnd.debian.binary-package application/x-msi \
                application/x-ms-dos-executable application/x-msdownload \
                application/vnd.openxmlformats-officedocument.presentationml.presentation application/vnd.ms-powerpoint
types folders   "Your files" inode/directory
types web       "Internet" x-scheme-handler/http x-scheme-handler/https text/html
# A link to an email address is REGISTERED to Internet by the same index, but what Firefox does with a mailto: link the
# desktop hands back to it (a chooser, a loop, nothing) has not been tried: so it is a note, not a pass.
note "mailto: is registered to \"$(nameof "$(gio mime x-scheme-handler/mailto 2>&1 | sed -n 's/^Default application for.*: //p' | head -1)")\"; what Firefox does with it next is untested"

# Solitaire is on her list of programs, under that name, and it opens. This runs in the desktop's own session, so
# `command -v` asks what the shell asks: it leaves a launcher off her list when it cannot find the program (apps.c).
name=$(nameof sol.desktop)
if [ "$name" != Solitaire ]; then
    result types.solitaire fail "no Solitaire among her programs (sol.desktop says \"$name\")"
elif ! command -v sol >/dev/null 2>&1; then
    result types.solitaire fail "sol.desktop is there, but sol is not on the desktop's PATH, so it is left off her list ($PATH)"
else
    log=/var/lib/auros-parity/sol.log
    : > "$log"
    snap types-solitaire-before
    WAYLAND_DEBUG=1 setsid sol > "$log" 2>&1 &
    if wait_for 60 grep -q 'xdg_surface[@#][0-9]*.get_toplevel' "$log"; then
        sleep 8
        snap types-solitaire
        act changed types-solitaire-before types-solitaire types.solitaire-visible 260 130 1020 690 1.5
        result types.solitaire pass "Solitaire is on her list and opens a window"
    else
        result types.solitaire fail "sol started but no window in 60 s: $(grep -ai 'error\|critical\|cannot\|failed' "$log" | head -2 | tr '\n' ' ' | cut -c1-200)"
    fi
    pkill -x sol 2>/dev/null
fi
