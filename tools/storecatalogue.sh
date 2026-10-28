#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  storecatalogue -- does the image carry Flathub's catalogue?
#
#  "Get more programs" (GNOME Software) draws what is in
#  /var/lib/flatpak/appstream/flathub. With nothing there its first page
#  is a header bar over a blank sheet: RC1 and RC6 on real Windows, five
#  minutes of it, "failed to get featured apps: no apps to show". The
#  build now puts the catalogue in the image (build/forge
#  stage_flathub_catalogue). This reads the built rootfs and asks:
#
#    - the listing is there and has programs in it (>= 1000, VLC among them);
#    - the icons the store draws are there (64x64 and 128x128, thousands),
#      and VLC's own icon is one of them;
#    - the image says where it came from (/etc/auros/software-sources:
#      flathub_catalogue=build) and its build-warnings do not say MISSING.
#
#  It does not need the network. It does not run the store: that is
#  tools/storeoffline.sh (the real store, on the real shell, with no
#  network at all, finds VLC).
#
#    sh tools/storecatalogue.sh [ROOTFS]     (default work/forge/desktop/rootfs)
#    sh tools/storecatalogue.sh selftest     (five stand-in trees: one good, four broken on purpose)
# ═══════════════════════════════════════════════════════════════════
set -u
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-72s ok\n' "$1"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-72s FAIL\n' "$1"; shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

check_tree() { # ROOTFS
    R=$1
    d=$R/var/lib/flatpak/appstream/flathub/x86_64/active
    if [ ! -x "$R/usr/bin/flatpak" ] && [ ! -e "$R/.standin" ]; then
        echo "    no flatpak in this image (allow_user_install=no): no Flathub, nothing to carry"; return
    fi
    x=$d/appstream.xml.gz
    if [ -s "$x" ]; then ok "the listing is in the image"; else
        bad "the listing is in the image" "missing or empty: $x" "the store's first page would be blank"; return; fi
    n=$(gzip -dc "$x" 2>/dev/null | grep -c '<component' || true)
    if [ "$n" -ge 1000 ]; then ok "the listing has programs in it ($n)"; else bad "the listing has programs in it" "$n components (need 1000)"; fi
    v=$(gzip -dc "$x" 2>/dev/null | grep -c 'org.videolan.VLC' || true)
    if [ "$v" -ge 1 ]; then ok "VLC is in it"; else bad "VLC is in it" "no org.videolan.VLC in the listing"; fi
    for s in 64x64 128x128; do
        c=$(find "$d/icons/$s" -type f 2>/dev/null | wc -l | tr -d ' ')
        if [ "$c" -ge 1000 ]; then ok "the icons the store draws are there ($s: $c)"; else bad "the icons the store draws are there ($s)" "$c files in $d/icons/$s (need 1000)"; fi
    done
    if [ -f "$d/icons/64x64/org.videolan.VLC.png" ]; then ok "VLC's own icon is one of them"; else bad "VLC's own icon is one of them" "no $d/icons/64x64/org.videolan.VLC.png"; fi
    if grep -qx 'flathub_catalogue=build' "$R/etc/auros/software-sources" 2>/dev/null; then ok "the image says it carries the catalogue (software-sources)"; else
        bad "the image says it carries the catalogue" "no flathub_catalogue=build in /etc/auros/software-sources"; fi
    if grep -q 'flathub_catalogue=MISSING' "$R/etc/auros/build-warnings" 2>/dev/null; then bad "the build did not warn that it is missing" "build-warnings says flathub_catalogue=MISSING"; else ok "the build did not warn that it is missing"; fi
}

if [ "${1:-}" = selftest ]; then
    T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
    mk() { # NAME: a stand-in tree with a good catalogue
        t=$T/$1; d=$t/var/lib/flatpak/appstream/flathub/x86_64/active
        mkdir -p "$d/icons/64x64" "$d/icons/128x128" "$t/etc/auros"; : > "$t/.standin"
        { i=0; while [ $i -lt 1100 ]; do echo "<component><id>org.example.A$i</id></component>"; i=$((i+1)); done
          echo "<component><id>org.videolan.VLC</id></component>"; } | gzip > "$d/appstream.xml.gz"
        for s in 64x64 128x128; do i=0; while [ $i -lt 1100 ]; do : > "$d/icons/$s/a$i.png"; i=$((i+1)); done; : > "$d/icons/$s/org.videolan.VLC.png"; done
        echo flathub_catalogue=build > "$t/etc/auros/software-sources"; : > "$t/etc/auros/build-warnings"
    }
    mk good; mk nolist; rm -rf "$T/nolist/var/lib/flatpak/appstream"
    mk novlc; { i=0; while [ $i -lt 1100 ]; do echo "<component><id>org.example.A$i</id></component>"; i=$((i+1)); done; } | gzip > "$T/novlc/var/lib/flatpak/appstream/flathub/x86_64/active/appstream.xml.gz"
    mk noicons; rm -rf "$T/noicons/var/lib/flatpak/appstream/flathub/x86_64/active/icons/128x128"
    mk unsaid; echo flathub=first-online > "$T/unsaid/etc/auros/software-sources"
    res=""
    for t in good nolist novlc noicons unsaid; do
        f0=$fail; echo "  stand-in tree: $t"; check_tree "$T/$t" > "$T/$t.out"; [ "$t" = good ] && cat "$T/$t.out"
        if [ "$t" = good ]; then [ "$fail" = "$f0" ] && res="$res good:pass" || res="$res good:FAIL"
        else if [ "$fail" -gt "$f0" ]; then res="$res $t:red"; else res="$res $t:NOT-RED"; fi
             fail=$f0; fi          # a broken tree failing is the point, not a failure of the self-test
    done
    echo "  $res"
    case "$res" in *FAIL*|*NOT-RED*) echo "storecatalogue selftest: the check does not see the difference"; exit 1 ;; esac
    echo "storecatalogue selftest: good passes, each broken tree fails"; exit 0
fi

R=${1:-work/forge/desktop/rootfs}
[ -d "$R" ] || { echo "storecatalogue: no rootfs at $R" >&2; exit 2; }
echo "  the catalogue in $R"
check_tree "$R"
echo
if [ "$fail" -gt 0 ]; then echo "storecatalogue: $fail of $checked FAILED"; exit 1; fi
echo "storecatalogue: $checked checks passed"
