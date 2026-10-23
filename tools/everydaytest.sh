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
         fonts-crosextra-carlito fonts-crosextra-caladea heif-gdk-pixbuf libheif-plugin-libde265 libglib2.0-bin; do
    if has "$p"; then ok "$p is in the image"; else bad "$p is NOT in the profile"; fi
done
for p in libreoffice-impress libreoffice-draw libreoffice-base default-jre openjdk-17-jre libreoffice-java-common; do
    if has "$p"; then bad "$p is in the profile (decision 14: Writer and Calc only, no Java)"
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
LIBS="$RFS/usr/lib/x86_64-linux-gnu:$RFS/lib/x86_64-linux-gnu"
inrfs() { "$LDSO" --library-path "$LIBS" "$@"; }

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
ls "$RFS"/usr/share/fonts/truetype/crosextra/Carlito* >/dev/null 2>&1 && ls "$RFS"/usr/share/fonts/truetype/crosextra/Caladea* >/dev/null 2>&1 \
    && ok "the fonts a Word file asks for are there (Carlito, Caladea)" || bad "Carlito / Caladea are not in the image"
if grep -q 'gio open' "$RFS/usr/bin/xdg-open" 2>/dev/null && [ -x "$RFS/usr/bin/xdg-open.distrib" ]; then
    ok "xdg-open is ours, and the original is kept beside it"
else
    bad "xdg-open is not ours (or the original is not kept)"
fi

# Her files, as the image's own GLib sees them. Extension first, as a
# double-click does; then who is given that kind of file.
mkdir -p "$T/home/cfg" "$T/home/data" "$T/files"
GENV="HOME=$T/home XDG_CONFIG_HOME=$T/home/cfg XDG_DATA_HOME=$T/home/data XDG_DATA_DIRS=$RFS/usr/local/share:$RFS/usr/share XDG_CONFIG_DIRS=$RFS/etc/xdg GIO_USE_VFS=local GIO_USE_VOLUME_MONITOR=unix"
gio_do() { env $GENV "$LDSO" --library-path "$LIBS" "$RFS/usr/bin/gio" "$@"; }
if ! gio_do version >/dev/null 2>&1; then
    bad "the image's gio does not run here" "$(gio_do version 2>&1 | head -2)"
else
    expect() { # file  desktop-file   label
        f="$T/files/$1"; printf 'x' > "$f"
        ct=$(gio_do info -a standard::content-type "$f" 2>/dev/null | sed -n 's/^ *standard::content-type: //p')
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

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: she can print to printer.local, open a Word or Excel file, and every program's \"open\" goes where a double-click would."
exit 0
