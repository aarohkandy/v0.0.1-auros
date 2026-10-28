#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  everydaytest — the three everyday things PARITY found missing
#
#  1. PRINTING. A network printer is found by a name such as printer.local;
#     nothing on the computer could turn that into an address without
#     libnss-mdns and its word in /etc/nsswitch.conf.
#  2. OFFICE. No Word or Excel program, and a .docx opened the archive
#     manager (it is a zip file underneath).
#  3. OPENING FILES. xdg-open cannot tell what a local file is unless the
#     `file` program is installed, so a program that asked to open a PDF,
#     a photo or a letter got the browser.
#
#  What is checked, and where:
#    always     the profile asks for the packages (and not for the ones the
#               size decision keeps out), build/forge's mDNS step really
#               edits a hosts line, and rootfs/usr/bin/xdg-open does what it
#               says (against a stand-in `gio`)
#    with a built rootfs (RFS=<rootfs>, default work/forge/desktop/rootfs)
#               the printer name resolves through nsswitch, the programs are
#               installed, xdg-open is ours with the original beside it, and
#               a .pdf .jpg .heic .docx .odt .xlsx .ods .csv .mp3 .mp4 file
#               is given to the right program by the image's own GLib,
#               database and mimeapps.list -- the chain xdg-open now uses
#               (asked from inside the image: needs root or passwordless sudo)
#
#  4. WHAT A NO-RECOMMENDS INSTALL QUIETLY DROPPED (docs/reviews/RECOMMENDS-
#     2026-10-24.md): thumbnails, Bluetooth sound, USB printer set-up, zip
#     files, the eject arrow, encrypted sticks, .webp pictures, real-time
#     sound, formulas in Word files, spell-check, phones. Each is checked by
#     doing the thing in the built image where that is cheap (the thumbnail
#     service answers on D-Bus; LibreOffice keeps a formula), and by the file
#     the package exists to provide where it is not.
#
#    sh tools/everydaytest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

RFS="${RFS:-work/forge/desktop/rootfs}"
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '  %-74s ok\n' "$1"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '  %-74s FAIL\n' "$1"; shift
        for m in "$@"; do printf '      %s\n' "$m"; done; }
T=$(mktemp -d "${TMPDIR:-/tmp}/everyday.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM

# ── the profile ─────────────────────────────────────────────────────
echo "the profile"
PROFILE=profiles/desktop.profile
# Package names are words; a comment mentioning one must not count.
PKGS=$(grep -v '^[[:space:]]*#' "$PROFILE" | tr -s ' \n\t"=' '\n' | sort -u)
has() { printf '%s\n' "$PKGS" | grep -qx "$1"; }
for p in libnss-mdns libreoffice-writer libreoffice-calc libreoffice-gtk3 \
         fonts-crosextra-carlito fonts-crosextra-caladea heif-gdk-pixbuf libheif-plugin-libde265 libglib2.0-bin \
         tumbler libspa-0.2-bluetooth system-config-printer-udev unzip zip eject dmsetup webp-pixbuf-loader rtkit \
         libreoffice-math hunspell-en-us hyphen-en-us mythes-en-us libmtp-runtime; do
    if has "$p"; then ok "$p is in the image"; else bad "$p is NOT in the profile"; fi
done
for p in libreoffice-impress libreoffice-draw libreoffice-base default-jre openjdk-17-jre libreoffice-java-common tumbler-plugins-extra; do
    if has "$p"; then bad "$p is in the profile (decision 14: Writer and Calc only, no Java; tumbler-plugins-extra pulls in WebKit, ~140 MB)"
    else ok "$p is kept out"; fi
done

# ── build/forge: the hosts line ─────────────────────────────────────
echo
echo "the hosts line (build/forge stage_mdns)"
sed -n '/^stage_mdns() {/,/^}/p' build/forge > "$T/stage_mdns.sh"
if [ ! -s "$T/stage_mdns.sh" ]; then
    bad "build/forge has no stage_mdns"
else
    try() { # label, original hosts line, expect (yes|no)
        R="$T/r.$checked"; mkdir -p "$R/etc" "$R/usr/lib/x86_64-linux-gnu"
        : > "$R/usr/lib/x86_64-linux-gnu/libnss_mdns4_minimal.so.2"
        printf 'passwd: files\n%s\ngroup: files\n' "$2" > "$R/etc/nsswitch.conf"
        ( RFS="$R"; note() { :; }; warn() { :; }; die() { exit 9; }; . "$T/stage_mdns.sh"; stage_mdns ) >/dev/null 2>&1
        line=$(sed -n 's/^hosts:[[:space:]]*//p' "$R/etc/nsswitch.conf")
        if [ "$3" = yes ]; then
            case "$line" in
              *"mdns4_minimal [NOTFOUND=return] dns"*) ok "$1: \"$line\"" ;;
              *) bad "$1" "got: hosts: $line" ;;
            esac
        else
            [ "$line" = "$2" ] || [ "hosts: $line" = "$2" ] && ok "$1: left alone" || bad "$1" "got: hosts: $line"
        fi
    }
    try "Ubuntu's plain line"             "hosts: files dns" yes
    try "a line with more modules"        "hosts: files myhostname dns" yes
    try "already there (left alone)"      "hosts: files mdns4_minimal [NOTFOUND=return] dns" no
    # With the package missing the step must not touch the file.
    R="$T/r.none"; mkdir -p "$R/etc"; printf 'hosts: files dns\n' > "$R/etc/nsswitch.conf"
    ( RFS="$R"; note() { :; }; warn() { :; }; die() { exit 9; }; . "$T/stage_mdns.sh"; stage_mdns ) >/dev/null 2>&1
    grep -qx 'hosts: files dns' "$R/etc/nsswitch.conf" \
        && ok "without libnss-mdns the file is not touched" || bad "without libnss-mdns the file was edited"
fi

# ── xdg-open, against a stand-in gio ────────────────────────────────
echo
echo "xdg-open (rootfs/usr/bin/xdg-open)"
XO=rootfs/usr/bin/xdg-open
mkdir -p "$T/bin"
cat > "$T/bin/gio" <<'EOF'
#!/bin/sh
echo "gio $*" >> "$GIOLOG"
[ -z "${GIOFAIL:-}" ]
EOF
cat > "$T/real" <<'EOF'
#!/bin/sh
echo "real $*" >> "$GIOLOG"
EOF
chmod 755 "$T/bin/gio" "$T/real"
GIOLOG="$T/log"; export GIOLOG
: > "$GIOLOG"; PATH="$T/bin:$PATH" XDG_OPEN_REAL="$T/real" sh "$XO" /home/her/Documents/letter.docx; rc=$?
[ "$(cat "$GIOLOG")" = "gio open /home/her/Documents/letter.docx" ] && [ $rc = 0 ] \
    && ok "a file goes to gio open, which reads the desktop's own defaults" \
    || bad "a file goes to gio open" "log: $(cat "$GIOLOG")  rc=$rc"
: > "$GIOLOG"; PATH="$T/bin:$PATH" XDG_OPEN_REAL="$T/real" sh "$XO" "https://example.org/"; rc=$?
[ "$(cat "$GIOLOG")" = "gio open https://example.org/" ] && [ $rc = 0 ] \
    && ok "a web address goes the same way" || bad "a web address goes to gio open" "log: $(cat "$GIOLOG")"
: > "$GIOLOG"; GIOFAIL=1 PATH="$T/bin:$PATH" XDG_OPEN_REAL="$T/real" sh "$XO" /x/y.pdf; rc=$?
[ $rc = 4 ] && ok "when it cannot be opened the answer is 4, as xdg-open says" || bad "exit code when gio fails" "rc=$rc"
: > "$GIOLOG"; PATH="$T/bin:$PATH" XDG_OPEN_REAL="$T/real" sh "$XO" --version
[ "$(cat "$GIOLOG")" = "real --version" ] && ok "--version and --help are the original's" || bad "--version goes to the original" "log: $(cat "$GIOLOG")"
mkdir -p "$T/nogio"; : > "$GIOLOG"
PATH="$T/nogio:/usr/bin:/bin" XDG_OPEN_REAL="$T/real" sh "$XO" /x/y.pdf
if command -v gio >/dev/null 2>&1 && [ "$(PATH="$T/nogio:/usr/bin:/bin" command -v gio)" ]; then
    ok "(this machine has its own gio: the no-gio case is not run here)"
else
    [ "$(cat "$GIOLOG")" = "real /x/y.pdf" ] && ok "with no gio it falls back to the original" || bad "no gio falls back to the original" "log: $(cat "$GIOLOG")"
fi

# ── Flathub programs draw on this desktop ───────────────────────────
echo
echo "Flathub programs use this desktop's Wayland (build/forge stage_flatpak_overrides)"
sed -n "/^stage_flatpak_overrides() {/,/^}/p" build/forge \
  | awk '/<<.EOL.$/{f=1; next} /^EOL$/{f=0} f' > "$T/global"
flat_ok() { # file label
    g=$1
    grep -qx '\[Context\]' "$g" && grep -qx 'sockets=wayland;' "$g" \
      && grep -qx '\[Environment\]' "$g" && grep -qx 'ELECTRON_OZONE_PLATFORM_HINT=auto' "$g" \
      && grep -qx 'XDG_SESSION_TYPE=wayland' "$g"
}
if flat_ok "$T/global"; then ok "the override gives every program the Wayland socket and tells Electron to use it"
else bad "the override gives every program the Wayland socket and tells Electron to use it" "$(cat "$T/global" 2>/dev/null | head -8)"; fi
# keyfile sanity: every non-comment line is a [group] or key=value
if awk 'NF && !/^#/ && !/^\[[A-Za-z]+\]$/ && !/^[A-Za-z_]+=.*$/ { bad=1 } END { exit bad }' "$T/global"; then
    ok "...and it is a keyfile flatpak can read"
else bad "the override is not a keyfile"; fi

# ── forge installs it, the profile's table maps it ──────────────────
grep -q 'dpkg-divert --local --rename --divert /usr/bin/xdg-open.distrib' build/forge \
    && ok "build/forge diverts the package's xdg-open (an update does not take ours away)" \
    || bad "build/forge does not divert xdg-open"

# ── the built image ─────────────────────────────────────────────────
echo
LDSO="$RFS/lib64/ld-linux-x86-64.so.2"
if [ ! -d "$RFS/etc" ] || [ ! -x "$LDSO" ]; then
    echo "no built rootfs at $RFS -- the image half is not run (set RFS=<rootfs>)."
    echo
    if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
    echo "$checked checks (the built-image half was not run)"
    exit 0
fi
echo "the built image ($RFS)"

ns=$(sed -n 's/^hosts:[[:space:]]*//p' "$RFS/etc/nsswitch.conf")
case "$ns" in
  *mdns4_minimal\ \[NOTFOUND=return\]*dns*) ok "printer.local can be looked up: hosts: $ns" ;;
  *) bad "hosts line has no mdns4_minimal before dns" "hosts: $ns" ;;
esac
[ -e "$RFS/usr/lib/x86_64-linux-gnu/libnss_mdns4_minimal.so.2" ] \
    && ok "the mDNS module is installed" || bad "libnss_mdns4_minimal.so.2 is not in the image"
for d in libreoffice-writer libreoffice-calc; do
    [ -f "$RFS/usr/share/applications/$d.desktop" ] && ok "$d is installed" || bad "$d.desktop is not in the image"
done
for d in libreoffice-impress libreoffice-draw libreoffice-base; do
    [ -f "$RFS/usr/share/applications/$d.desktop" ] && bad "$d is in the image (decision 14)" || ok "$d is not in the image"
done
if [ -f "$RFS/var/lib/flatpak/overrides/global" ]; then
    if flat_ok "$RFS/var/lib/flatpak/overrides/global"; then ok "the image carries the Flathub Wayland override"
    else bad "the image's /var/lib/flatpak/overrides/global is not what forge writes"; fi
elif [ -x "$RFS/usr/bin/flatpak" ]; then bad "flatpak is installed but there is no global override (Electron programs would use X11)"
fi
[ -x "$RFS/usr/bin/Xwayland" ] && ok "Xwayland is in the image (for programs that have only X11)" || bad "Xwayland is not in the image"
ls "$RFS"/usr/share/fonts/truetype/crosextra/Carlito* >/dev/null 2>&1 && ls "$RFS"/usr/share/fonts/truetype/crosextra/Caladea* >/dev/null 2>&1 \
    && ok "the fonts a Word file asks for are there (Carlito, Caladea)" || bad "Carlito / Caladea are not in the image"
if grep -q 'gio open' "$RFS/usr/bin/xdg-open" 2>/dev/null && [ -x "$RFS/usr/bin/xdg-open.distrib" ]; then
    ok "xdg-open is ours, and the original is kept beside it"
else
    bad "xdg-open is not ours (or the original is not kept)"
fi

# Her files, as the image's own GLib sees them. Extension first, as a
# double-click does; then who is given that kind of file. GLib hides a program
# whose Exec is not on THIS machine's disk, so the question is asked from
# inside the image (chroot: as root, or through sudo), not through its loader.
CH=""
if [ "$(id -u)" = 0 ]; then CH="chroot"
elif sudo -n true 2>/dev/null; then CH="sudo -n chroot"; fi
if [ -z "$CH" ]; then
    echo "  (the file-type checks need root to chroot into the image; not run here)"
elif [ ! -w "$RFS/tmp" ]; then
    echo "  (the file-type checks write a few files into the image's /tmp; it is not writable here)"
else
    FD="/tmp/everyday.$$"
    mkdir -p "$RFS$FD" "$RFS$FD/home"
    trap 'rm -rf "$T" "$RFS$FD"' EXIT INT TERM
    gio_do() { $CH "$RFS" /usr/bin/env HOME="$FD/home" XDG_CONFIG_HOME="$FD/home/cfg" XDG_DATA_HOME="$FD/home/data" \
                   GIO_USE_VFS=local GIO_USE_VOLUME_MONITOR=unix /usr/bin/gio "$@"; }
    if ! gio_do version >/dev/null 2>&1; then
        bad "the image's gio does not run" "$(gio_do version 2>&1 | head -2)"
    else
        expect() { # file  desktop-file   label
            printf 'x' > "$RFS$FD/$1"
            ct=$(gio_do info -a standard::content-type "$FD/$1" 2>/dev/null | sed -n 's/^ *standard::content-type: //p')
            who=$(gio_do mime "$ct" 2>/dev/null | sed -n 's/^Default application for .*: //p')
            if [ "$who" = "$2" ]; then ok "$3 ($1: $ct) opens $who"
            else bad "$3 ($1: $ct)" "wanted $2, the image gives: ${who:-nobody}"; fi
        }
        expect a.pdf   org.gnome.Evince.desktop            "a PDF"
        expect a.jpg   org.xfce.ristretto.desktop          "a photo"
        expect a.heic  org.xfce.ristretto.desktop          "a photo from an iPhone"
        expect a.docx  libreoffice-writer.desktop          "a Word letter"
        expect a.doc   libreoffice-writer.desktop          "an older Word letter"
        expect a.odt   libreoffice-writer.desktop          "a LibreOffice letter"
        expect a.xlsx  libreoffice-calc.desktop            "an Excel sheet"
        expect a.ods   libreoffice-calc.desktop            "a LibreOffice sheet"
        expect a.csv   libreoffice-calc.desktop            "a CSV from the bank"
        expect a.mp3   mpv.desktop                         "a song"
        expect a.mp4   mpv.desktop                         "a film"
        expect a.txt   org.xfce.mousepad.desktop           "a text file"
        expect a.zip   xarchiver.desktop                   "a zip file"
        # A presentation has no program here (Impress is out, decision 14): it
        # says so and opens the store; it never shows the inside of the file.
        expect a.pptx  auros-program-file.desktop          "a presentation"
        expect a.odp   auros-program-file.desktop          "a LibreOffice presentation"
    fi
fi

# ── what the no-Recommends install dropped ──────────────────────────
echo
echo "what a no-Recommends install quietly dropped (docs/reviews/RECOMMENDS-2026-10-24.md)"
want() { # label  path-glob-under-the-image  what she would notice without it
    if ls $RFS/$2 >/dev/null 2>&1; then ok "$1"; else bad "$1" "no $2 in the image: $3"; fi
}
want "Bluetooth headphones make sound: PipeWire's BlueZ plug-in is where PipeWire loads it" \
     "usr/lib/*/spa-0.2/bluez5/libspa-bluez5.so" "they pair and connect and play nothing"
want "an older USB printer sets itself up: the udev rule is there" \
     "usr/lib/udev/rules.d/70-printers.rules" "plugging it in does nothing"
want "the eject arrow works: eject is on the path" \
     "usr/bin/eject" "the disk vanishes, then 'Failed to eject'"
want "an encrypted stick unlocks: the device-mapper udev rules are there" \
     "usr/lib/udev/rules.d/95-dm-notify.rules" "the unlock waits for a notice that never comes"
want "...both of them" "usr/lib/udev/rules.d/55-dm.rules" "no device-mapper rules"
want "sound can ask for real-time priority: rtkit's D-Bus service is there" \
     "usr/share/dbus-1/system-services/org.freedesktop.RealtimeKit1.service" "music crackles when the machine is busy"
want "...and its daemon" "usr/libexec/rtkit-daemon" "the service points at nothing"
want "spell-check works in Letters: the English (US) dictionary is there" \
     "usr/share/hunspell/en_US.dic" "no red underlines"
want "...and its rules" "usr/share/hunspell/en_US.aff" "no red underlines"
want "hyphenation works in Letters" "usr/share/hyphen/hyph_en_US.dic" "no hyphenation"
want "the thesaurus works in Letters" "usr/share/mythes/th_en_US_v2.dat" "no synonyms"
want "an unlisted Android phone is recognised: mtp-probe is there" \
     "usr/lib/udev/mtp-probe" "the phone is plugged in and never appears"
want "formulas in a Word file can be drawn: LibreOffice Math is there" \
     "usr/lib/libreoffice/program/libsmlo.so" "the formula silently disappears"
# .webp is something Photos must be able to decode (build/forge maps it to Photos).
lc=$(ls $RFS/usr/lib/*/gdk-pixbuf-2.0/*/loaders.cache 2>/dev/null | head -1)
if [ -n "$lc" ] && grep -q 'pixbufloader[-_]webp' "$lc"; then ok "a .webp picture opens in Photos: gdk-pixbuf lists the WebP loader"
else bad "a .webp picture opens in Photos: gdk-pixbuf lists the WebP loader" "loaders.cache (${lc:-not found}) has no webp: a picture saved from a web page shows an error"; fi
# LibreOffice Math is installed to draw formulas, not to be a program in her list.
ml="$RFS/usr/local/share/applications/libreoffice-math.desktop"
if [ -f "$RFS/usr/share/applications/libreoffice-math.desktop" ]; then
    grep -qx 'NoDisplay=true' "$ml" 2>/dev/null && ok "...and 'LibreOffice Math' is not among her programs" \
        || bad "...and 'LibreOffice Math' is not among her programs" "$ml does not hide it (build/forge)"
fi
if [ -z "$CH" ] || [ ! -w "$RFS/tmp" ]; then
    echo "  (the checks that run programs in the image need root and a writable $RFS/tmp; not run here)"
else
    # An unpacked rootfs has no /proc and may have no /dev; LibreOffice and the bus want both.
    SUDO=${CH%chroot}; MNT=""
    [ -c "$RFS/dev/null" ] || { ${SUDO}mount --bind /dev "$RFS/dev" 2>/dev/null && MNT="$MNT dev"; }
    [ -e "$RFS/proc/self" ] || { ${SUDO}mount -t proc proc "$RFS/proc" 2>/dev/null && MNT="$MNT proc"; }
    FD2="/tmp/everyday2.$$"
    mkdir -p "$RFS$FD2/home" "$RFS$FD2/run" "$RFS$FD2/out"; chmod 700 "$RFS$FD2/run"
    trap 'cd /; for m in $MNT; do ${SUDO}umount "$RFS/$m" 2>/dev/null; done; rm -rf "$T" "${RFS:?}${FD:-/x}" "${RFS:?}$FD2"' EXIT INT TERM
    inimg() { $CH "$RFS" /usr/bin/env HOME="$FD2/home" XDG_RUNTIME_DIR="$FD2/run" XDG_CONFIG_HOME="$FD2/home/cfg" \
                  XDG_CACHE_HOME="$FD2/home/cache" XDG_DATA_HOME="$FD2/home/data" "$@"; }

    # The thumbnail service Thunar and Photos ask for answers on the session
    # bus, starts by itself, and says it can do photos and PDFs.
    cat > "$RFS$FD2/thumb.sh" <<'THUMB'
out=$(dbus-daemon --session --fork --print-address=1 --print-pid=1) || exit 3
export DBUS_SESSION_BUS_ADDRESS=$(echo "$out" | sed -n 1p)
gdbus call --session --dest org.freedesktop.thumbnails.Thumbnailer1 \
     --object-path /org/freedesktop/thumbnails/Thumbnailer1 \
     --method org.freedesktop.thumbnails.Thumbnailer1.GetSupported 2>&1
kill $(echo "$out" | sed -n 2p)
THUMB
    inimg /bin/sh "$FD2/thumb.sh" > "$T/thumb.out" 2>&1
    if grep -q "'image/jpeg'" "$T/thumb.out" && grep -q "'application/pdf'" "$T/thumb.out"; then
        ok "her photos and PDFs get little pictures: the thumbnail service starts, lists image/jpeg and application/pdf"
    else bad "her photos and PDFs get little pictures: the thumbnail service starts, lists image/jpeg and application/pdf" \
             "$(head -c 300 "$T/thumb.out")"; fi

    # The Bluetooth plug-in loads: nothing it needs is missing.
    sp=$(cd "$RFS" && ls usr/lib/*/spa-0.2/bluez5/libspa-bluez5.so 2>/dev/null | head -1)
    if [ -z "$sp" ]; then bad "the Bluetooth plug-in loads (every library it needs is there)" "no libspa-bluez5.so"
    elif inimg /usr/bin/ldd "/$sp" 2>&1 | grep -q 'not found'; then
        bad "the Bluetooth plug-in loads (every library it needs is there)" "$(inimg /usr/bin/ldd "/$sp" 2>&1 | grep 'not found')"
    else ok "the Bluetooth plug-in loads (every library it needs is there)"; fi

    # The programs Zip files runs: make a zip, list it.
    printf 'x' > "$RFS$FD2/a.txt"
    inimg /bin/sh -c "cd $FD2 && zip -q t.zip a.txt && unzip -l t.zip" > "$T/zip.out" 2>&1
    grep -q 'a\.txt' "$T/zip.out" && ok "a zip file can be made and listed (what Zip files runs)" \
        || bad "a zip file can be made and listed (what Zip files runs)" "$(head -c 200 "$T/zip.out")"

    # A formula in a document survives Writer: an .odt with a formula object,
    # saved as .docx, must come out as Word's own equation (OMML), which only
    # happens with LibreOffice Math installed. (--convert-to pdf "succeeds"
    # without it too, with a gap where the formula was.)
    python3 -I - "$RFS$FD2/f.odt" <<'PY'
import sys, zipfile
ns = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
      'xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" '
      'xmlns:xlink="http://www.w3.org/1999/xlink"')
body = ('<?xml version="1.0" encoding="UTF-8"?><office:document-content %s office:version="1.2"><office:body><office:text><text:p>'
        'Before <draw:frame draw:name="F1" text:anchor-type="as-char" svg:width="3cm" svg:height="1cm"><draw:object xlink:href="./Object 1" '
        'xlink:type="simple" xlink:show="embed" xlink:actuate="onLoad"/></draw:frame> After</text:p></office:text></office:body></office:document-content>') % ns
man = ('<?xml version="1.0" encoding="UTF-8"?><manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">'
       '<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>'
       '<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>'
       '<manifest:file-entry manifest:full-path="Object 1/" manifest:media-type="application/vnd.oasis.opendocument.formula"/>'
       '<manifest:file-entry manifest:full-path="Object 1/content.xml" manifest:media-type="text/xml"/></manifest:manifest>')
mml = ('<?xml version="1.0" encoding="UTF-8"?><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>E</mi><mo>=</mo><mi>m</mi>'
       '<msup><mi>c</mi><mn>2</mn></msup></mrow><annotation encoding="StarMath 5.0">E = m c^2</annotation></semantics></math>')
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.writestr(zipfile.ZipInfo('mimetype'), 'application/vnd.oasis.opendocument.text')
    z.writestr('content.xml', body, zipfile.ZIP_DEFLATED)
    z.writestr('META-INF/manifest.xml', man, zipfile.ZIP_DEFLATED)
    z.writestr('Object 1/content.xml', mml, zipfile.ZIP_DEFLATED)
PY
    inimg timeout 180 /usr/bin/soffice --headless "-env:UserInstallation=file://$FD2/ui" \
          --convert-to docx --outdir "$FD2/out" "$FD2/f.odt" > "$T/soffice.out" 2>&1
    if python3 -I -c 'import sys,zipfile; sys.exit(0 if b"<m:oMath" in zipfile.ZipFile(sys.argv[1]).read("word/document.xml") else 1)' "$RFS$FD2/out/f.docx" 2>/dev/null; then
        ok "a formula in a document survives Letters (saved as Word, it is still an equation)"
    else bad "a formula in a document survives Letters (saved as Word, it is still an equation)" \
             "no equation in the .docx: it disappeared. $(tail -c 200 "$T/soffice.out")"; fi
fi

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: she can print to printer.local, open a Word or Excel file, and every program's \"open\" goes where a double-click would."
exit 0
