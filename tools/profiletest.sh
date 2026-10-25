#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  profiletest — does every version of AurOS build, and start up to
#  its desktop?
#
#  "Every version of our OS a user could want" is five profiles, and
#  until this test only one of them (desktop) was ever built or booted
#  by anything that runs on its own. The other four were last built by
#  hand a month ago, with the browser fallback, before the snap pin --
#  and two of them no longer built at all, in ways nothing reported:
#
#    office   asked for `thunderbird`, which in Ubuntu 24.04 is a
#             120 KB package that installs a snap. forge now pins
#             those to -1, so the install line fails forty minutes in
#             with "no installation candidate".
#    revive   excluded `tracker3`, `tracker3-miners` and
#             `gnome-software-plugin-deb`, none of which exist in
#             noble. apt refuses the whole purge for one unknown name,
#             the build says `|| true`, and NOTHING on the list was
#             removed -- including the things that were there.
#
#  Three modes, cheapest first:
#
#    sh tools/profiletest.sh lint [profile...]
#        Seconds, no root. Every package a profile names exists in its
#        suite, none of them is a snap stub, every name it excludes is
#        a real package (so the purge is not a silent no-op), its theme
#        and its desktop exist, a kiosk names the programs it allows.
#        Reads Ubuntu's own package indexes (downloaded once, cached).
#
#    sudo sh tools/profiletest.sh boot <profile> [evidence-dir]
#        The built image, out/auros-<profile>.img: first what is IN it
#        (read off the disk: the packages, the excludes, the policy,
#        the browser), then started under OVMF in QEMU (KVM when there
#        is one) on a 1280x800 screen, until its desktop is up. A probe
#        unit that is NOT part of AurOS -- it is put into a scratch
#        copy of the image and nothing else -- reports from inside over
#        the serial port: which desktop is running, as whom, in which
#        language, which units failed, how long the start took, how
#        much memory is in use. Then the screen is photographed
#        (PNG, in the evidence directory) and asked whether anything
#        was drawn on it.
#
#    sh tools/profiletest.sh selftest
#        Every check above, shown to FAIL on input made to be wrong: a
#        profile with a snap stub, an unknown package, an unknown
#        exclude, a missing theme; a black screen; a report from a
#        machine whose desktop is not the one the profile asks for.
#
#  Environment:
#    PROFILETEST_PKGS     where the package indexes are cached
#    PROFILETEST_TIMEOUT  seconds to wait for the desktop (default 900)
#    PROFILETEST_MEM      MiB for the VM (default: 2048 for revive --
#                         the machine it is for -- else 4096)
#    PROFILETEST_IBUS_FUNCTIONAL=1
#                         for multilingual only: type real Pinyin into
#                         Mousepad and Firefox, reboot, and repeat. This is
#                         deliberately opt-in because it boots twice.
#    PROFILETEST_IBUS_X11_PROBE=1
#                         with the functional multilingual run only: collect
#                         diagnostic XRes/process/screenshot evidence for an
#                         explicitly X11 ibus-ui-gtk3 panel. It does not make
#                         candidate visibility a product success claim.
#    PROFILETEST_FCITX_CLIENT_PROBE=1
#                         for a retained multilingual diagnostic only: install
#                         Noble's Fcitx GTK module into the scratch copy and
#                         record composition and parented-popup protocol syntax.
#    PROFILETEST_FCITX_PRODUCTION=1
#                         for a fresh multilingual image only: exercise the
#                         shipped Fcitx service in Mousepad and Firefox without
#                         installing packages or replacing the service.
#    PROFILETEST_FCITX_SECURITY_PROBE=1
#                         with a retained Fcitx client or production run only:
#                         add two real boots plus focus-transfer and synthetic
#                         password-field suppression evidence.
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-62s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-62s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

MODE="${1:-}"; [ $# -gt 0 ] && shift

# ── a profile, read the way forge reads it ──────────────────────────
#
# forge sources the inherit chain parent-first; so does this, in a
# subshell, and prints one key. Not a parser of its own: a second
# reading of the same file is how the two come to disagree.
pval() { # profile key
    (
        set +u
        chain=""; cur="$1"; depth=0
        while [ -n "$cur" ]; do
            f="$ROOT/profiles/$cur.profile"
            [ -f "$f" ] || f="$cur"
            [ -f "$f" ] || exit 3
            chain="$f $chain"
            cur=$(sed -n 's/^[[:space:]]*inherit="\{0,1\}\([A-Za-z0-9_-]*\)"\{0,1\}.*/\1/p' "$f" | head -1)
            depth=$((depth+1)); [ $depth -gt 8 ] && exit 3
        done
        for f in $chain; do . "$f"; done
        eval "printf '%s' \"\${$2:-}\"" | tr -s ' \n\t' ' ' | sed 's/^ //; s/ $//'
    )
}

# ── Ubuntu's own package indexes ────────────────────────────────────
PK="${PROFILETEST_PKGS:-${TMPDIR:-/tmp}/profiletest-pkgs}"
indexes() { # suite mirror components -> $PK/<suite>.idx  ("name version" lines)
    s=$1; m=$2; comps=$3
    out="$PK/$s.idx"
    [ -s "$out" ] && return 0
    mkdir -p "$PK"
    : > "$out.new"
    for pocket in "$s" "$s-updates" "$s-security"; do
        for c in $comps; do
            u="$m/dists/$pocket/$c/binary-amd64/Packages.xz"
            curl -fsSL --max-time 300 "$u" -o "$PK/p.xz" \
                || { echo "  could not fetch $u"; rm -f "$out.new" "$PK/p.xz"; return 1; }
            xz -dc "$PK/p.xz" | awk '/^Package: /{p=$2} /^Version: /{print p, $2}' >> "$out.new"
        done
    done
    rm -f "$PK/p.xz"
    sort -u "$out.new" > "$out"; rm -f "$out.new"
}

lint_one() { # profile-name-or-path [index-file]
    p=$1
    case "$p" in
      */*) name=$(basename "$p" .profile)
           [ -f "$p" ] || { bad "$p exists"; return; } ;;
      *)   name=$p
           [ -f "profiles/$p.profile" ] || { bad "profiles/$p.profile exists"; return; } ;;
    esac
    echo
    echo "  $name"
    id=$(pval "$p" profile_id)
    [ "$id" = "$name" ] && ok "profile_id is the file's name" \
                        || bad "profile_id is the file's name" "profile_id='$id'"
    suite=$(pval "$p" base_suite); mirror=$(pval "$p" base_mirror)
    comps=$(pval "$p" base_components)
    idx="${2:-}"
    if [ -z "$idx" ]; then
        indexes "$suite" "$mirror" "$comps" || { bad "the $suite package indexes can be read"; return; }
        idx="$PK/$suite.idx"
    fi
    th=$(pval "$p" theme); sh_=$(pval "$p" shell_archetype)
    [ -f "themes/$th.theme" ] && ok "theme '$th' exists" \
                              || bad "theme '$th' exists" "no themes/$th.theme"
    [ -f "shells/$sh_.shell" ] && ok "desktop '$sh_' exists" \
        || bad "desktop '$sh_' exists" "no shells/$sh_.shell -- forge would quietly use rail"

    # Every list stage 2 hands to one `apt-get install` -- the same
    # lists, in forge's order (build/forge, stage 2's `local pkgs=`).
    # packages_security was missing here, so a profile that inherits
    # desktop's zram-tools looked as if it asked for zram with nothing
    # to provide it.
    names="locales"
    for k in packages_hardware packages_boot packages_base packages_security \
             packages_desktop packages_files packages_apps packages_extra; do
        names="$names $(pval "$p" $k)"
    done
    missing=""; stubs=""
    for n in $names; do
        v=$(awk -v n="$n" '$1==n {print $2; exit}' "$idx")
        if [ -z "$v" ]; then missing="$missing $n"
        else case "$v" in *snap*) stubs="$stubs $n" ;; esac
        fi
    done
    [ -z "$missing" ] && ok "every package it installs exists in $suite" \
        || bad "every package it installs exists in $suite" "not in $suite:$missing"
    [ -z "$stubs" ] && ok "none of them is a snap stub (pinned out by forge)" \
        || bad "none of them is a snap stub (pinned out by forge)" \
               "these only install a snap, and forge pins them to -1:$stubs"
    unk=""
    for n in $(pval "$p" packages_exclude); do
        awk -v n="$n" '$1==n {f=1} END {exit !f}' "$idx" || unk="$unk $n"
    done
    [ -z "$unk" ] && ok "every name it excludes is a real package" \
        || bad "every name it excludes is a real package" \
               "unknown to apt:$unk -- one unknown name and apt purges NOTHING"
    case "$(pval "$p" kiosk_mode)" in
      yes)
        [ -n "$(pval "$p" allowed_apps)" ] && ok "a kiosk names the programs it allows" \
            || bad "a kiosk names the programs it allows" "allowed_apps is empty"
        ;;
    esac
    u=$(pval "$p" default_user)
    case "$u" in ''|*[!a-z0-9_-]*) bad "default_user is a plain account name" "'$u'" ;;
                 *) ok "default_user is a plain account name ($u)" ;; esac
    if [ -n "$(pval "$p" zram_percent)" ]; then
        case " $names " in *" zram-tools "*) ok "zram asked for, and zram-tools installed" ;;
            *) bad "zram asked for, and zram-tools installed" ;; esac
    fi
    for l in $(pval "$p" locale) $(pval "$p" extra_locales); do
        case "$l" in
          *_*.UTF-8) : ;;
          *) bad "locale '$l' is a UTF-8 locale name" ;;
        esac
    done
}

# ── a picture of the screen, and whether anything is on it ──────────
#
# QEMU writes PPM; this turns it into a PNG with nothing but zlib, and
# says how much of it is not black and how many colours it has. A
# desktop that never drew is a black (or one-colour) frame, and that is
# the failure the picture is there to catch.
shot_py() { # in.ppm out.png  -> "WxH drawn=<permille> colours=<n>"
    python3 - "$1" "$2" <<'EOPY'
import sys, zlib, struct
src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
# P6 header: magic, width, height, maxval, each separated by whitespace
parts = []; i = 0
while len(parts) < 4:
    while d[i:i+1].isspace(): i += 1
    if d[i:i+1] == b'#':
        while d[i:i+1] not in (b'\n', b''): i += 1
        continue
    j = i
    while not d[j:j+1].isspace(): j += 1
    parts.append(d[i:j]); i = j
i += 1
assert parts[0] == b'P6', 'not a P6 PPM'
w, h = int(parts[1]), int(parts[2])
px = d[i:i + w*h*3]
raw = b''.join(b'\0' + px[y*w*3:(y+1)*w*3] for y in range(h))
def chunk(t, b):
    c = struct.pack('>I', len(b)) + t + b
    return c + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) \
      + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
open(dst, 'wb').write(png)
# Sample every 4th pixel of every 4th row: plenty to tell a drawn
# desktop from a black one, and quick on a 1280x800 frame.
lit = 0; n = 0; cols = set()
for y in range(0, h, 4):
    row = px[y*w*3:(y+1)*w*3]
    for x in range(0, w, 4):
        r, g, b = row[x*3], row[x*3+1], row[x*3+2]
        n += 1
        if r + g + b > 48: lit += 1
        if len(cols) < 4096: cols.add((r >> 3, g >> 3, b >> 3))
print('%dx%d drawn=%d colours=%d' % (w, h, lit * 1000 // max(n, 1), len(cols)))
EOPY
}
# A frame counts as a desktop when at least 5% of it is lit and it has
# more than 8 colours (text, icons, a wallpaper). A firmware's black
# screen, a blank framebuffer or one flat colour do not.
drawn_enough() { # "WxH drawn=N colours=M"
    dn=$(printf '%s' "$1" | sed -n 's/.*drawn=\([0-9]*\).*/\1/p')
    cl=$(printf '%s' "$1" | sed -n 's/.*colours=\([0-9]*\).*/\1/p')
    [ "${dn:-0}" -ge 50 ] && [ "${cl:-0}" -gt 8 ]
}

qmp() { # socket command-json...
    python3 - "$@" <<'EOPY'
import sys, socket, json, time
s = socket.socket(socket.AF_UNIX); s.settimeout(30); s.connect(sys.argv[1])
f = s.makefile('rw')
f.readline()
def cmd(c):
    f.write(json.dumps(c) + '\n'); f.flush()
    while True:
        r = json.loads(f.readline())
        if 'return' in r or 'error' in r: return r
cmd({'execute': 'qmp_capabilities'})
for a in sys.argv[2:]:
    r = cmd(json.loads(a))
    if 'error' in r: print(r['error'].get('desc', r)); sys.exit(1)
EOPY
}

# ── what the probe reported, against what the profile asks for ──────
#
# One function for the real boot and for the selftest, so the test of
# the checks is a test of THESE checks.
judge_report() { # profile report-file
    p=$1; R=$2
    rv() { sed -n "s/^PROFILETEST $1=//p" "$R" | tr -d '\r' | tail -1; }
    grep -q '^PROFILETEST up' "$R" && ok "the desktop came up" \
        || bad "the desktop came up" "the probe never saw /run/auros"
    [ "$(rv aurshell)" = active ] && ok "aurshell is running" \
        || bad "aurshell is running" "aurshell.service: $(rv aurshell)"
    want=$(pval "$p" shell_archetype)
    [ "$(rv shell)" = "/usr/share/auros/shells/$want.shell" ] \
        && ok "it is the $want desktop" \
        || bad "it is the $want desktop" "active.shell -> $(rv shell)"
    want=$(pval "$p" theme)
    [ "$(rv theme)" = "$want" ] && ok "it looks like $want" \
        || bad "it looks like $want" "theme: $(rv theme)"
    want=$(pval "$p" default_user)
    [ "$(rv user)" = "$want" ] && ok "it runs as $want, not root" \
        || bad "it runs as $want, not root" "desktop user: $(rv user)"
    want=$(pval "$p" locale)
    [ "$(rv lang)" = "LANG=$want" ] && ok "its language is $want" \
        || bad "its language is $want" "$(rv lang)"
    have=" $(rv locales) "
    miss=""
    for l in $(pval "$p" locale) $(pval "$p" extra_locales); do
        g=$(printf '%s' "$l" | sed 's/\.UTF-8$/.utf8/')
        case "$have" in *" $g "*) : ;; *) miss="$miss $l" ;; esac
    done
    [ -z "$miss" ] && ok "every language it ships was generated" \
        || bad "every language it ships was generated" "not generated:$miss" "have:$have"
    [ -n "$(rv browser)" ] && ok "a browser is installed ($(rv browser))" \
        || bad "a browser is installed" "no firefox on PATH"
    [ -z "$(rv failed)" ] && ok "no unit failed on the way up" \
        || bad "no unit failed on the way up" "failed: $(rv failed)"
    if [ -n "$(pval "$p" zram_percent)" ]; then
        case "$(rv swap)" in *zram*) ok "compressed swap in memory is on" ;;
            *) bad "compressed swap in memory is on" "swap: $(rv swap)" ;; esac
    fi
    if [ -n "$(pval "$p" earlyoom_min_free_percent)" ]; then
        [ "$(rv earlyoom)" = active ] && ok "earlyoom is watching memory" \
            || bad "earlyoom is watching memory" "earlyoom: $(rv earlyoom)"
    fi
    if [ "$(pval "$p" allow_tty)" = no ]; then
        [ "$(rv tty2)" = masked ] && ok "there is no console on Ctrl-Alt-F2" \
            || bad "there is no console on Ctrl-Alt-F2" "getty@tty2: $(rv tty2)"
    fi
    if [ "$p" = multilingual ]; then
        legacy_ibus=0
        [ "${PROFILETEST_IBUS_FUNCTIONAL:-0}" = 1 ] && legacy_ibus=1
        [ "${PROFILETEST_FCITX_CLIENT_PROBE:-0}" = 1 ] && legacy_ibus=1
        if [ "$legacy_ibus" = 1 ]; then
            [ "$(rv ibus_unit)" = active ] && ok "the retained image's IBus service is running" \
                || bad "the retained image's IBus service is running" "auros-ibus.service: $(rv ibus_unit)"
            [ "$(rv ibus_gtk)" = ibus ] && ok "the retained user manager gives GTK programs IBus" \
                || bad "the retained user manager gives GTK programs IBus" "GTK_IM_MODULE=$(rv ibus_gtk)"
            [ "$(rv ibus_qt)" = ibus ] && ok "the retained user manager gives Qt programs IBus" \
                || bad "the retained user manager gives Qt programs IBus" "QT_IM_MODULE=$(rv ibus_qt)"
            [ "$(rv ibus_xmodifiers)" = '@im=ibus' ] && ok "the retained user manager gives X programs IBus" \
                || bad "the retained user manager gives X programs IBus" "XMODIFIERS=$(rv ibus_xmodifiers)"
            [ -n "$(rv ibus_wayland)" ] && [ -n "$(rv ibus_display)" ] \
                && ok "the retained IBus session has both displays" \
                || bad "the retained IBus session has both displays" \
                       "WAYLAND_DISPLAY=$(rv ibus_wayland) DISPLAY=$(rv ibus_display)"
            [ "$(rv ibus_pinyin)" = yes ] && ok "Pinyin is available through retained-image IBus" \
                || bad "Pinyin is available through retained-image IBus" "pinyin=$(rv ibus_pinyin)"
        else
            [ "$(rv fcitx_unit)" = active ] && ok "the user's Fcitx service is running" \
                || bad "the user's Fcitx service is running" "auros-fcitx.service: $(rv fcitx_unit)"
            [ "$(rv ibus_unit)" = inactive ] && ok "the conflicting IBus service is inactive" \
                || bad "the conflicting IBus service is inactive" "auros-ibus.service: $(rv ibus_unit)"
            [ "$(rv fcitx_gtk)" = fcitx ] && [ -z "$(rv fcitx_qt)" ] \
                && [ -z "$(rv fcitx_xmodifiers)" ] \
                && ok "only the tested GTK input module is selected" \
                || bad "only the tested GTK input module is selected" \
                       "GTK=$(rv fcitx_gtk) QT=$(rv fcitx_qt) XMODIFIERS=$(rv fcitx_xmodifiers)"
            [ "$(rv fcitx_identity)" = exact ] && ok "the Fcitx service has the exact executable and arguments" \
                || bad "the Fcitx service has the exact executable and arguments" "identity=$(rv fcitx_identity)"
            [ "$(rv fcitx_owner)" = exact ] && ok "the exact Fcitx process owns its D-Bus service" \
                || bad "the exact Fcitx process owns its D-Bus service" "owner=$(rv fcitx_owner)"
            [ "$(rv fcitx_display)" = absent ] && [ "$(rv fcitx_wayland)" = absent ] \
                && ok "the Fcitx daemon has no display fallback" \
                || bad "the Fcitx daemon has no display fallback" \
                       "DISPLAY=$(rv fcitx_display) WAYLAND_DISPLAY=$(rv fcitx_wayland)"
            [ "$(rv fcitx_pinyin)" = yes ] && ok "Pinyin is available through the live Fcitx service" \
                || bad "Pinyin is available through the live Fcitx service" "pinyin=$(rv fcitx_pinyin)"
        fi
        if [ "${PROFILETEST_IBUS_FUNCTIONAL:-0}" = 1 ] || \
           [ "${PROFILETEST_FCITX_CLIENT_PROBE:-0}" = 1 ] || \
           [ "${PROFILETEST_FCITX_PRODUCTION:-0}" = 1 ]; then
            printf '    %-62s %s\n' "typing Pinyin in Mousepad and Firefox" \
                "checked below from exact application evidence"
        else
            printf '    %-62s %s\n' "typing Pinyin in Mousepad and Firefox" \
                "requires the fresh-image functional certification"
        fi
    fi
}

# Exact application evidence from the optional functional run. Screenshots
# show the real candidate UI, but pixels alone never pass: Mousepad must save
# the exact UTF-8 bytes and Firefox must submit the exact encoded form value.
judge_ibus_functional() { # evidence-root screenshot-root
    D=$1; SHOTS=$2
    for round in 1 2; do
        R="$D/round$round"
        [ -d "$R" ] || { bad "Pinyin round $round produced evidence" "missing $R"; continue; }
        hex=$(od -An -tx1 "$R/mousepad.txt" 2>/dev/null | tr -d ' \n')
        [ "$hex" = e4bda0e5a5bd ] \
            && ok "round $round: Mousepad saved the exact Pinyin composition" \
            || bad "round $round: Mousepad saved the exact Pinyin composition" "UTF-8 bytes: ${hex:-missing}"
        grep -Fq '"GET /?text=%E4%BD%A0%E5%A5%BD HTTP/' "$R/firefox-http.log" 2>/dev/null \
            && ok "round $round: Firefox submitted the exact Pinyin composition" \
            || bad "round $round: Firefox submitted the exact Pinyin composition" "exact local request missing"
        grep -qx 'GTK_IM_MODULE=ibus' "$R/firefox.env" 2>/dev/null \
            && grep -q '^WAYLAND_DISPLAY=.' "$R/firefox.env" 2>/dev/null \
            && grep -q '^DISPLAY=.' "$R/firefox.env" 2>/dev/null \
            && ok "round $round: the tested Firefox inherited the live IBus session" \
            || bad "round $round: the tested Firefox inherited the live IBus session" \
                   "$(tr '\n' ' ' < "$R/firefox.env" 2>/dev/null || echo missing)"
        grep -qx 'mousepad=pass' "$R/result" 2>/dev/null \
            && grep -qx 'firefox=pass' "$R/result" 2>/dev/null \
            && grep -qx 'status=pass' "$R/result" 2>/dev/null \
            && ok "round $round: the guest completed both real application checks" \
            || bad "round $round: the guest completed both real application checks" \
                   "$(tr '\n' ' ' < "$R/result" 2>/dev/null || echo missing)"
        for app in mousepad firefox; do
            shot="$SHOTS/ibus-round$round-$app-candidate.png"
            [ -s "$shot" ] \
                && ok "round $round: $app candidate-state screenshot was captured" \
                || bad "round $round: $app candidate-state screenshot was captured" "missing $shot"
        done
    done
}

judge_fcitx_client_probe() { # evidence-root screenshot-root [source]
    D=$1; SHOTS=$2; EXPECTED_SOURCE=${3:-retained-ibus-conversion}
    status=$(sed -n 's/^status=//p' "$D/result" 2>/dev/null | tail -1)
    case "$status" in
      pass) ok "the opt-in Fcitx first-stage diagnostic completed" ;;
      unavailable) bad "the opt-in Fcitx packages were available" \
          "UNAVAILABLE: $(sed -n 's/^reason=//p' "$D/result" 2>/dev/null | tail -1)" ;;
      *) bad "the opt-in Fcitx first-stage diagnostic completed" \
          "$(tr '\n' ' ' < "$D/result" 2>/dev/null || echo missing)" ;;
    esac
    grep -qx "source=$EXPECTED_SOURCE" "$D/restore.txt" 2>/dev/null \
        && ok "the Fcitx evidence came from the requested source" \
        || bad "the Fcitx evidence came from the requested source" \
               "expected $EXPECTED_SOURCE"
    printf '    %-62s %s\n' "candidate pixels and Wayland connection identity" \
        "UNPROVEN -- inspect screenshots; connection-tagged evidence is still required"
    for app in mousepad firefox; do
        grep -qx 'parented_mapped_popup_count=1' "$D/$app-popup.txt" 2>/dev/null \
            && grep -qx 'parented_popup_syntax_observed=yes' "$D/$app-popup.txt" 2>/dev/null \
            && grep -qx 'connection_identity=unproven' "$D/$app-popup.txt" 2>/dev/null \
            && grep -qx 'layer_shell_objects=0' "$D/$app-popup.txt" 2>/dev/null \
            && grep -qx 'popup_input_grab=no' "$D/$app-popup.txt" 2>/dev/null \
            && grep -Eq '^popup_width=[1-9][0-9]*$' "$D/$app-popup.txt" 2>/dev/null \
            && grep -Eq '^popup_height=[1-9][0-9]*$' "$D/$app-popup.txt" 2>/dev/null \
            && ok "$app emitted mapped parented-popup protocol syntax" \
            || bad "$app emitted mapped parented-popup protocol syntax" \
                   "parented-popup syntax evidence missing"
        hex=$(od -An -tx1 "$D/$app.txt" 2>/dev/null | tr -d ' \n')
        [ "$hex" = e4bda0e5a5bd ] \
            && ok "$app produced the exact Pinyin composition" \
            || bad "$app produced the exact Pinyin composition" "UTF-8 bytes: ${hex:-missing}"
        for phase in candidate committed; do
            [ "$app" = firefox ] && [ "$phase" = committed ] && phase=submitted
            [ -s "$SHOTS/fcitx-$app-$phase.png" ] \
                && ok "$app $phase screenshot was retained" \
                || bad "$app $phase screenshot was retained" "missing screenshot"
        done
        grep -Eq '^changed_samples=[0-9]+ total_samples=[0-9]+$' \
            "$D/fcitx-$app-candidate-change.txt" 2>/dev/null \
            && ok "$app candidate phase changed screen pixels" \
            || bad "$app candidate phase changed screen pixels" "pixel-change evidence missing"
    done
    grep -qx 'display_unset=yes' "$D/fcitx.process" 2>/dev/null \
        && grep -qx 'wayland_display_unset=yes' "$D/fcitx.process" 2>/dev/null \
        && ok "the Fcitx daemon had no fallback display" \
        || bad "the Fcitx daemon had no fallback display" "fallback isolation missing"
    grep -qx 'service_owner_stable=yes' "$D/fcitx.process" 2>/dev/null \
        && ok "the owned Fcitx process retained its D-Bus service" \
        || bad "the owned Fcitx process retained its D-Bus service" "service replacement or restart"
    grep -qx 'mousepad=pinyin' "$D/selected-engines.txt" 2>/dev/null \
        && grep -qx 'firefox=pinyin' "$D/selected-engines.txt" 2>/dev/null \
        && ok "both focused fields selected the exact Pinyin engine" \
        || bad "both focused fields selected the exact Pinyin engine" "focused engine evidence missing"
    grep -qx 'raw_protocol_logs_retained=no' "$D/protocol-summary.txt" 2>/dev/null \
        && ok "raw protocol logs were removed after reduction" \
        || bad "raw protocol logs were removed after reduction" "redaction proof missing"
    grep -qx 'ordinary_service_ready=1' "$D/restore.txt" 2>/dev/null \
        && grep -qx 'user_config_unchanged=yes' "$D/restore.txt" 2>/dev/null \
        && ok "the shipped input service stayed ready and user config was preserved" \
        || bad "the shipped input service stayed ready and user config was preserved" "readiness evidence missing"
}

judge_fcitx_production() { # evidence-root screenshot-root
    D=$1; SHOTS=$2
    judge_fcitx_client_probe "$D" "$SHOTS" shipped-fcitx
    grep -qx 'unit=auros-fcitx.service' "$D/fcitx.process" 2>/dev/null \
        && grep -qx 'ibus_inactive=yes' "$D/fcitx.process" 2>/dev/null \
        && grep -Eq '^start_time=[1-9][0-9]*$' "$D/fcitx.process" 2>/dev/null \
        && ok "the production proof exercised only the shipped Fcitx service" \
        || bad "the production proof exercised only the shipped Fcitx service" \
               "shipped service evidence missing"
    grep -qx 'GTK_IM_MODULE=fcitx' "$D/mousepad.env" 2>/dev/null \
        && grep -qx 'GTK_IM_MODULE=fcitx' "$D/firefox.env" 2>/dev/null \
        && ok "both applications inherited the shipped GTK input module" \
        || bad "both applications inherited the shipped GTK input module" \
               "application environment evidence missing"
}

exact_password_suppression() { # reduced evidence file
    awk -F= '
        BEGIN {
            expected["candidate_popup_creations"]="0"
            expected["candidate_popup_maps"]="0"
            expected["candidate_popup_configures"]="0"
            expected["candidate_popup_grabs"]="0"
            expected["layer_shell_objects"]="0"
            expected["observed_candidate_popup"]="no"
            expected["focus_event_observed"]="yes"
            expected["ascii_value_exact"]="yes"
            expected["composition_start_count"]="0"
            expected["composition_update_count"]="0"
            expected["composition_end_count"]="0"
            expected["composition_input_count"]="0"
            expected["raw_password_focus_log_retained"]="no"
            expected["raw_password_value_log_retained"]="no"
            expected["raw_password_event_log_retained"]="no"
        }
        NF != 2 || !($1 in expected) || seen[$1] || $2 != expected[$1] { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 15 }
    ' "$1" 2>/dev/null
}

exact_password_popup_events() { # bounded reduced protocol evidence file
    python3 - "$1" <<'PY'
import re, sys

path = sys.argv[1]
try:
    lines = open(path, encoding="ascii").read().splitlines()
except (OSError, UnicodeError):
    raise SystemExit(1)
values = {}
objects = {}
for line in lines:
    if line.count("=") != 1:
        raise SystemExit(1)
    key, value = line.split("=", 1)
    if key in values or key in objects:
        raise SystemExit(1)
    match = re.fullmatch(r"candidate_object_([1-9][0-9]*)", key)
    if match:
        objects[int(match.group(1))] = value
    else:
        values[key] = value

zero = {
    "candidate_popup_creations", "candidate_popup_maps",
    "candidate_popup_configures", "candidate_popup_grabs",
    "candidate_popup_configures_matched", "candidate_popup_configures_unmatched",
    "candidate_popup_grabs_matched", "candidate_popup_grabs_unmatched",
    "candidate_objects_at_focus", "candidate_objects_mapped_at_end",
    "reduced_events_recorded", "reduced_events_omitted", "layer_shell_objects",
}
phases = ("focus_to_input", "input_to_screenshot",
          "screenshot_to_submit", "after_submit")
for phase in phases:
    for kind in ("popup", "map", "configure", "grab"):
        zero.add(f"{phase}_{kind}_events")
numeric = {
    "candidate_objects_relevant", "candidate_objects_recorded",
    "candidate_objects_omitted", "candidate_objects_destroyed_after_focus",
    "focus_boundary_byte", "input_boundary_byte",
    "screenshot_boundary_byte", "submit_boundary_byte",
}
allowed = zero | numeric | {
    "observed_candidate_popup", "connection_identity", "popup_attribution",
}
if set(values) != allowed or any(values[key] != "0" for key in zero):
    raise SystemExit(1)
if values["observed_candidate_popup"] != "no":
    raise SystemExit(1)
if values["connection_identity"] != "unproven":
    raise SystemExit(1)
if values["popup_attribution"] != "unproven":
    raise SystemExit(1)
if any(not re.fullmatch(r"0|[1-9][0-9]*", values[key]) for key in numeric):
    raise SystemExit(1)
relevant = int(values["candidate_objects_relevant"])
recorded = int(values["candidate_objects_recorded"])
omitted = int(values["candidate_objects_omitted"])
destroyed = int(values["candidate_objects_destroyed_after_focus"])
if recorded > 16 or relevant != recorded + omitted or destroyed > relevant:
    raise SystemExit(1)
if set(objects) != set(range(1, recorded + 1)):
    raise SystemExit(1)
object_pattern = re.compile(
    r"child:[1-9][0-9]*,popup:[1-9][0-9]*,wl:(?:0|[1-9][0-9]*),"
    r"parent:[1-9][0-9]*,parent_role:(?:toplevel|popup|unknown),"
    r"created_phase:(?:before_focus|focus_to_input|input_to_screenshot|"
    r"screenshot_to_submit|after_submit),mapped_at_focus:no,"
    r"map_count:0,unmap_count:[0-9]+,configure_count:0,grab_count:0,"
    r"destroyed:(?:yes|no)")
if any(not object_pattern.fullmatch(value) for value in objects.values()):
    raise SystemExit(1)
boundaries = [int(values[key]) for key in
              ("focus_boundary_byte", "input_boundary_byte",
               "screenshot_boundary_byte", "submit_boundary_byte")]
if boundaries != sorted(boundaries):
    raise SystemExit(1)
PY
}

exact_sensitive_service_identity() { # reduced Fcitx identity at sensitive boundaries
    awk -F= '
        BEGIN {
            fixed["exe"]="/usr/bin/fcitx5"; fixed["cmdline"]="/usr/bin/fcitx5 -D"
            fixed["identity_before_sensitive"]="yes"; fixed["identity_after_sensitive"]="yes"
            variable["pid"]=1; variable["start_time"]=1; variable["dbus_owner"]=1
            for (key in fixed) expected[key]=1
            for (key in variable) expected[key]=1
        }
        NF != 2 || !($1 in expected) || seen[$1] { bad=1 }
        ($1 in fixed) && $2 != fixed[$1] { bad=1 }
        ($1 == "pid" || $1 == "start_time") && $2 !~ /^[1-9][0-9]*$/ { bad=1 }
        $1 == "dbus_owner" && $2 !~ /^:[0-9]+[.][0-9]+$/ { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 7 }
    ' "$1" 2>/dev/null
}

exact_shipped_restore() { # reduced production restore file
    awk -F= '
        BEGIN {
            expected["ordinary_service_ready"]="1"
            expected["user_config_unchanged"]="yes"
            expected["cleanup_pids_gone"]="1"
            expected["source"]="shipped-fcitx"
            expected["service_start_rc"]="not-needed"
            expected["service_active_seen"]="no"
            expected["service_identity_seen"]="no"
            expected["service_identity_stable_seen"]="no"
            expected["pinyin_engine_seen"]="no"
        }
        NF != 2 || !($1 in expected) || seen[$1] || $2 != expected[$1] { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 9 }
    ' "$1" 2>/dev/null
}

exact_shipped_process() { # reduced production process file
    awk -F= '
        BEGIN {
            fixed["exe"]="/usr/bin/fcitx5"
            fixed["cmdline"]="/usr/bin/fcitx5 -D"
            fixed["unit"]="auros-fcitx.service"
            fixed["ibus_inactive"]="yes"
            fixed["display_unset"]="yes"
            fixed["wayland_display_unset"]="yes"
            fixed["service_owner_stable"]="yes"
            variable["pid"]=1; variable["boot_id"]=1; variable["start_time"]=1
            variable["config_hash"]=1
        }
        NF != 2 || (!( $1 in fixed) && !($1 in variable)) || seen[$1] { bad=1 }
        ($1 in fixed) && $2 != fixed[$1] { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 11 }
    ' "$1" 2>/dev/null \
        && grep -Eq '^pid=[1-9][0-9]*$' "$1" 2>/dev/null \
        && grep -Eq '^start_time=[1-9][0-9]*$' "$1" 2>/dev/null \
        && grep -Eq '^config_hash=[0-9a-f]{64}$' "$1" 2>/dev/null \
        && grep -Eq '^boot_id=[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' "$1" 2>/dev/null
}

exact_production_password_env() { # reduced inherited environment file
    awk -F= '
        BEGIN { expected["GDK_BACKEND"]="wayland"; expected["GTK_IM_MODULE"]="fcitx"; variable["WAYLAND_DISPLAY"]=1 }
        NF != 2 || (!( $1 in expected) && !($1 in variable)) || seen[$1] { bad=1 }
        ($1 in expected) && $2 != expected[$1] { bad=1 }
        $1 == "WAYLAND_DISPLAY" && $2 == "" { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 3 }
    ' "$1" 2>/dev/null
}

exact_production_reboot() { # reduced round-two reboot file
    [ "$(cat "$1" 2>/dev/null)" = shipped_fcitx_autostart_ready=yes ]
}

exact_mousepad_b_process() { # reduced second-editor process file
    awk -F= '
        BEGIN {
            fixed["unit"]="auros-profiletest-fcitx-mousepad-b"
            fixed["exe"]="/usr/bin/mousepad"
            fixed["argv"]="/usr/bin/mousepad --disable-server <marker-file>"
            fixed["gdk_backend"]="wayland"; fixed["gtk_im_module"]="fcitx"
            fixed["wayland_display_set"]="yes"; fixed["display_unset"]="yes"
            variable["pid"]=1; variable["start_time"]=1
            fixed["marker_sha256"]="665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1"
        }
        NF != 2 || (!($1 in fixed) && !($1 in variable)) || seen[$1] { bad=1 }
        ($1 in fixed) && $2 != fixed[$1] { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 10 }
    ' "$1" 2>/dev/null \
        && grep -Eq '^pid=[1-9][0-9]*$' "$1" 2>/dev/null \
        && grep -Eq '^start_time=[1-9][0-9]*$' "$1" 2>/dev/null \
        && grep -qx 'marker_sha256=665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1' "$1" 2>/dev/null
}

exact_mousepad_b_toplevel() { # reduced second-editor Wayland role file
    awk -F= '
        BEGIN { variable["toplevel_xdg_surface"]=1; variable["toplevel_wl_surface"]=1; fixed["mapped_toplevel_count"]="1"; fixed["layer_shell_objects"]="0" }
        NF != 2 || (!($1 in fixed) && !($1 in variable)) || seen[$1] { bad=1 }
        ($1 in fixed) && $2 != fixed[$1] { bad=1 }
        ($1 in variable) && $2 !~ /^[1-9][0-9]*$/ { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 4 }
    ' "$1" 2>/dev/null
}

exact_mousepad_b_visibility() { # reduced host frame-delta file
    awk -F= '
        BEGIN {
            fixed["status"]="pass"; fixed["threshold_percent"]="3"
            fixed["marker_template_sha256"]="665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1"
            variable["changed_samples"]=1; variable["total_samples"]=1
            variable["marker_pattern_steps"]=1
            variable["marker_bbox_left"]=1; variable["marker_bbox_top"]=1
            variable["marker_bbox_right"]=1; variable["marker_bbox_bottom"]=1
            variable["click_x"]=1; variable["click_y"]=1
            variable["click_pixel_delta"]=1
            for (key in fixed) expected[key]=1
            for (key in variable) expected[key]=1
        }
        function number(value) { return value ~ /^-?[0-9]+$/ }
        {
            duplicate=seen[$1]++
            if (NF != 2 || !($1 in expected) || duplicate) bad=1
            if (!duplicate) count++
            if (($1 in fixed) && $2 != fixed[$1]) bad=1
            if (($1 in variable) && !number($2)) bad=1
            value[$1]=$2
        }
        END {
            if (bad || count != 13) exit 1
            if (value["total_samples"] <= 0 || value["changed_samples"] * 100 < value["total_samples"] * 3) exit 1
            if (value["marker_pattern_steps"] < 20) exit 1
            if (value["marker_bbox_left"] < 0 || value["marker_bbox_top"] < 0) exit 1
            if (value["marker_bbox_right"] - value["marker_bbox_left"] < 200) exit 1
            if (value["marker_bbox_bottom"] - value["marker_bbox_top"] < 200) exit 1
            if (value["click_x"] < value["marker_bbox_left"] || value["click_x"] > value["marker_bbox_right"]) exit 1
            if (value["click_y"] < value["marker_bbox_top"] || value["click_y"] > value["marker_bbox_bottom"]) exit 1
            if (value["click_pixel_delta"] <= 24) exit 1
        }
    ' "$1" 2>/dev/null
}

connection_interval_passed() { # exact authenticated connection reducer
    awk -F= '
        BEGIN {
            split("status=pass children_at_boundaries=0 compositor_connections_at_boundaries=1 application_trace_writers_at_boundaries=1 foreign_trace_writers_at_boundaries=0 authenticated_probe_trace_writer_at_end=1 peer_pid=exact_aurshell peer_path=exact_wayland_display listener_inode=exact_wayland_display service_main_pid=exact_dbus_run_session compositor_exe=exact_usr_bin_aurshell service_invocation_id=exact_systemd_property_and_environment service_control_group=exact_systemd_property pam_session_cgroup=exact_expected_user_session_scope compositor_in_pam_session_cgroup=yes compositor_descends_from_service_main_pid=yes", claims, " ")
            for (i in claims) { split(claims[i], pair, "="); fixed[pair[1]]=pair[2] }
            observed="service_main_pid compositor_pid service_control_group pam_session_control_group"
            split(observed, names, " "); for (i in names) identity["observed_" names[i]]=1
            metrics="wayland_path_inode_count global_matching_listener_count after_wayland_path_inode_count after_global_matching_listener_count compositor_socket_fd_count matching_accepting_listener_fd_count matching_accepting_listener_open_description_count matching_accepting_listener_count application_descendant_count trace_writer_count application_trace_writer_count probe_trace_writer_count foreign_trace_writer_count application_socket_fd_count application_socket_inspection_error_count matching_compositor_connection_count trace_sink_identity_exact"
            split(metrics, names, " ")
            for (i in names) { metric["observed_start_" names[i]]=1; metric["observed_end_" names[i]]=1 }
            fixed["observed_service_control_group"]="/system.slice/aurshell.service"
            fixed["observed_start_global_matching_listener_count"]="1"; fixed["observed_end_global_matching_listener_count"]="1"
            fixed["observed_start_after_global_matching_listener_count"]="1"; fixed["observed_end_after_global_matching_listener_count"]="1"
            fixed["observed_start_matching_accepting_listener_open_description_count"]="1"; fixed["observed_end_matching_accepting_listener_open_description_count"]="1"
            fixed["observed_start_matching_accepting_listener_count"]="1"; fixed["observed_end_matching_accepting_listener_count"]="1"
            fixed["observed_start_application_descendant_count"]="0"; fixed["observed_end_application_descendant_count"]="0"
            fixed["observed_start_trace_writer_count"]="1"; fixed["observed_end_trace_writer_count"]="2"
            fixed["observed_start_application_trace_writer_count"]="1"; fixed["observed_end_application_trace_writer_count"]="1"
            fixed["observed_start_probe_trace_writer_count"]="0"; fixed["observed_end_probe_trace_writer_count"]="1"
            fixed["observed_start_foreign_trace_writer_count"]="0"; fixed["observed_end_foreign_trace_writer_count"]="0"
            fixed["observed_start_application_socket_inspection_error_count"]="0"; fixed["observed_end_application_socket_inspection_error_count"]="0"
            fixed["observed_start_matching_compositor_connection_count"]="1"; fixed["observed_end_matching_compositor_connection_count"]="1"
            fixed["observed_start_trace_sink_identity_exact"]="yes"; fixed["observed_end_trace_sink_identity_exact"]="yes"
            for (key in fixed) expected[key]=1
            for (key in identity) expected[key]=1
            for (key in metric) expected[key]=1
        }
        NF != 2 || !($1 in expected) || seen[$1] || $2 == "" { bad=1 }
        ($1 in fixed) && $2 != fixed[$1] { bad=1 }
        ($1 == "observed_service_main_pid" || $1 == "observed_compositor_pid") && $2 !~ /^[1-9][0-9]*$/ { bad=1 }
        $1 == "observed_pam_session_control_group" && $2 !~ /^\/user[.]slice\/user-[0-9]+[.]slice\/session-[A-Za-z0-9_.:-]+[.]scope$/ { bad=1 }
        ($1 in metric) && !($1 in fixed) && $2 !~ /^[1-9][0-9]*$/ { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 54 }
    ' "$1" 2>/dev/null
}

exact_focus_lifecycle() {
    awk -F= '
        BEGIN {
            fixed["initial_unmapped_before_b_input"]="yes"; fixed["mapped_candidate_while_away"]="no"
            fixed["return_map_epoch"]="fresh"; fixed["popup_input_grab"]="no"; fixed["layer_shell_objects"]="0"
            fixed["b_saved_sentinel"]="20"; fixed["a_saved_file_unchanged"]="yes"
            variable["initial_popup_wl_surface"]=1; variable["return_popup_wl_surface"]=1; variable["return_popup_reused"]=1
            for (key in fixed) expected[key]=1
            for (key in variable) expected[key]=1
        }
        NF != 2 || !($1 in expected) || seen[$1] { bad=1 }
        ($1 in fixed) && $2 != fixed[$1] { bad=1 }
        ($1 ~ /_wl_surface$/) && $2 !~ /^[1-9][0-9]*$/ { bad=1 }
        $1 == "return_popup_reused" && $2 != "yes" && $2 != "no" { bad=1 }
        !seen[$1]++ { count++ }
        END { exit bad || count != 10 }
    ' "$1" 2>/dev/null
}

judge_fcitx_security_probe() { # evidence-root screenshot-root [source]
    D=$1; SHOTS=$2; SECURITY_SOURCE=${3:-retained-ibus-conversion}
    case "$SECURITY_SOURCE" in
      retained-ibus-conversion|shipped-fcitx) ;;
      *) bad "Fcitx security evidence source is recognized" "$SECURITY_SOURCE"; return ;;
    esac
    for round in 1 2; do
        R="$D/round$round"
        grep -qx 'status=pass' "$R/result" 2>/dev/null \
            && ok "Fcitx security round $round completed" \
            || bad "Fcitx security round $round completed" "missing pass result"
        round_apps=yes
        for app in mousepad firefox; do
            [ "$(od -An -tx1 "$R/$app.txt" 2>/dev/null | tr -d ' \n')" = e4bda0e5a5bd ] \
                && grep -qx 'parented_mapped_popup_count=1' "$R/$app-popup.txt" 2>/dev/null \
                && grep -qx 'popup_input_grab=no' "$R/$app-popup.txt" 2>/dev/null \
                && grep -qx 'layer_shell_objects=0' "$R/$app-popup.txt" 2>/dev/null \
                || round_apps=no
        done
        [ "$round_apps" = yes ] \
            && grep -qx 'mousepad=pinyin' "$R/selected-engines.txt" 2>/dev/null \
            && grep -qx 'firefox=pinyin' "$R/selected-engines.txt" 2>/dev/null \
            && grep -qx 'raw_protocol_logs_retained=no' "$R/protocol-summary.txt" 2>/dev/null \
            && ok "round $round repeated exact composition and popup predicates" \
            || bad "round $round repeated exact composition and popup predicates" "first-stage round evidence missing"
        connection_interval_passed "$R/mousepad-connection.txt" \
            && ok "Mousepad held one exact AurWL connection in round $round" \
            || bad "Mousepad held one exact AurWL connection in round $round" "pidfd/socket identity evidence missing"
        exact_mousepad_b_process "$R/mousepad-b.process" \
            && exact_mousepad_b_toplevel "$R/mousepad-b-toplevel.txt" \
            && connection_interval_passed "$R/mousepad-b-connection.txt" \
            && exact_mousepad_b_visibility "$SHOTS/fcitx-round$round-mousepad-b-visibility.txt" \
            && [ -s "$SHOTS/fcitx-round$round-mousepad-b-before.png" ] \
            && [ -s "$SHOTS/fcitx-round$round-mousepad-b-after.png" ] \
            && ok "the second Mousepad was a visible authenticated native toplevel in round $round" \
            || bad "the second Mousepad was a visible authenticated native toplevel in round $round" "second-editor identity/map/frame evidence missing"
        exact_focus_lifecycle "$R/focus-lifecycle.txt" \
            && grep -qx '20' "$R/focus-b-sentinel.hex" 2>/dev/null \
            && ok "A to B to A revoked and freshly mapped the candidate in round $round" \
            || bad "A to B to A revoked and freshly mapped the candidate in round $round" "focus lifecycle evidence missing"
        password_env_ok=yes
        if [ "$SECURITY_SOURCE" = shipped-fcitx ]; then
            exact_production_password_env "$R/password.env" || password_env_ok=no
        else
            grep -qx 'GTK_IM_MODULE=fcitx' "$R/password.env" 2>/dev/null \
                && grep -qx 'GDK_BACKEND=wayland' "$R/password.env" 2>/dev/null \
                && ! grep -q '^DISPLAY=' "$R/password.env" 2>/dev/null \
                || password_env_ok=no
        fi
        exact_password_suppression "$R/password-suppression.txt" \
            && exact_password_popup_events "$R/password-popup-events.txt" \
            && exact_sensitive_service_identity "$R/service-owner-sensitive.txt" \
            && [ "$password_env_ok" = yes ] \
            && ok "the synthetic password field showed no observed popup or browser composition event in round $round" \
            || bad "the synthetic password field showed no observed popup or browser composition event in round $round" "bounded suppression evidence is missing or ambiguous"
        [ -s "$SHOTS/fcitx-round$round-focus-away.png" ] \
            && [ -s "$SHOTS/fcitx-round$round-password-focused.png" ] \
            && [ -s "$SHOTS/fcitx-round$round-password-masked.png" ] \
            && ok "round $round password-phase screenshots were retained" \
            || bad "round $round password-phase screenshots were retained" "security screenshot missing"
        service_ok=no
        if [ "$SECURITY_SOURCE" = shipped-fcitx ]; then
            exact_shipped_restore "$R/restore.txt" && service_ok=yes
        elif grep -qx 'ordinary_service_ready=1' "$R/restore.txt" 2>/dev/null \
            && grep -qx 'user_config_unchanged=yes' "$R/restore.txt" 2>/dev/null \
            && grep -qx 'cleanup_pids_gone=1' "$R/restore.txt" 2>/dev/null \
            && grep -qx "source=$SECURITY_SOURCE" "$R/restore.txt" 2>/dev/null; then
            service_ok=yes
        fi
        [ "$service_ok" = yes ] \
            && ok "round $round preserved its shipped input service and removed transient processes" \
            || bad "round $round preserved its shipped input service and removed transient processes" "round cleanup/source evidence missing"
        process_ok=no
        if [ "$SECURITY_SOURCE" = shipped-fcitx ]; then
            exact_shipped_process "$R/fcitx.process" && process_ok=yes
        elif grep -qx 'exe=/usr/bin/fcitx5' "$R/fcitx.process" 2>/dev/null \
            && grep -qx 'cmdline=/usr/bin/fcitx5 -D' "$R/fcitx.process" 2>/dev/null \
            && grep -qx 'display_unset=yes' "$R/fcitx.process" 2>/dev/null \
            && grep -qx 'wayland_display_unset=yes' "$R/fcitx.process" 2>/dev/null \
            && grep -qx 'service_owner_stable=yes' "$R/fcitx.process" 2>/dev/null; then
            process_ok=yes
        fi
        [ "$process_ok" = yes ] \
            && ok "round $round retained the exact Fcitx service identity" \
            || bad "round $round retained the exact Fcitx service identity" "process identity evidence missing"
    done
    if [ "$SECURITY_SOURCE" = shipped-fcitx ]; then
        exact_production_reboot "$D/round2/reboot.txt" \
            && ok "the shipped Fcitx service autostarted after the real reboot" \
            || bad "the shipped Fcitx service autostarted after the real reboot" "round-two autostart evidence missing"
        boot1=$(sed -n 's/^boot_id=//p' "$D/round1/fcitx.process" 2>/dev/null)
        boot2=$(sed -n 's/^boot_id=//p' "$D/round2/fcitx.process" 2>/dev/null)
        [ -n "$boot1" ] && [ -n "$boot2" ] && [ "$boot1" != "$boot2" ] \
            && ok "the two production rounds came from distinct real boots" \
            || bad "the two production rounds came from distinct real boots" "boot identities match or are missing"
    else
        grep -qx 'fcitx_autostart_before_explicit_launch=no' "$D/round2/reboot.txt" 2>/dev/null \
            && ok "Fcitx did not autostart after the scratch reboot" \
            || bad "Fcitx did not autostart after the scratch reboot" "round-two prelaunch evidence missing"
    fi
    h1=$(sed -n 's/^config_hash=//p' "$D/round1/fcitx.process" 2>/dev/null)
    h2=$(sed -n 's/^config_hash=//p' "$D/round2/fcitx.process" 2>/dev/null)
    [ -n "$h1" ] && [ "$h1" = "$h2" ] \
        && ok "both rounds used the exact same isolated Fcitx configuration" \
        || bad "both rounds used the exact same isolated Fcitx configuration" "config hashes differ or are missing"
    printf '    %-62s %s\n' "Firefox Wayland connection identity" \
        "UNPROVEN -- this stage authenticates the Mousepad interval only"
}

# Judge the opt-in diagnostic without turning the absence of a candidate
# window into product evidence.  Server PID identity can be useful when no
# panel window mapped, but only a mapped window carries geometry evidence.
judge_ibus_x11_probe() { # evidence-root screenshot-root
    XEV=$1; SHOTS=$2
    status=$(sed -n 's/^status=//p' "$XEV/probe.log" 2>/dev/null | tail -1)
    case "$status" in
      complete) ok "the opt-in X11 panel diagnostic completed" ;;
      inconclusive)
        printf '    %-62s %s\n' "the opt-in X11 panel diagnostic" \
            "INCONCLUSIVE -- no product eligibility"
        ;;
      *) bad "the opt-in X11 panel diagnostic completed" \
             "$(tail -20 "$XEV/probe.log" 2>/dev/null | tr '\n' ' ' || echo missing)" ;;
    esac
    grep -qx 'default_panel_count=0' "$XEV/probe.log" 2>/dev/null \
        && grep -qx 'default_panel_quiescence_s=5' "$XEV/probe.log" 2>/dev/null \
        && ok "ibus-daemon --panel=disable launched no default panel" \
        || bad "ibus-daemon --panel=disable launched no default panel" \
               "$(grep default_panel_count "$XEV/probe.log" 2>/dev/null || echo missing)"
    panel_start=$(sed -n 's/.*panel_start_pid=\([0-9][0-9]*\).*/\1/p' "$XEV/probe.log" | tail -1)
    panel_retained=$(sed -n 's/.*panel_retained_pid=\([0-9][0-9]*\).*/\1/p' "$XEV/probe.log" | tail -1)
    [ -n "$panel_start" ] && [ "$panel_start" = "$panel_retained" ] \
        && grep -qx 'exe=/usr/libexec/ibus-ui-gtk3' "$XEV/panel-start.process" 2>/dev/null \
        && ok "the exact X11 panel retained its spawned PID" \
        || bad "the exact X11 panel retained its spawned PID" \
               "start=${panel_start:-missing} retained=${panel_retained:-missing}"
    identity=$(sed -n 's/^identity_state=//p' "$XEV/candidate-summary.txt" 2>/dev/null | tail -1)
    case "$identity" in
      mapped) ok "XRes authenticated a mapped panel window and its absolute geometry" ;;
      identity-only) ok "XRes authenticated the retained panel PID (no mapped candidate geometry)" ;;
      inconclusive|'')
        printf '    %-62s %s\n' "XRes panel identity and candidate geometry" \
            "INCONCLUSIVE -- no product eligibility"
        ;;
      *) bad "XRes panel identity classification is valid" "identity_state=$identity" ;;
    esac
    grep -Eq '^preedit_candidate_xid=0x[0-9a-fA-F]+$' "$XEV/candidate-lifecycle.txt" 2>/dev/null \
        && grep -Eq '^return_candidate_xid=0x[0-9a-fA-F]+$' "$XEV/candidate-lifecycle.txt" 2>/dev/null \
        && grep -Eq '^xid_reused=(yes|no)$' "$XEV/candidate-lifecycle.txt" 2>/dev/null \
        && grep -qx 'preedit_map_state=2' "$XEV/candidate-lifecycle.txt" 2>/dev/null \
        && grep -qx 'focus_away_map_state=0' "$XEV/candidate-lifecycle.txt" 2>/dev/null \
        && grep -qx 'focus_return_map_state=2' "$XEV/candidate-lifecycle.txt" 2>/dev/null \
        && ok "the authenticated candidate unmapped and returned with native focus" \
        || bad "the authenticated candidate unmapped and returned with native focus" \
               "SECURITY STOP: exact lifecycle evidence missing"
    grep -qx 'GDK_BACKEND=wayland' "$XEV/focus-away.env" 2>/dev/null \
        && grep -Eq '^focus_away_pid=[0-9]+ gone=yes$' "$XEV/probe.log" 2>/dev/null \
        && [ "$(od -An -tx1 "$XEV/runtime-output/focus-away.txt" 2>/dev/null | tr -d ' \n')" = 20 ] \
        && ok "the focus-away application was native Wayland and exited" \
        || bad "the focus-away application was native Wayland and exited" \
               "native focus-away identity or cleanup missing"
    cleanup_ok=1
    for cleanup_unit in auros-profiletest-ibus-x11-focus-away \
                        auros-profiletest-ibus-x11-mousepad \
                        auros-profiletest-ibus-x11-panel \
                        auros-profiletest-ibus-x11-daemon; do
        grep -Eq "^cleanup_unit=$cleanup_unit active_state=(inactive|failed|unknown|unloaded)$" \
            "$XEV/probe.log" 2>/dev/null || cleanup_ok=0
    done
    [ "$(grep -Ec '^cleanup_pid=[0-9]+ gone=yes$' "$XEV/probe.log" 2>/dev/null || true)" -eq 4 ] \
        || cleanup_ok=0
    [ "$cleanup_ok" = 1 ] \
        && ok "all transient units stopped and their recorded PIDs exited" \
        || bad "all transient units stopped and their recorded PIDs exited" \
               "cleanup evidence missing or incomplete"
    grep -Eq '^ordinary_service_ready=1 main_pid=[0-9]+$' "$XEV/probe.log" 2>/dev/null \
        && grep -qx 'exe=/usr/bin/ibus-daemon' "$XEV/shipped-service.process" 2>/dev/null \
        && grep -qx 'cmdline=/usr/bin/ibus-daemon --replace --xim ' "$XEV/shipped-service.process" 2>/dev/null \
        && grep -Fqx pinyin "$XEV/shipped-engine-ids.txt" 2>/dev/null \
        && ok "the shipped IBus service resumed with exact arguments and Pinyin" \
        || bad "the shipped IBus service resumed with exact arguments and Pinyin" \
               "restart identity or readiness evidence missing"
    screenshots_ok=1
    for lifecycle_shot in ibus-x11-before ibus-x11-preedit \
                          ibus-x11-focus-away ibus-x11-focus-return; do
        [ -s "$SHOTS/$lifecycle_shot.png" ] || screenshots_ok=0
    done
    [ "$screenshots_ok" = 1 ] \
        && ok "the X11 diagnostic retained each focus-lifecycle screenshot" \
        || bad "the X11 diagnostic retained each focus-lifecycle screenshot" \
               "one or more X11 lifecycle screenshots are missing"
    printf '    %-62s %s\n' "candidate visibility and product eligibility" \
        "NOT ESTABLISHED by this diagnostic"
}

# ═══ lint ═══════════════════════════════════════════════════════════
if [ "$MODE" = lint ]; then
    for t in curl xz awk; do command -v $t >/dev/null 2>&1 || { echo "need $t"; exit 2; }; done
    [ $# -gt 0 ] || set -- office revive school-kiosk multilingual desktop
    echo
    echo "Would each profile build? (read against Ubuntu's own package lists)"
    for p in "$@"; do lint_one "$p"; done
    echo
    [ "$fail" -eq 0 ] && { echo "$checked checks: every profile asks only for what exists."; exit 0; }
    echo "$fail of $checked wrong."; exit 1
fi

# ═══ selftest ═══════════════════════════════════════════════════════
if [ "$MODE" = selftest ]; then
    T=$(mktemp -d "${TMPDIR:-/tmp}/profiletest-self.XXXXXX")
    trap 'rm -rf "$T"' EXIT
    echo
    echo "Does each check fail when the thing it checks is wrong?"
    # A fake suite: three real names, one snap stub.
    printf '%s\n' "locales 2.39" "mousepad 0.6" "thunderbird 2:1snap1-0ubuntu3" \
                  "tracker 3.7" "snapd 2.6" > "$T/idx"
    # A profile that inherits nothing, so every list is under control.
    cat > "$T/broken.profile" <<'EOP'
profile_id="not-its-name"
base_suite="noble"
theme="no-such-theme"
shell_archetype="rail"
packages_apps="mousepad thunderbird not-a-package"
packages_exclude="snapd tracker3"
kiosk_mode="yes"
allowed_apps=""
default_user="Bad User"
EOP
    # How many of lint_one's checks refuse this profile.
    refusals() { ( fail=0; checked=0; lint_one "$1" "$2" >/dev/null 2>&1; echo "$fail" ); }
    verdict() { # description  refusals  minimum
        if [ "${2:-0}" -ge "$3" ]; then printf '    %-62s %s\n' "$1" "refused ($2)"
        else printf '    %-62s %s\n' "$1" "ACCEPTED -- the check is dead"; fail=$((fail+1)); fi
        checked=$((checked+1))
    }
    # profile_id, theme, missing package, snap stub, unknown exclude,
    # kiosk without apps, default_user: seven.
    verdict "a profile with seven things wrong" "$(refusals "$T/broken.profile" "$T/idx")" 7

    # And each one alone: the real office profile with ONE thing
    # changed, so it is that check and not a neighbour that fires.
    # The index is the real names the desktop and office profiles use
    # (so this part needs no network), plus Ubuntu's thunderbird stub.
    for p in desktop office; do
        for k in packages_hardware packages_boot packages_base packages_security \
                 packages_desktop packages_files packages_apps packages_extra packages_exclude; do
            for n in $(pval $p $k); do echo "$n 1.0"; done
        done
    done | grep -v '^thunderbird ' | sort -u > "$T/real.idx"
    printf '%s\n' "locales 2.39" "thunderbird 2:1snap1-0ubuntu3" >> "$T/real.idx"
    one() { # description sed-expr
        sed -e "$2" -e 's/^profile_id=.*/profile_id="one"/' profiles/office.profile > "$T/one.profile"
        verdict "$1" "$(refusals "$T/one.profile" "$T/real.idx")" 1
    }
    sed 's/^profile_id=.*/profile_id="one"/' profiles/office.profile > "$T/one.profile"
    base=$(refusals "$T/one.profile" "$T/real.idx")
    if [ "$base" = 0 ]; then
        printf '    %-62s %s\n' "the office profile as it is now" "passes"
        one "...with thunderbird (a snap stub) added back" 's/^\(packages_apps="\)/\1thunderbird /'
        one "...asking for a package that does not exist" 's/^\(packages_apps="\)/\1no-such-package /'
        one "...excluding a name apt does not know" 's/^shell_archetype=.*/&\npackages_exclude="snapd tracker3"/'
        one "...in a theme that does not exist" 's/^theme=.*/theme="no-such-theme"/'
        one "...on a desktop that does not exist" 's/^shell_archetype=.*/shell_archetype="plain"/'
        one "...asking for zram with no zram-tools to provide it" 's/^shell_archetype=.*/&\npackages_security="nftables earlyoom"/'
    else
        printf '    %-62s %s\n' "the office profile as it is now" "FAILS ($base): fix it first"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if PROFILETEST_FCITX_PRODUCTION=1 PROFILETEST_FCITX_CLIENT_PROBE=1 \
       sh tools/profiletest.sh boot multilingual 2>&1 | grep -Fq 'cannot be combined'; then
        printf '    %-62s %s\n' "production and retained Fcitx modes are exclusive" "passes"
    else
        printf '    %-62s %s\n' "production and retained Fcitx modes are exclusive" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if PROFILETEST_FCITX_CLIENT_PROBE=1 PROFILETEST_IBUS_FUNCTIONAL=1 \
       sh tools/profiletest.sh boot multilingual 2>&1 | grep -Fq 'cannot be combined'; then
        printf '    %-62s %s\n' "the Fcitx and IBus functional runs are exclusive" "passes"
    else
        printf '    %-62s %s\n' "the Fcitx and IBus functional runs are exclusive" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))

    # The screen: black, one flat colour, and something drawn.
    python3 - "$T" <<'EOPY'
import sys, random
t = sys.argv[1]; w, h = 320, 200
def ppm(name, px): open(f'{t}/{name}.ppm', 'wb').write(b'P6\n%d %d\n255\n' % (w, h) + bytes(px))
ppm('black', [0] * (w*h*3))
ppm('flat', [40, 60, 90] * (w*h))
random.seed(1)
px = []
for y in range(h):
    for x in range(w):
        px += [(x * 7) % 256, (y * 5) % 256, ((x + y) * 3) % 256] if y > 20 else [10, 10, 10]
ppm('drawn', px)
EOPY
    for s in black flat; do
        r=$(shot_py "$T/$s.ppm" "$T/$s.png")
        if drawn_enough "$r"; then
            printf '    %-62s %s\n' "a $s screen" "ACCEPTED as a desktop -- dead check"; fail=$((fail+1))
        else
            printf '    %-62s %s\n' "a $s screen" "refused ($r)"
        fi
        checked=$((checked+1))
    done
    r=$(shot_py "$T/drawn.ppm" "$T/drawn.png")
    if drawn_enough "$r" && head -c 8 "$T/drawn.png" | od -An -c | grep -q 'P   N   G'; then
        printf '    %-62s %s\n' "a drawn screen, written as a real PNG" "accepted ($r)"
    else
        printf '    %-62s %s\n' "a drawn screen, written as a real PNG" "REFUSED ($r)"; fail=$((fail+1))
    fi
    checked=$((checked+1))

    # The probe's report: a good one for school-kiosk passes; the same
    # report from a machine on the wrong desktop, as root, with a
    # failed unit, or a missing language, does not.
    good() {
        cat <<EOR
PROFILETEST up 41
PROFILETEST aurshell=active
PROFILETEST shell=/usr/share/auros/shells/locked.shell
PROFILETEST theme=sandstone
PROFILETEST user=student
PROFILETEST lang=LANG=en_US.UTF-8
PROFILETEST locales=C.utf8 en_US.utf8 POSIX
PROFILETEST browser=/usr/bin/firefox
PROFILETEST failed=
PROFILETEST swap=/dev/zram0 partition 1.9G;
PROFILETEST earlyoom=active
PROFILETEST tty2=masked
PROFILETEST done
EOR
    }
    good > "$T/r.good"
    g=$( fail=0; checked=0; judge_report school-kiosk "$T/r.good" >/dev/null; echo "$fail" )
    if [ "$g" = 0 ]; then
        printf '    %-62s %s\n' "a kiosk that came up as its profile says" "accepted"
    else
        printf '    %-62s %s\n' "a kiosk that came up as its profile says" "REFUSED ($g)"
        fail=$((fail+1)); judge_report school-kiosk "$T/r.good" | grep FAIL -A2
    fi
    checked=$((checked+1))
    rep() { # description sed-expr
        good | sed "$2" > "$T/r.bad"
        b=$( fail=0; checked=0; judge_report school-kiosk "$T/r.bad" >/dev/null; echo "$fail" )
        if [ "$b" -ge 1 ]; then printf '    %-62s %s\n' "$1" "refused"
        else printf '    %-62s %s\n' "$1" "ACCEPTED -- the check is dead"; fail=$((fail+1)); fi
        checked=$((checked+1))
    }
    rep "...the desktop never came up"        '/^PROFILETEST up/d'
    rep "...on the taskbar desktop instead"   's/locked.shell/taskbar.shell/'
    rep "...running as root"                  's/user=student/user=root/'
    rep "...in another theme"                 's/theme=sandstone/theme=nocturne/'
    rep "...with a unit that failed"          's/failed=$/failed=auros-firstboot.service/'
    rep "...with no browser"                  's/browser=.*/browser=/'
    rep "...with a console on Ctrl-Alt-F2"    's/tty2=masked/tty2=enabled/'
    rep "...with aurshell crashed"            's/aurshell=active/aurshell=failed/'
    rep "...with no swap at all"              's/swap=.*/swap=/'
    rep "...swapping to the disk, not memory" 's|swap=.*|swap=/swapfile file 2G;|'
    rep "...with earlyoom not running"        's/earlyoom=active/earlyoom=inactive/'
    good | sed 's/locales=.*/locales=C.utf8 POSIX/' > "$T/r.bad"
    b=$( fail=0; checked=0; judge_report school-kiosk "$T/r.bad" >/dev/null; echo "$fail" )
    if [ "$b" -ge 1 ]; then printf '    %-62s %s\n' "...with its language never generated" "refused"
    else printf '    %-62s %s\n' "...with its language never generated" "ACCEPTED -- dead"; fail=$((fail+1)); fi
    checked=$((checked+1))

    # Multilingual has runtime promises the other profiles deliberately do
    # not make. Its production evidence comes from the real user's manager,
    # the exact Fcitx process and its D-Bus owner. Every missing part fails.
    multilingual_good() {
        cat <<EOR
PROFILETEST up 41
PROFILETEST aurshell=active
PROFILETEST shell=/usr/share/auros/shells/rail.shell
PROFILETEST theme=sandstone
PROFILETEST user=auros
PROFILETEST lang=LANG=fr_FR.UTF-8
PROFILETEST locales=C.utf8 ar_EG.utf8 en_US.utf8 es_ES.utf8 fr_FR.utf8 zh_CN.utf8 POSIX
PROFILETEST browser=/usr/bin/firefox
PROFILETEST failed=
PROFILETEST swap=/dev/zram0 partition 1.9G;
PROFILETEST earlyoom=active
PROFILETEST tty2=masked
PROFILETEST fcitx_unit=active
PROFILETEST ibus_unit=inactive
PROFILETEST fcitx_gtk=fcitx
PROFILETEST fcitx_qt=
PROFILETEST fcitx_xmodifiers=
PROFILETEST fcitx_identity=exact
PROFILETEST fcitx_owner=exact
PROFILETEST fcitx_display=absent
PROFILETEST fcitx_wayland=absent
PROFILETEST fcitx_pinyin=yes
PROFILETEST done
EOR
    }
    multilingual_good > "$T/m.good"
    m=$( fail=0; checked=0; judge_report multilingual "$T/m.good" >/dev/null; echo "$fail" )
    if [ "$m" = 0 ]; then printf '    %-62s %s\n' "a multilingual session with live Fcitx evidence" "accepted"
    else printf '    %-62s %s\n' "a multilingual session with live Fcitx evidence" "REFUSED ($m)"; fail=$((fail+1)); fi
    checked=$((checked+1))
    mrep() { # description sed-expr
        multilingual_good | sed "$2" > "$T/m.bad"
        m=$( fail=0; checked=0; judge_report multilingual "$T/m.bad" >/dev/null; echo "$fail" )
        if [ "$m" -ge 1 ]; then printf '    %-62s %s\n' "$1" "refused"
        else printf '    %-62s %s\n' "$1" "ACCEPTED -- the check is dead"; fail=$((fail+1)); fi
        checked=$((checked+1))
    }
    mrep "...with no Fcitx service evidence"       '/^PROFILETEST fcitx_unit=/d'
    mrep "...with a failed Fcitx service"          's/fcitx_unit=active/fcitx_unit=failed/'
    mrep "...with conflicting IBus active"         's/ibus_unit=inactive/ibus_unit=active/'
    mrep "...without GTK input-method environment" '/^PROFILETEST fcitx_gtk=/d'
    mrep "...with an untested Qt input method"      's/fcitx_qt=/fcitx_qt=fcitx/'
    mrep "...with an X input-method environment"   's/fcitx_xmodifiers=/fcitx_xmodifiers=@im=fcitx/'
    mrep "...with the wrong Fcitx executable"      's/fcitx_identity=exact/fcitx_identity=wrong/'
    mrep "...with the wrong D-Bus owner"           's/fcitx_owner=exact/fcitx_owner=wrong/'
    mrep "...with an X11 fallback"                 's/fcitx_display=absent/fcitx_display=present/'
    mrep "...with a Wayland daemon connection"     's/fcitx_wayland=absent/fcitx_wayland=present/'
    mrep "...with no Pinyin engine evidence"       '/^PROFILETEST fcitx_pinyin=/d'
    mrep "...without a live Pinyin engine"         's/fcitx_pinyin=yes/fcitx_pinyin=no/'

    # Retained-image diagnostics still describe the older, source-bound IBus
    # image. They must keep their own baseline instead of being relabelled as
    # evidence for the new production Fcitx wiring.
    multilingual_legacy_ibus_good() {
        multilingual_good | sed \
            -e '/^PROFILETEST fcitx_/d' \
            -e 's/^PROFILETEST ibus_unit=inactive/PROFILETEST ibus_unit=active/' \
            -e '/^PROFILETEST ibus_unit=active/a\
PROFILETEST ibus_gtk=ibus\
PROFILETEST ibus_qt=ibus\
PROFILETEST ibus_xmodifiers=@im=ibus\
PROFILETEST ibus_wayland=wayland-0\
PROFILETEST ibus_display=:0\
PROFILETEST ibus_pinyin=yes'
    }
    multilingual_legacy_ibus_good > "$T/m.legacy"
    m=$( PROFILETEST_FCITX_CLIENT_PROBE=1; export PROFILETEST_FCITX_CLIENT_PROBE
         fail=0; checked=0; judge_report multilingual "$T/m.legacy" >/dev/null; echo "$fail" )
    if [ "$m" = 0 ]; then
        printf '    %-62s %s\n' "a retained IBus image keeps source-specific expectations" "accepted"
    else
        printf '    %-62s %s\n' "a retained IBus image keeps source-specific expectations" "REFUSED ($m)"
        fail=$((fail+1))
    fi
    checked=$((checked+1))

    # The functional judge requires two complete rounds and exact evidence.
    # Its positive fixture proves the expected shape; each mutation below
    # proves a screenshot or a vague success line cannot replace real text.
    functional_fixture() { # directory
        d=$1; rm -rf "$d"; mkdir -p "$d/evidence/round1" "$d/evidence/round2" "$d/shots"
        for n in 1 2; do
            python3 - "$d/evidence/round$n/mousepad.txt" <<'EOPY'
import sys
open(sys.argv[1], 'wb').write(bytes.fromhex('e4bda0e5a5bd'))
EOPY
            printf '%s\n' \
                '127.0.0.1 - - [test] "GET / HTTP/1.1" 200 -' \
                '127.0.0.1 - - [test] "GET /?text=%E4%BD%A0%E5%A5%BD HTTP/1.1" 200 -' \
                > "$d/evidence/round$n/firefox-http.log"
            printf '%s\n' 'GTK_IM_MODULE=ibus' 'WAYLAND_DISPLAY=wayland-0' 'DISPLAY=:0' \
                > "$d/evidence/round$n/firefox.env"
            printf '%s\n' 'mousepad=pass' 'firefox=pass' 'status=pass' \
                > "$d/evidence/round$n/result"
            : > "$d/shots/ibus-round$n-mousepad-candidate.png"
            : > "$d/shots/ibus-round$n-firefox-candidate.png"
            # A real PNG is nonempty; the content is tested by shot_py above.
            printf x >> "$d/shots/ibus-round$n-mousepad-candidate.png"
            printf x >> "$d/shots/ibus-round$n-firefox-candidate.png"
        done
    }
    functional_refuses() { # fixture dir -> number of failures
        ( fail=0; checked=0
          judge_ibus_functional "$1/evidence" "$1/shots" >/dev/null 2>&1
          echo "$fail" )
    }
    functional_case() { # description mutation-command
        functional_fixture "$T/functional"
        sh -c "$2" sh "$T/functional"
        n=$(functional_refuses "$T/functional")
        if [ "$n" -ge 1 ]; then printf '    %-62s %s\n' "$1" "refused"
        else printf '    %-62s %s\n' "$1" "ACCEPTED -- the functional check is dead"; fail=$((fail+1)); fi
        checked=$((checked+1))
    }
    functional_fixture "$T/functional"
    n=$(functional_refuses "$T/functional")
    if [ "$n" = 0 ]; then printf '    %-62s %s\n' "two exact Pinyin application rounds" "accepted"
    else printf '    %-62s %s\n' "two exact Pinyin application rounds" "REFUSED ($n)"; fail=$((fail+1)); fi
    checked=$((checked+1))
    functional_case "...with Latin text saved by Mousepad" \
        'printf nihao > "$1/evidence/round1/mousepad.txt"'
    functional_case "...with a different Firefox form value" \
        'f="$1/evidence/round1/firefox-http.log"; sed "s/%E5%A5%BD/%E5%A5%BF/" "$f" > "$f.new" && mv "$f.new" "$f"'
    functional_case "...without Firefox inheriting GTK IBus" \
        'f="$1/evidence/round1/firefox.env"; sed "/^GTK_IM_MODULE=/d" "$f" > "$f.new" && mv "$f.new" "$f"'
    functional_case "...with a missing candidate screenshot" \
        'rm "$1/shots/ibus-round1-firefox-candidate.png"'
    functional_case "...with no successful post-restart round" \
        'rm -rf "$1/evidence/round2"'
    x11_fixture() { # directory identity-state status
        d=$1; identity=$2; probe_status=$3
        rm -rf "$d"; mkdir -p "$d/evidence" "$d/shots"
        cat > "$d/evidence/probe.log" <<EOF
default_panel_count=0
default_panel_quiescence_s=5
panel_start_pid=20 panel_retained_pid=20
focus_away_pid=22 gone=yes
cleanup_unit=auros-profiletest-ibus-x11-focus-away active_state=inactive
cleanup_unit=auros-profiletest-ibus-x11-mousepad active_state=inactive
cleanup_unit=auros-profiletest-ibus-x11-panel active_state=inactive
cleanup_unit=auros-profiletest-ibus-x11-daemon active_state=inactive
cleanup_pid=22 gone=yes
cleanup_pid=21 gone=yes
cleanup_pid=20 gone=yes
cleanup_pid=19 gone=yes
ordinary_service_ready=1 main_pid=30
status=$probe_status
EOF
        printf 'identity_state=%s\n' "$identity" > "$d/evidence/candidate-summary.txt"
        printf '%s\n' 'preedit_candidate_xid=0x20' 'return_candidate_xid=0x20' \
            'xid_reused=yes' 'preedit_map_state=2' \
            'focus_away_map_state=0' 'focus_return_map_state=2' \
            > "$d/evidence/candidate-lifecycle.txt"
        printf '%s\n' 'GDK_BACKEND=wayland' > "$d/evidence/focus-away.env"
        printf '%s\n' 'exe=/usr/libexec/ibus-ui-gtk3' > "$d/evidence/panel-start.process"
        printf '%s\n' 'exe=/usr/bin/ibus-daemon' \
            'cmdline=/usr/bin/ibus-daemon --replace --xim ' \
            > "$d/evidence/shipped-service.process"
        printf '%s\n' pinyin > "$d/evidence/shipped-engine-ids.txt"
        mkdir -p "$d/evidence/runtime-output"
        printf ' ' > "$d/evidence/runtime-output/focus-away.txt"
        printf x > "$d/shots/ibus-x11-before.png"
        printf x > "$d/shots/ibus-x11-preedit.png"
        printf x > "$d/shots/ibus-x11-focus-away.png"
        printf x > "$d/shots/ibus-x11-focus-return.png"
    }
    x11_refuses() {
        ( fail=0; checked=0
          judge_ibus_x11_probe "$1/evidence" "$1/shots" >/dev/null 2>&1
          echo "$fail" )
    }
    x11_fixture "$T/x11" inconclusive inconclusive
    n=$(x11_refuses "$T/x11")
    x11_judgement=$(judge_ibus_x11_probe "$T/x11/evidence" "$T/x11/shots")
    case "$x11_judgement" in *'INCONCLUSIVE -- no product eligibility'*) neutral=yes ;;
      *) neutral=no ;;
    esac
    if [ "$n" = 0 ] && [ "$neutral" = yes ]; then
        printf '    %-62s %s\n' "empty XRes window/PID evidence stays neutral" "accepted"
    else
        printf '    %-62s %s\n' "empty XRes window/PID evidence stays neutral" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    x11_fixture "$T/x11" mapped complete
    sed 's/focus_away_map_state=0/focus_away_map_state=2/' \
        "$T/x11/evidence/candidate-lifecycle.txt" \
        > "$T/x11/evidence/candidate-lifecycle.new"
    mv "$T/x11/evidence/candidate-lifecycle.new" \
        "$T/x11/evidence/candidate-lifecycle.txt"
    n=$(x11_refuses "$T/x11")
    if [ "$n" -ge 1 ]; then
        printf '    %-62s %s\n' "X11 candidate left mapped across native focus loss" "refused"
    else
        printf '    %-62s %s\n' "X11 candidate left mapped across native focus loss" "ACCEPTED"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    x11_fixture "$T/x11" identity-only complete
    rm "$T/x11/evidence/shipped-engine-ids.txt"
    n=$(x11_refuses "$T/x11")
    if [ "$n" -ge 1 ]; then
        printf '    %-62s %s\n' "X11 cleanup without restored Pinyin readiness" "refused"
    else
        printf '    %-62s %s\n' "X11 cleanup without restored Pinyin readiness" "ACCEPTED"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    sh -n tools/profiletest-ibus-guest.sh || { printf '    %-62s %s\n' \
        "the injected Pinyin guest orchestrator parses" "FAIL"; fail=$((fail+1)); }
    checked=$((checked+1))
    if sh tools/profiletest-ibus-guest.sh selftest >/dev/null; then
        printf '    %-62s %s\n' "the bounded IBus engine selector" "passes"
    else
        printf '    %-62s %s\n' "the bounded IBus engine selector" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if python3 tools/test_x11_xres_probe.py >/dev/null; then
        printf '    %-62s %s\n' "the bounded raw XRes diagnostic parser" "passes"
    else
        printf '    %-62s %s\n' "the bounded raw XRes diagnostic parser" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if PROFILETEST_IBUS_X11_PROBE=1 PROFILETEST_IBUS_FUNCTIONAL=0 \
       sh tools/profiletest.sh boot multilingual 2>&1 \
           | grep -Fq 'requires the multilingual functional run'; then
        printf '    %-62s %s\n' "the X11 diagnostic cannot silently add boot cost" "passes"
    else
        printf '    %-62s %s\n' "the X11 diagnostic cannot silently add boot cost" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    fcitx_fixture="$T/fcitx"; mkdir -p "$fcitx_fixture/evidence" "$fcitx_fixture/shots"
    printf 'status=pass\nreason=test\n' > "$fcitx_fixture/evidence/result"
    for app in mousepad firefox; do
        printf 'parented_mapped_popup_count=1\nparented_popup_syntax_observed=yes\nconnection_identity=unproven\nlayer_shell_objects=0\npopup_input_grab=no\npopup_width=320\npopup_height=90\n' \
            > "$fcitx_fixture/evidence/$app-popup.txt"
        printf '\344\275\240\345\245\275' > "$fcitx_fixture/evidence/$app.txt"
        printf 'changed_samples=400 total_samples=64000\n' > "$fcitx_fixture/evidence/fcitx-$app-candidate-change.txt"
    done
    printf 'display_unset=yes\nwayland_display_unset=yes\nservice_owner_stable=yes\n' \
        > "$fcitx_fixture/evidence/fcitx.process"
    printf 'mousepad=pinyin\nfirefox=pinyin\n' > "$fcitx_fixture/evidence/selected-engines.txt"
    printf 'raw_protocol_logs_retained=no\n' > "$fcitx_fixture/evidence/protocol-summary.txt"
    printf 'ordinary_service_ready=1\nuser_config_unchanged=yes\nsource=retained-ibus-conversion\n' > "$fcitx_fixture/evidence/restore.txt"
    for f in fcitx-mousepad-candidate fcitx-mousepad-committed \
             fcitx-firefox-candidate fcitx-firefox-submitted; do printf x > "$fcitx_fixture/shots/$f.png"; done
    fcitx_refuses() {
        ( fail=0; checked=0; judge_fcitx_client_probe "$1/evidence" "$1/shots" >/dev/null 2>&1; echo "$fail" )
    }
    printf 'parented_mapped_popup_count=0\n' > "$fcitx_fixture/evidence/firefox-popup.txt"
    if [ "$(fcitx_refuses "$fcitx_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx evidence without parented-popup syntax" "refused"
    else
        printf '    %-62s %s\n' "Fcitx evidence without parented-popup syntax" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    printf 'parented_mapped_popup_count=1\nparented_popup_syntax_observed=yes\nconnection_identity=unproven\nlayer_shell_objects=0\npopup_input_grab=no\npopup_width=320\npopup_height=90\n' \
        > "$fcitx_fixture/evidence/firefox-popup.txt"
    printf 'mousepad=pinyin\n' > "$fcitx_fixture/evidence/selected-engines.txt"
    if [ "$(fcitx_refuses "$fcitx_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx evidence without both focused engine checks" "refused"
    else
        printf '    %-62s %s\n' "Fcitx evidence without both focused engine checks" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    printf 'mousepad=pinyin\nfirefox=pinyin\n' > "$fcitx_fixture/evidence/selected-engines.txt"
    printf 'ordinary_service_ready=0\nuser_config_unchanged=no\nsource=retained-ibus-conversion\n' > "$fcitx_fixture/evidence/restore.txt"
    if [ "$(fcitx_refuses "$fcitx_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx evidence without config and IBus restoration" "refused"
    else
        printf '    %-62s %s\n' "Fcitx evidence without config and IBus restoration" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    printf 'ordinary_service_ready=1\nuser_config_unchanged=yes\nsource=shipped-fcitx\n' \
        > "$fcitx_fixture/evidence/restore.txt"
    printf 'display_unset=yes\nwayland_display_unset=yes\nservice_owner_stable=yes\nunit=auros-fcitx.service\nibus_inactive=yes\nstart_time=123\n' \
        > "$fcitx_fixture/evidence/fcitx.process"
    printf 'GDK_BACKEND=wayland\nGTK_IM_MODULE=fcitx\nWAYLAND_DISPLAY=wayland-0\n' \
        > "$fcitx_fixture/evidence/mousepad.env"
    cp "$fcitx_fixture/evidence/mousepad.env" "$fcitx_fixture/evidence/firefox.env"
    production_refuses() {
        ( fail=0; checked=0; judge_fcitx_production "$1/evidence" "$1/shots" >/dev/null 2>&1; echo "$fail" )
    }
    if [ "$(production_refuses "$fcitx_fixture")" = 0 ]; then
        printf '    %-62s %s\n' "shipped Fcitx application evidence" "accepted"
    else
        printf '    %-62s %s\n' "shipped Fcitx application evidence" "REFUSED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/unit=auros-fcitx.service/unit=auros-profiletest-fcitx-daemon/' \
        "$fcitx_fixture/evidence/fcitx.process" > "$fcitx_fixture/evidence/fcitx.process.tmp"
    mv "$fcitx_fixture/evidence/fcitx.process.tmp" "$fcitx_fixture/evidence/fcitx.process"
    if [ "$(production_refuses "$fcitx_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production evidence from a transient Fcitx daemon" "refused"
    else
        printf '    %-62s %s\n' "production evidence from a transient Fcitx daemon" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/unit=auros-profiletest-fcitx-daemon/unit=auros-fcitx.service/' \
        "$fcitx_fixture/evidence/fcitx.process" > "$fcitx_fixture/evidence/fcitx.process.tmp"
    mv "$fcitx_fixture/evidence/fcitx.process.tmp" "$fcitx_fixture/evidence/fcitx.process"
    sed 's/GTK_IM_MODULE=fcitx/GTK_IM_MODULE=ibus/' "$fcitx_fixture/evidence/firefox.env" \
        > "$fcitx_fixture/evidence/firefox.env.tmp"
    mv "$fcitx_fixture/evidence/firefox.env.tmp" "$fcitx_fixture/evidence/firefox.env"
    if [ "$(production_refuses "$fcitx_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production evidence with a forced Firefox input module" "refused"
    else
        printf '    %-62s %s\n' "production evidence with a forced Firefox input module" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sh -n tools/profiletest-fcitx-guest.sh || { printf '    %-62s %s\n' \
        "the injected Fcitx guest orchestrator parses" "FAIL"; fail=$((fail+1)); }
    checked=$((checked+1))
    if python3 tools/test_fcitx_wayland_trace.py >/dev/null; then
        printf '    %-62s %s\n' "the Fcitx parented-popup trace parser" "passes"
    else
        printf '    %-62s %s\n' "the Fcitx parented-popup trace parser" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if python3 -m unittest -q tools/test_fcitx_connection_probe.py \
        tools/test_fcitx_password_server.py tools/test_fcitx_marker_visibility.py; then
        printf '    %-62s %s\n' "the Fcitx connection, password, and marker reducers" "pass"
    else
        printf '    %-62s %s\n' "the Fcitx connection, password, and marker reducers" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if python3 - tools/profiletest-fcitx-guest.sh tools/profiletest.sh .github/workflows/profiles.yml <<'EOPY'
import sys
text = open(sys.argv[1], encoding="utf-8").read()
preflight = text.index('fcitx5-remote -m pinyin')
mouse_focus = text.index('act click-change fcitx-mousepad')
mouse_select = text.index('select_exact_pinyin mousepad')
firefox_focus = text.index('act click-form')
firefox_select = text.index('select_exact_pinyin firefox')
if not preflight < mouse_focus < mouse_select < firefox_focus < firefox_select:
    raise SystemExit("Pinyin selection must follow each real focused input context")
helper = text[text.index('select_exact_pinyin()'):text.index('restore_and_finish()')]
if 'fcitx5-remote -s pinyin' not in helper or 'fcitx5-remote -n' not in helper:
    raise SystemExit("focused selector must set and verify the exact engine")
security_start = text.index('if [ "$SECURITY" = 1 ]; then', text.index('trace_has_no_input_text "$mouse_trace"'))
security_end = text.index('\nfi\n', security_start)
security = text[security_start:security_end]
ordered = (
    "while [ \"$i\" -le 24 ]",
    'run_app mousepad-isolated "$mouse_b_file"',
    'mousepad-b.process',
    'mousepad-b-connection.txt',
    '--toplevel > "$STATE/mousepad-b-toplevel.tmp"',
    'act wait-change "$mouse_b_evidence"',
    'act click-change "$mouse_b_evidence"',
    'act chord ctrl q',
    'act key backspace',
    'act key spc',
    'wait_hex_file "$mouse_b_file" 20',
    '[ ! -s "$mouse_file" ]',
)
positions = [security.index(item) for item in ordered]
if positions != sorted(positions):
    raise SystemExit("second-editor identity, observed click, AZERTY clearing, and sentinel proof are out of order")
if ('/usr/bin/mousepad --disable-server' not in security
        or "env.get(b'GDK_BACKEND') != b'wayland'" not in security
        or 'gdk_backend=wayland' not in security):
    raise SystemExit("second editor must be a distinct native Wayland Mousepad process")
restore_probe = text[text.index('restore_and_finish()'):text.index(': > "$STATE/cleanup-units.txt"')]
if restore_probe.index('connection_b_done') > restore_probe.index('connection_probe_pid'):
    raise SystemExit("cleanup must finalize the second-editor interval before the first-editor interval")
if 'top:calc(50% - 70px)' not in text or 'height:140px' not in text:
    raise SystemExit("Firefox field must be centered on the measured form click point")
password_test = text.index('password_summary="$RUNTIME/password-summary"')
password_visible = text.index('act wait-form', password_test)
preclick_absence = text.index('[ ! -e "$password_focus" ] || fail "password focused before injected click"', password_visible)
password_click = text.index('act click-form', password_visible)
if not password_visible < preclick_absence < password_click:
    raise SystemExit("password focus evidence must be absent immediately before the injected click")
identity_before = text.index('password_fcitx_owner=$(fcitx_owner_name)', password_visible)
password_boundary = text.index('password_boundary=$(wc -c < "$password_trace"', password_visible)
identity_after = text.index('verify_fcitx_identity "$password_fcitx_owner"', password_click)
popup_reduce = text.index('fcitx_wayland_trace.py "$password_trace"', password_click)
if not identity_before < password_boundary < password_click < identity_after < popup_reduce:
    raise SystemExit("Fcitx identity must bind both sensitive-phase boundaries before reduction")
password_launch = text[text.index('if [ "$PRODUCTION" = 1 ]; then', password_test):password_visible]
production_password, legacy_password = password_launch.split('\n    else\n', 1)
if 'GTK_IM_MODULE=fcitx' in production_password or 'XDG_CONFIG_HOME="$CFG"' in production_password:
    raise SystemExit("production password Firefox must inherit the shipped input configuration")
if 'GTK_IM_MODULE=fcitx' not in legacy_password or 'XDG_CONFIG_HOME="$CFG"' not in legacy_password:
    raise SystemExit("retained conversion password Firefox must keep its isolated configuration")
round_two = text[text.index('if [ "$SECURITY" = 1 ] && [ "$security_round" = 2 ]'):text.index('\nif [ "$PRODUCTION" = 1 ]; then', text.index('if [ "$SECURITY" = 1 ] && [ "$security_round" = 2 ]'))]
for required in ('shipped_fcitx_autostart_ready=yes', 'fcitx_autostart_before_explicit_launch=no'):
    if required not in round_two:
        raise SystemExit("round two must retain source-specific reboot evidence")
restore = text[text.index('restore_and_finish()'):text.index('fail() {')]
loop = restore[restore.index('ibus_deadline='):restore.index('unchanged=no')]
for required in ('as_desktop_until "$ibus_deadline" systemctl --user is-active',
                 'ibus_identity_snapshot "$ibus_deadline" "$ipid"',
                 'ibus list-engine --name-only',
                 'grep -Fqx pinyin',
                 'ibus_identity_snapshot "$ibus_deadline" "$ipid_after"',
                 '"$identity_after" = "$identity_before"'):
    if required not in loop:
        raise SystemExit("IBus restoration must retry every exact readiness predicate")
for phase in ('service_start_rc', 'service_active_seen',
              'service_identity_seen', 'service_identity_stable_seen',
              'pinyin_engine_seen'):
    if phase not in restore:
        raise SystemExit("IBus restoration must retain bounded phase evidence")
identity = text[text.index('ibus_identity_snapshot()'):text.index('user_env=')]
if 'cmdline != [b"/usr/bin/ibus-daemon", b"--replace", b"--xim", b""]' not in identity:
    raise SystemExit("IBus restoration must authenticate exact argv tokens")
if ('as_desktop_until()' not in text or 'remaining=$((deadline-now))' not in text
        or 'identity_remaining=$((identity_deadline-identity_now))' not in text):
    raise SystemExit("IBus restoration must share one monotonic deadline")
for status in ('active_before_rc', 'ipid_rc', 'identity_before_rc', 'engines_rc',
               'active_after_rc', 'ipid_after_rc', 'identity_after_rc'):
    if f'[ "${status}" = 0 ]' not in loop:
        raise SystemExit("IBus restoration must reject partial failed command output")
round_copy = text.index('find "$STATE" -maxdepth 1 -type f ! -name security-next-round -exec cp -p')
final_report = text.index('say "fcitx-probe=$final_state', round_copy)
if '\n    sync\n' not in text[round_copy:final_report]:
    raise SystemExit("each security round must persist its copied evidence before reporting done")
early_reports = (
    ('Noble package indexes unavailable',
     'say "fcitx-probe=unavailable reason=Noble package indexes unavailable"'),
    ('Noble Fcitx packages unavailable',
     'say "fcitx-probe=unavailable reason=Noble Fcitx packages unavailable"'),
    ('source-reviewed package versions mismatch',
     'say "fcitx-probe=fail reason=source-reviewed package versions mismatch"'),
)
def early_flush_errors(source):
    errors = []
    for reason, marker in early_reports:
        report = source.index(marker)
        branch = source[source.rfind('\nif ', 0, report):report]
        result = branch.rfind('> "$STATE/result"')
        restore = branch.rfind('> "$STATE/restore.txt"')
        persisted = branch.rfind('\n    sync\n')
        if min(result, restore, persisted) < 0 or not result < restore < persisted:
            errors.append(reason)
    return errors
if errors := early_flush_errors(text):
    raise SystemExit(f"early exits must persist reduced evidence before done: {errors}")
for reason, marker in early_reports:
    report = text.index(marker)
    persisted = text.rfind('\n    sync\n', 0, report)
    mutant = text[:persisted] + text[persisted + len('\n    sync\n'):]
    if reason not in early_flush_errors(mutant):
        raise SystemExit(f"selftest did not reject missing early evidence flush: {reason}")
host = open(sys.argv[2], encoding="utf-8").read()
production_probe = host.index('[ ! -f /usr/lib/auros-profiletest/fcitx-production.enabled ]')
legacy_probe = host.index('ibus_unit=$(as_desktop systemctl --user is-active', production_probe)
production_facts = host.index('fcitx_unit=$(as_desktop systemctl --user is-active', legacy_probe)
if not production_probe < legacy_probe < production_facts:
    raise SystemExit("production marker must bypass retained-IBus fact collection")
action_helper = host.index('ibus_host_action() {', host.index('# ── start it'))
action_result = host[action_helper:host.index('\n}\n\nT0=', action_helper)]
marker_check = action_result.index('[ -f "$CHAN/failed-$id" ]')
dispatch = action_result.index('case "$verb" in')
if marker_check > dispatch:
    raise SystemExit("a published action failure must be replayed as an acknowledgement, not as an action")
result_branch = action_result.index('if [ "$action_ok" = 0 ]', dispatch)
failure = action_result[action_result.index('else', result_branch):]
marker_write = failure.index(': > "$CHAN/failed-$id"')
ack_write = failure.index('printf \'%s\\n\' "$id" > "$CHAN/.acks"')
if marker_write > ack_write or 'if : > "$CHAN/failed-$id"; then' not in failure:
    raise SystemExit("an action failure must be published before its acknowledgement")
wait_change = action_result[action_result.index('wait-change)'):action_result.index('wait-form)')]
for required in ('fcitx-round*-mousepad-b', '$EV/$1-before.png', '$EV/$1-after.png', '$EV/$1-visibility.txt'):
    if required not in wait_change:
        raise SystemExit("second-editor baseline, final frame, and reduced delta must be retained")
workflow = open(sys.argv[3], encoding="utf-8").read()
for asset in ('ev/fcitx-round*-mousepad-b-before.png',
              'ev/fcitx-round*-mousepad-b-after.png',
              'ev/fcitx-round*-mousepad-b-visibility.txt'):
    if workflow.count(asset) != 1:
        raise SystemExit(f"diagnostic release must publish exactly one bounded asset glob: {asset}")
EOPY
    then
        printf '    %-62s %s\n' "Fcitx centers each field before focused Pinyin selection" "passes"
    else
        printf '    %-62s %s\n' "Fcitx centers each field before focused Pinyin selection" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if PROFILETEST_FCITX_CLIENT_PROBE=1 PROFILETEST_IBUS_X11_PROBE=1 \
       sh tools/profiletest.sh boot multilingual 2>&1 | grep -Fq 'cannot be combined'; then
        printf '    %-62s %s\n' "the Fcitx and X11 diagnostics are exclusive" "passes"
    else
        printf '    %-62s %s\n' "the Fcitx and X11 diagnostics are exclusive" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    if PROFILETEST_FCITX_SECURITY_PROBE=1 PROFILETEST_FCITX_CLIENT_PROBE=0 \
       sh tools/profiletest.sh boot multilingual 2>&1 \
           | grep -Fq 'requires a Fcitx client or production probe'; then
        printf '    %-62s %s\n' "the Fcitx security stage cannot silently enable its prerequisite" "passes"
    else
        printf '    %-62s %s\n' "the Fcitx security stage cannot silently enable its prerequisite" "FAIL"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    security_fixture="$T/fcitx-security"; mkdir -p "$security_fixture/evidence" "$security_fixture/shots"
    for round in 1 2; do
        R="$security_fixture/evidence/round$round"; mkdir -p "$R"
        printf 'status=pass\n' > "$R/result"
        printf 'status=pass\nchildren_at_boundaries=0\ncompositor_connections_at_boundaries=1\napplication_trace_writers_at_boundaries=1\nforeign_trace_writers_at_boundaries=0\nauthenticated_probe_trace_writer_at_end=1\npeer_pid=exact_aurshell\npeer_path=exact_wayland_display\nlistener_inode=exact_wayland_display\nservice_main_pid=exact_dbus_run_session\ncompositor_exe=exact_usr_bin_aurshell\nservice_invocation_id=exact_systemd_property_and_environment\nservice_control_group=exact_systemd_property\npam_session_cgroup=exact_expected_user_session_scope\ncompositor_in_pam_session_cgroup=yes\ncompositor_descends_from_service_main_pid=yes\nobserved_service_main_pid=100\nobserved_compositor_pid=102\nobserved_service_control_group=/system.slice/aurshell.service\nobserved_pam_session_control_group=/user.slice/user-1000.slice/session-c1.scope\n' > "$R/mousepad-connection.txt"
        for phase in start end; do
            for metric in wayland_path_inode_count global_matching_listener_count \
                after_wayland_path_inode_count after_global_matching_listener_count compositor_socket_fd_count \
                matching_accepting_listener_fd_count matching_accepting_listener_open_description_count \
                matching_accepting_listener_count application_descendant_count trace_writer_count \
                application_trace_writer_count probe_trace_writer_count foreign_trace_writer_count \
                application_socket_fd_count application_socket_inspection_error_count \
                matching_compositor_connection_count trace_sink_identity_exact; do
                metric_value=1
                case "$metric" in
                  application_descendant_count|foreign_trace_writer_count|application_socket_inspection_error_count) metric_value=0 ;;
                  probe_trace_writer_count) [ "$phase" = start ] && metric_value=0 ;;
                  trace_writer_count) [ "$phase" = end ] && metric_value=2 ;;
                  trace_sink_identity_exact) metric_value=yes ;;
                esac
                printf 'observed_%s_%s=%s\n' "$phase" "$metric" \
                    "$metric_value" \
                    >> "$R/mousepad-connection.txt"
            done
        done
        cp "$R/mousepad-connection.txt" "$R/mousepad-b-connection.txt"
        printf 'unit=auros-profiletest-fcitx-mousepad-b\npid=200\nstart_time=300\nexe=/usr/bin/mousepad\nargv=/usr/bin/mousepad --disable-server <marker-file>\nmarker_sha256=665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1\ngdk_backend=wayland\ngtk_im_module=fcitx\nwayland_display_set=yes\ndisplay_unset=yes\n' > "$R/mousepad-b.process"
        printf 'toplevel_xdg_surface=20\ntoplevel_wl_surface=19\nmapped_toplevel_count=1\nlayer_shell_objects=0\n' > "$R/mousepad-b-toplevel.txt"
        printf 'initial_popup_wl_surface=30\nreturn_popup_wl_surface=31\ninitial_unmapped_before_b_input=yes\nmapped_candidate_while_away=no\nreturn_map_epoch=fresh\nreturn_popup_reused=no\npopup_input_grab=no\nlayer_shell_objects=0\nb_saved_sentinel=20\na_saved_file_unchanged=yes\n' > "$R/focus-lifecycle.txt"
        printf '20\n' > "$R/focus-b-sentinel.hex"
        printf 'candidate_popup_creations=0\ncandidate_popup_maps=0\ncandidate_popup_configures=0\ncandidate_popup_grabs=0\nlayer_shell_objects=0\nobserved_candidate_popup=no\nfocus_event_observed=yes\nascii_value_exact=yes\ncomposition_start_count=0\ncomposition_update_count=0\ncomposition_end_count=0\ncomposition_input_count=0\nraw_password_focus_log_retained=no\nraw_password_value_log_retained=no\nraw_password_event_log_retained=no\n' > "$R/password-suppression.txt"
        printf 'pid=123\nstart_time=456\nexe=/usr/bin/fcitx5\ncmdline=/usr/bin/fcitx5 -D\ndbus_owner=:1.%s\nidentity_before_sensitive=yes\nidentity_after_sensitive=yes\n' \
            "$round" > "$R/service-owner-sensitive.txt"
        cat > "$R/password-popup-events.txt" <<'EOF'
candidate_popup_creations=0
candidate_popup_maps=0
candidate_popup_configures=0
candidate_popup_grabs=0
candidate_popup_configures_matched=0
candidate_popup_configures_unmatched=0
candidate_popup_grabs_matched=0
candidate_popup_grabs_unmatched=0
candidate_objects_at_focus=0
candidate_objects_relevant=0
candidate_objects_recorded=0
candidate_objects_omitted=0
candidate_objects_mapped_at_end=0
candidate_objects_destroyed_after_focus=0
reduced_events_recorded=0
reduced_events_omitted=0
layer_shell_objects=0
observed_candidate_popup=no
connection_identity=unproven
popup_attribution=unproven
focus_boundary_byte=10
input_boundary_byte=20
screenshot_boundary_byte=30
submit_boundary_byte=30
focus_to_input_popup_events=0
focus_to_input_map_events=0
focus_to_input_configure_events=0
focus_to_input_grab_events=0
input_to_screenshot_popup_events=0
input_to_screenshot_map_events=0
input_to_screenshot_configure_events=0
input_to_screenshot_grab_events=0
screenshot_to_submit_popup_events=0
screenshot_to_submit_map_events=0
screenshot_to_submit_configure_events=0
screenshot_to_submit_grab_events=0
after_submit_popup_events=0
after_submit_map_events=0
after_submit_configure_events=0
after_submit_grab_events=0
EOF
        printf 'ordinary_service_ready=1\nuser_config_unchanged=yes\ncleanup_pids_gone=1\nsource=retained-ibus-conversion\n' > "$R/restore.txt"
        printf 'exe=/usr/bin/fcitx5\ncmdline=/usr/bin/fcitx5 -D\nconfig_hash=abc\ndisplay_unset=yes\nwayland_display_unset=yes\nservice_owner_stable=yes\nboot_id=00000000-0000-4000-8000-00000000000%s\n' "$round" > "$R/fcitx.process"
        printf 'GDK_BACKEND=wayland\nGTK_IM_MODULE=fcitx\nWAYLAND_DISPLAY=wayland-0\n' > "$R/password.env"
        for app in mousepad firefox; do
            printf '\344\275\240\345\245\275' > "$R/$app.txt"
            printf 'parented_mapped_popup_count=1\npopup_input_grab=no\nlayer_shell_objects=0\n' > "$R/$app-popup.txt"
        done
        printf 'mousepad=pinyin\nfirefox=pinyin\n' > "$R/selected-engines.txt"
        printf 'raw_protocol_logs_retained=no\n' > "$R/protocol-summary.txt"
        printf x > "$security_fixture/shots/fcitx-round$round-focus-away.png"
        printf x > "$security_fixture/shots/fcitx-round$round-password-focused.png"
        printf x > "$security_fixture/shots/fcitx-round$round-password-masked.png"
        printf x > "$security_fixture/shots/fcitx-round$round-mousepad-b-before.png"
        printf x > "$security_fixture/shots/fcitx-round$round-mousepad-b-after.png"
        printf 'status=pass\nmarker_template_sha256=665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1\nchanged_samples=2200\ntotal_samples=64000\nthreshold_percent=3\nmarker_pattern_steps=24\nmarker_bbox_left=244\nmarker_bbox_top=120\nmarker_bbox_right=1034\nmarker_bbox_bottom=650\nclick_x=640\nclick_y=384\nclick_pixel_delta=255\n' > "$security_fixture/shots/fcitx-round$round-mousepad-b-visibility.txt"
    done
    printf 'fcitx_autostart_before_explicit_launch=no\n' > "$security_fixture/evidence/round2/reboot.txt"
    security_refuses() {
        ( fail=0; checked=0; judge_fcitx_security_probe "$1/evidence" "$1/shots" >/dev/null 2>&1; echo "$fail" )
    }
    if [ "$(security_refuses "$security_fixture")" = 0 ]; then
        printf '    %-62s %s\n' "complete bounded Fcitx security evidence" "accepted"
    else
        printf '    %-62s %s\n' "complete bounded Fcitx security evidence" "REFUSED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    visibility="$security_fixture/shots/fcitx-round1-mousepad-b-visibility.txt"
    sed 's/changed_samples=2200/changed_samples=100/' "$visibility" > "$visibility.tmp"; mv "$visibility.tmp" "$visibility"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad evidence below the visibility threshold" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad evidence below the visibility threshold" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/changed_samples=100/changed_samples=2200/' "$visibility" > "$visibility.tmp"; mv "$visibility.tmp" "$visibility"
    sed 's/marker_pattern_steps=24/marker_pattern_steps=1/' "$visibility" > "$visibility.tmp"; mv "$visibility.tmp" "$visibility"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "screen change without the fixed multiline marker" "refused"
    else
        printf '    %-62s %s\n' "screen change without the fixed multiline marker" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/marker_pattern_steps=1/marker_pattern_steps=24/' "$visibility" > "$visibility.tmp"; mv "$visibility.tmp" "$visibility"
    sed 's/click_pixel_delta=255/click_pixel_delta=0/' "$visibility" > "$visibility.tmp"; mv "$visibility.tmp" "$visibility"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad click outside observed changed pixels" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad click outside observed changed pixels" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/click_pixel_delta=0/click_pixel_delta=255/' "$visibility" > "$visibility.tmp"; mv "$visibility.tmp" "$visibility"
    printf 'argv=/usr/bin/mousepad <marker-file>\n' >> "$security_fixture/evidence/round1/mousepad-b.process"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad evidence with contradictory argv" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad evidence with contradictory argv" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed -i.bak '/^argv=\/usr\/bin\/mousepad <marker-file>$/d' "$security_fixture/evidence/round1/mousepad-b.process"; rm "$security_fixture/evidence/round1/mousepad-b.process.bak"
    sed 's/gdk_backend=wayland/gdk_backend=x11/' "$security_fixture/evidence/round1/mousepad-b.process" \
        > "$security_fixture/evidence/round1/mousepad-b.process.tmp"
    mv "$security_fixture/evidence/round1/mousepad-b.process.tmp" "$security_fixture/evidence/round1/mousepad-b.process"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad evidence with an X11 backend" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad evidence with an X11 backend" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/gdk_backend=x11/gdk_backend=wayland/' "$security_fixture/evidence/round1/mousepad-b.process" \
        > "$security_fixture/evidence/round1/mousepad-b.process.tmp"
    mv "$security_fixture/evidence/round1/mousepad-b.process.tmp" "$security_fixture/evidence/round1/mousepad-b.process"
    printf 'mapped_toplevel_count=1\n' >> "$security_fixture/evidence/round1/mousepad-b-toplevel.txt"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad evidence with ambiguous mapped toplevels" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad evidence with ambiguous mapped toplevels" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed -i.bak '$d' "$security_fixture/evidence/round1/mousepad-b-toplevel.txt"; rm "$security_fixture/evidence/round1/mousepad-b-toplevel.txt.bak"
    sed 's/foreign_trace_writers_at_boundaries=0/foreign_trace_writers_at_boundaries=1/' \
        "$security_fixture/evidence/round1/mousepad-b-connection.txt" > "$security_fixture/evidence/round1/mousepad-b-connection.tmp"
    mv "$security_fixture/evidence/round1/mousepad-b-connection.tmp" "$security_fixture/evidence/round1/mousepad-b-connection.txt"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad evidence with a foreign trace writer" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad evidence with a foreign trace writer" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/foreign_trace_writers_at_boundaries=1/foreign_trace_writers_at_boundaries=0/' \
        "$security_fixture/evidence/round1/mousepad-b-connection.txt" > "$security_fixture/evidence/round1/mousepad-b-connection.tmp"
    mv "$security_fixture/evidence/round1/mousepad-b-connection.tmp" "$security_fixture/evidence/round1/mousepad-b-connection.txt"
    b_connection="$security_fixture/evidence/round1/mousepad-b-connection.txt"
    cp "$b_connection" "$b_connection.clean"
    for mutation in duplicate contradictory unknown; do
        case "$mutation" in
          duplicate) printf 'status=pass\n' >> "$b_connection" ;;
          contradictory) printf 'status=fail\n' >> "$b_connection" ;;
          unknown) printf 'unreviewed_claim=yes\n' >> "$b_connection" ;;
        esac
        if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
            printf '    %-62s %s\n' "second Mousepad connection with $mutation data" "refused"
        else
            printf '    %-62s %s\n' "second Mousepad connection with $mutation data" "ACCEPTED"; fail=$((fail+1))
        fi
        checked=$((checked+1))
        cp "$b_connection.clean" "$b_connection"
    done
    rm "$b_connection.clean"
    sed 's/observed_start_foreign_trace_writer_count=0/observed_start_foreign_trace_writer_count=99/' \
        "$b_connection" > "$b_connection.tmp"; mv "$b_connection.tmp" "$b_connection"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad connection with contradictory observed metric" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad connection with contradictory observed metric" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/observed_start_foreign_trace_writer_count=99/observed_start_foreign_trace_writer_count=0/' \
        "$b_connection" > "$b_connection.tmp"; mv "$b_connection.tmp" "$b_connection"
    printf '21\n' > "$security_fixture/evidence/round1/focus-b-sentinel.hex"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "second Mousepad evidence with the wrong saved sentinel" "refused"
    else
        printf '    %-62s %s\n' "second Mousepad evidence with the wrong saved sentinel" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    printf '20\n' > "$security_fixture/evidence/round1/focus-b-sentinel.hex"
    lifecycle="$security_fixture/evidence/round1/focus-lifecycle.txt"
    cp "$lifecycle" "$lifecycle.clean"
    for mutation in duplicate contradictory unknown; do
        case "$mutation" in
          duplicate) printf 'return_map_epoch=fresh\n' >> "$lifecycle" ;;
          contradictory) printf 'return_map_epoch=stale\n' >> "$lifecycle" ;;
          unknown) printf 'unreviewed_claim=yes\n' >> "$lifecycle" ;;
        esac
        if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
            printf '    %-62s %s\n' "focus lifecycle with $mutation data" "refused"
        else
            printf '    %-62s %s\n' "focus lifecycle with $mutation data" "ACCEPTED"; fail=$((fail+1))
        fi
        checked=$((checked+1))
        cp "$lifecycle.clean" "$lifecycle"
    done
    rm "$lifecycle.clean"
    production_security_fixture="$T/fcitx-production-security"
    cp -R "$security_fixture" "$production_security_fixture"
    for round in 1 2; do
        R="$production_security_fixture/evidence/round$round"
        printf 'ordinary_service_ready=1\nuser_config_unchanged=yes\ncleanup_pids_gone=1\nsource=shipped-fcitx\nservice_start_rc=not-needed\nservice_active_seen=no\nservice_identity_seen=no\nservice_identity_stable_seen=no\npinyin_engine_seen=no\n' > "$R/restore.txt"
        printf 'pid=123\nexe=/usr/bin/fcitx5\ncmdline=/usr/bin/fcitx5 -D\nconfig_hash=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\ndisplay_unset=yes\nwayland_display_unset=yes\nservice_owner_stable=yes\nboot_id=10000000-0000-4000-8000-00000000000%s\nunit=auros-fcitx.service\nibus_inactive=yes\nstart_time=123\n' "$round" > "$R/fcitx.process"
    done
    printf 'shipped_fcitx_autostart_ready=yes\n' > "$production_security_fixture/evidence/round2/reboot.txt"
    production_security_refuses() {
        ( fail=0; checked=0; judge_fcitx_security_probe "$1/evidence" "$1/shots" shipped-fcitx >/dev/null 2>&1; echo "$fail" )
    }
    if [ "$(production_security_refuses "$production_security_fixture")" = 0 ]; then
        printf '    %-62s %s\n' "complete shipped Fcitx security evidence" "accepted"
    else
        printf '    %-62s %s\n' "complete shipped Fcitx security evidence" "REFUSED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    cp "$production_security_fixture/evidence/round2/fcitx.process" "$T/production.process"
    sed 's/10000000-0000-4000-8000-000000000002/10000000-0000-4000-8000-000000000001/' \
        "$T/production.process" > "$production_security_fixture/evidence/round2/fcitx.process"
    if [ "$(production_security_refuses "$production_security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production security evidence from the same boot" "refused"
    else
        printf '    %-62s %s\n' "production security evidence from the same boot" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    cp "$T/production.process" "$production_security_fixture/evidence/round2/fcitx.process"
    printf 'unit=auros-profiletest-fcitx-daemon\n' >> "$production_security_fixture/evidence/round1/fcitx.process"
    if [ "$(production_security_refuses "$production_security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production security evidence with contradictory service identity" "refused"
    else
        printf '    %-62s %s\n' "production security evidence with contradictory service identity" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed -i.bak '/^unit=auros-profiletest-fcitx-daemon$/d' "$production_security_fixture/evidence/round1/fcitx.process"
    rm "$production_security_fixture/evidence/round1/fcitx.process.bak"
    printf 'GTK_IM_MODULE=ibus\n' >> "$production_security_fixture/evidence/round1/password.env"
    if [ "$(production_security_refuses "$production_security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production password evidence with contradictory input module" "refused"
    else
        printf '    %-62s %s\n' "production password evidence with contradictory input module" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed -i.bak '/^GTK_IM_MODULE=ibus$/d' "$production_security_fixture/evidence/round1/password.env"
    rm "$production_security_fixture/evidence/round1/password.env.bak"
    printf 'boot_id=20000000-0000-4000-8000-000000000001\n' >> "$production_security_fixture/evidence/round1/fcitx.process"
    if [ "$(production_security_refuses "$production_security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production security evidence with duplicate boot identity" "refused"
    else
        printf '    %-62s %s\n' "production security evidence with duplicate boot identity" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed -i.bak '/^boot_id=20000000-/d' "$production_security_fixture/evidence/round1/fcitx.process"
    rm "$production_security_fixture/evidence/round1/fcitx.process.bak"
    printf 'source=retained-ibus-conversion\n' >> "$production_security_fixture/evidence/round1/restore.txt"
    if [ "$(production_security_refuses "$production_security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production security evidence with contradictory source" "refused"
    else
        printf '    %-62s %s\n' "production security evidence with contradictory source" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed -i.bak '/^source=retained-ibus-conversion$/d' "$production_security_fixture/evidence/round1/restore.txt"
    rm "$production_security_fixture/evidence/round1/restore.txt.bak"
    printf 'fcitx_autostart_before_explicit_launch=no\n' > "$production_security_fixture/evidence/round2/reboot.txt"
    if [ "$(production_security_refuses "$production_security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "production security evidence without shipped reboot readiness" "refused"
    else
        printf '    %-62s %s\n' "production security evidence without shipped reboot readiness" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    password_evidence="$security_fixture/evidence/round1/password-suppression.txt"
    cp "$password_evidence" "$password_evidence.clean"
    for mutation in duplicate contradictory unknown; do
        case "$mutation" in
          duplicate) printf 'composition_update_count=0\n' >> "$password_evidence" ;;
          contradictory) printf 'composition_update_count=1\n' >> "$password_evidence" ;;
          unknown) printf 'unreviewed_claim=yes\n' >> "$password_evidence" ;;
        esac
        if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
            printf '    %-62s %s\n' "Fcitx password evidence with $mutation data" "refused"
        else
            printf '    %-62s %s\n' "Fcitx password evidence with $mutation data" "ACCEPTED"
            fail=$((fail+1))
        fi
        checked=$((checked+1))
        cp "$password_evidence.clean" "$password_evidence"
    done
    rm "$password_evidence.clean"
    popup_evidence="$security_fixture/evidence/round1/password-popup-events.txt"
    cp "$popup_evidence" "$popup_evidence.clean"
    for mutation in duplicate contradictory unknown attribution; do
        case "$mutation" in
          duplicate) printf 'focus_boundary_byte=10\n' >> "$popup_evidence" ;;
          contradictory) sed 's/observed_candidate_popup=no/observed_candidate_popup=yes/' \
              "$popup_evidence.clean" > "$popup_evidence" ;;
          unknown) printf 'unreviewed_protocol_claim=yes\n' >> "$popup_evidence" ;;
          attribution) sed 's/popup_attribution=unproven/popup_attribution=fcitx/' \
              "$popup_evidence.clean" > "$popup_evidence" ;;
        esac
        if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
            printf '    %-62s %s\n' "Fcitx popup evidence with $mutation data" "refused"
        else
            printf '    %-62s %s\n' "Fcitx popup evidence with $mutation data" "ACCEPTED"
            fail=$((fail+1))
        fi
        checked=$((checked+1))
        cp "$popup_evidence.clean" "$popup_evidence"
    done
    rm "$popup_evidence.clean"
    sensitive_identity="$security_fixture/evidence/round1/service-owner-sensitive.txt"
    cp "$sensitive_identity" "$sensitive_identity.clean"
    sed -i.bak '/^identity_after_sensitive=yes$/d' "$sensitive_identity"
    rm "$sensitive_identity.bak"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx sensitive phase without ending service identity" "refused"
    else
        printf '    %-62s %s\n' "Fcitx sensitive phase without ending service identity" "ACCEPTED"
        fail=$((fail+1))
    fi
    checked=$((checked+1))
    mv "$sensitive_identity.clean" "$sensitive_identity"
    sed 's/foreign_trace_writers_at_boundaries=0/foreign_trace_writers_at_boundaries=1/' \
        "$security_fixture/evidence/round1/mousepad-connection.txt" \
        > "$security_fixture/evidence/round1/mousepad-connection.tmp"
    mv "$security_fixture/evidence/round1/mousepad-connection.tmp" \
       "$security_fixture/evidence/round1/mousepad-connection.txt"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx connection evidence with a foreign trace writer" "refused"
    else
        printf '    %-62s %s\n' "Fcitx connection evidence with a foreign trace writer" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    sed 's/foreign_trace_writers_at_boundaries=1/foreign_trace_writers_at_boundaries=0/' \
        "$security_fixture/evidence/round1/mousepad-connection.txt" \
        > "$security_fixture/evidence/round1/mousepad-connection.tmp"
    mv "$security_fixture/evidence/round1/mousepad-connection.tmp" \
       "$security_fixture/evidence/round1/mousepad-connection.txt"
    sed '/^compositor_in_pam_session_cgroup=yes$/d' \
        "$security_fixture/evidence/round1/mousepad-connection.txt" \
        > "$security_fixture/evidence/round1/mousepad-connection.tmp"
    mv "$security_fixture/evidence/round1/mousepad-connection.tmp" \
       "$security_fixture/evidence/round1/mousepad-connection.txt"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx compositor outside authenticated service topology" "refused"
    else
        printf '    %-62s %s\n' "Fcitx compositor outside authenticated service topology" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    printf 'compositor_in_pam_session_cgroup=yes\n' \
        >> "$security_fixture/evidence/round1/mousepad-connection.txt"
    rm "$security_fixture/evidence/round2/focus-lifecycle.txt"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx security evidence missing focus revocation" "refused"
    else
        printf '    %-62s %s\n' "Fcitx security evidence missing focus revocation" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    printf 'initial_popup_wl_surface=30\nreturn_popup_wl_surface=31\ninitial_unmapped_before_b_input=yes\nmapped_candidate_while_away=no\nreturn_map_epoch=fresh\nreturn_popup_reused=no\npopup_input_grab=no\nlayer_shell_objects=0\nb_saved_sentinel=20\na_saved_file_unchanged=yes\n' > "$security_fixture/evidence/round2/focus-lifecycle.txt"
    sed 's/composition_update_count=0/composition_update_count=1/' \
        "$security_fixture/evidence/round1/password-suppression.txt" \
        > "$security_fixture/evidence/round1/password-suppression.tmp"
    mv "$security_fixture/evidence/round1/password-suppression.tmp" \
       "$security_fixture/evidence/round1/password-suppression.txt"
    if [ "$(security_refuses "$security_fixture")" -ge 1 ]; then
        printf '    %-62s %s\n' "Fcitx security evidence with password composition" "refused"
    else
        printf '    %-62s %s\n' "Fcitx security evidence with password composition" "ACCEPTED"; fail=$((fail+1))
    fi
    checked=$((checked+1))
    echo
    [ "$fail" -eq 0 ] && { echo "$checked checks: every check refuses what it exists to refuse."; exit 0; }
    echo "$fail of $checked wrong."; exit 1
fi

# ═══ boot ═══════════════════════════════════════════════════════════
[ "$MODE" = boot ] || { sed -n '2,60p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
P="${1:-}"; [ -n "$P" ] || { echo "usage: $0 boot <profile> [evidence-dir]"; exit 2; }
EV="${2:-$ROOT/out/profiletest-$P}"
IBUS_FUNCTIONAL=${PROFILETEST_IBUS_FUNCTIONAL:-0}
case "$IBUS_FUNCTIONAL" in 0|1) ;; *) echo "PROFILETEST_IBUS_FUNCTIONAL must be 0 or 1"; exit 2 ;; esac
IBUS_X11_PROBE=${PROFILETEST_IBUS_X11_PROBE:-0}
case "$IBUS_X11_PROBE" in 0|1) ;; *) echo "PROFILETEST_IBUS_X11_PROBE must be 0 or 1"; exit 2 ;; esac
FCITX_CLIENT_PROBE=${PROFILETEST_FCITX_CLIENT_PROBE:-0}
case "$FCITX_CLIENT_PROBE" in 0|1) ;; *) echo "PROFILETEST_FCITX_CLIENT_PROBE must be 0 or 1"; exit 2 ;; esac
FCITX_PRODUCTION=${PROFILETEST_FCITX_PRODUCTION:-0}
case "$FCITX_PRODUCTION" in 0|1) ;; *) echo "PROFILETEST_FCITX_PRODUCTION must be 0 or 1"; exit 2 ;; esac
FCITX_SECURITY_PROBE=${PROFILETEST_FCITX_SECURITY_PROBE:-0}
case "$FCITX_SECURITY_PROBE" in 0|1) ;; *) echo "PROFILETEST_FCITX_SECURITY_PROBE must be 0 or 1"; exit 2 ;; esac
if [ "$IBUS_FUNCTIONAL" = 1 ] && [ "$P" != multilingual ]; then
    echo "PROFILETEST_IBUS_FUNCTIONAL=1 is only valid for the multilingual profile"
    exit 2
fi
if [ "$FCITX_CLIENT_PROBE" = 1 ] && { [ "$IBUS_X11_PROBE" = 1 ] || [ "$IBUS_FUNCTIONAL" = 1 ]; }; then
    echo "PROFILETEST_FCITX_CLIENT_PROBE cannot be combined with an IBus functional or X11 run"
    exit 2
fi
if [ "$FCITX_PRODUCTION" = 1 ] && { [ "$FCITX_CLIENT_PROBE" = 1 ] || \
     [ "$IBUS_X11_PROBE" = 1 ] || \
     [ "$IBUS_FUNCTIONAL" = 1 ]; }; then
    echo "PROFILETEST_FCITX_PRODUCTION cannot be combined with another input-method probe"
    exit 2
fi
if [ "$IBUS_X11_PROBE" = 1 ] && { [ "$IBUS_FUNCTIONAL" != 1 ] || [ "$P" != multilingual ]; }; then
    echo "PROFILETEST_IBUS_X11_PROBE=1 requires the multilingual functional run"
    exit 2
fi
if [ "$FCITX_CLIENT_PROBE" = 1 ] && [ "$P" != multilingual ]; then
    echo "PROFILETEST_FCITX_CLIENT_PROBE=1 is only valid for multilingual"
    exit 2
fi
if [ "$FCITX_PRODUCTION" = 1 ] && [ "$P" != multilingual ]; then
    echo "PROFILETEST_FCITX_PRODUCTION=1 is only valid for multilingual"
    exit 2
fi
if [ "$FCITX_SECURITY_PROBE" = 1 ] && \
   [ "$FCITX_CLIENT_PROBE" != 1 ] && [ "$FCITX_PRODUCTION" != 1 ]; then
    echo "PROFILETEST_FCITX_SECURITY_PROBE=1 requires a Fcitx client or production probe"
    exit 2
fi
INPUT_FUNCTIONAL=$IBUS_FUNCTIONAL
[ "$FCITX_CLIENT_PROBE" = 1 ] && INPUT_FUNCTIONAL=1
[ "$FCITX_PRODUCTION" = 1 ] && INPUT_FUNCTIONAL=1
for t in qemu-system-x86_64 sgdisk python3 losetup mount; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ "$(id -u)" = 0 ] || { echo "boot needs root (it loop-mounts the image)"; exit 2; }
CODE=/usr/share/OVMF/OVMF_CODE_4M.fd; VARS=/usr/share/OVMF/OVMF_VARS_4M.fd
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF"; exit 2; }
IMG="out/auros-$P.img"
if [ ! -f "$IMG" ] && [ -f "$IMG.zst" ]; then IMG="$IMG.zst"; fi
[ -f "$IMG" ] || { echo "no image for $P: run sudo ./build/all $P --keep=raw"; exit 2; }
if [ "$FCITX_SECURITY_PROBE" = 1 ]; then
    TIMEOUT="${PROFILETEST_TIMEOUT:-2400}"
elif [ "$INPUT_FUNCTIONAL" = 1 ]; then
    TIMEOUT="${PROFILETEST_TIMEOUT:-1800}"
else
    TIMEOUT="${PROFILETEST_TIMEOUT:-900}"
fi
if [ -n "${PROFILETEST_MEM:-}" ]; then MEM=$PROFILETEST_MEM
elif [ "$P" = revive ]; then MEM=2048; else MEM=4096; fi

mkdir -p "$EV"
T=$(mktemp -d "${TMPDIR:-/tmp}/profiletest.XXXXXX")
QP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT

echo
echo "Does the $P image start up to its own desktop?"
DISK="$T/disk.img"
case "$IMG" in
  *.zst) zstd -q -d --long=27 --sparse "$IMG" -o "$DISK" || { echo "could not unpack $IMG"; exit 2; } ;;
  *)     cp --sparse=always "$IMG" "$DISK" ;;
esac
RAW_BYTES=$(stat -c %s "$DISK")

set -- $(sgdisk -p "$DISK" 2>/dev/null | awk '$NF=="AUROS-ROOT" {print $1, $2, $3}')
[ -n "${2:-}" ] || { echo "  the image has no AUROS-ROOT partition"; exit 1; }
RS=$2; RE=$3
LP=$(losetup --find --show -o $((RS * 512)) --sizelimit $(((RE - RS + 1) * 512)) "$DISK")
mkdir -p "$T/m"; mount "$LP" "$T/m" || { echo "  cannot mount its root"; exit 1; }
M="$T/m"

# ── what is in it, read off the disk ────────────────────────────────
echo
echo "  what the image holds"
installed() { # name -> 0 when dpkg says installed
    awk -v n="$1" 'BEGIN{RS=""} $0 ~ "(^|\n)Package: "n"\n" && $0 ~ /\nStatus: install ok installed/ {f=1} END{exit !f}' \
        "$M/var/lib/dpkg/status"
}
want=$(pval "$P" packages_apps; printf ' '; pval "$P" packages_extra)
miss=""; for n in $want; do installed "$n" || miss="$miss $n"; done
[ -z "$miss" ] && ok "every program the profile asks for is installed" \
    || bad "every program the profile asks for is installed" "missing:$miss"
there=""; for n in $(pval "$P" packages_exclude); do installed "$n" && there="$there $n"; done
[ -z "$there" ] && ok "nothing it excludes is there" \
    || bad "nothing it excludes is there" "still installed:$there"
grep -q "^profile *= *$P\$" "$M/etc/auros/policy.conf" 2>/dev/null \
    && ok "its policy file is this profile's" \
    || bad "its policy file is this profile's" "$(grep '^profile' "$M/etc/auros/policy.conf" 2>/dev/null)"
for k in kiosk_mode allow_user_install allow_tty; do
    v=$(pval "$P" $k); case "$v" in no|false|0|off|n) v=no ;; '') v=x ;; *) v=yes ;; esac
    [ "$v" = x ] && continue
    [ "$k" = kiosk_mode ] || [ "$v" = yes ] || [ "$v" = no ] || continue
    got=$(sed -n "s/^$k *= *//p" "$M/etc/auros/policy.conf" | tr -d ' ')
    [ "$got" = "$v" ] && ok "policy: $k = $v" || bad "policy: $k = $v" "the image says '$got'"
done
bn=$(pval "$P" brand_name)
grep -qx "NAME=\"$bn\"" "$M/etc/os-release" 2>/dev/null \
    && ok "it calls itself \"$bn\"" \
    || bad "it calls itself \"$bn\"" "$(grep '^NAME=' "$M/etc/os-release" 2>/dev/null)"
bw=$(grep '^browser=' "$M/etc/auros/build-warnings" 2>/dev/null | tail -1)
case "$bw" in
  ''|browser=MISSING|*'(fallback;'*) bad "its browser is the one the profile asks for" "${bw:-no browser= record}" ;;
  *) ok "its browser is the one the profile asks for (${bw#browser=})" ;;
esac
installed snapd && bad "there is no snapd" || ok "there is no snapd"
ROOT_USED_MB=$(df -BM --output=used "$M" | tail -1 | tr -dc 0-9)
{
    echo "profile=$P"
    echo "image_bytes=$RAW_BYTES"
    echo "root_used_mb=$ROOT_USED_MB"
    echo "packages=$(grep -c '^Status: install ok installed' "$M/var/lib/dpkg/status")"
    sed 's/^/build-warning: /' "$M/etc/auros/build-warnings" 2>/dev/null
} > "$EV/$P-image.txt"
note_sz="image $(awk -v b="$RAW_BYTES" 'BEGIN{printf "%.2f GiB", b/1073741824}'), root holds ${ROOT_USED_MB} MB"
echo "    $note_sz"

# ── the probe: NOT PART OF AUROS ────────────────────────────────────
install -d "$M/usr/lib/auros-profiletest"
cat > "$M/usr/lib/auros-profiletest/probe.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. tools/profiletest.sh puts this into a scratch copy
# of an image, and nothing else ever does. It reports, over the serial
# port, what the running system is, once its desktop is up.
S=/dev/ttyS0
# A newline first: the kernel and systemd write to this console too, and
# a report line must start a line of its own to be found.
say() { printf '\nPROFILETEST %s\n' "$*" > "$S"; }
say "probe started"
i=0
while [ ! -d /run/auros ] && [ "$i" -lt 600 ]; do sleep 1; i=$((i+1)); done
[ -d /run/auros ] || { say "never-up after ${i}s: $(systemctl is-active aurshell.service)"; exit 0; }
say "up $(cut -d. -f1 /proc/uptime)"
# Let the start-up FINISH (systemd-analyze answers only once it has),
# then a little more for the desktop's own first minute, before asking
# what failed and how much memory it took.
i=0; until systemd-analyze time >/dev/null 2>&1 || [ "$i" -ge 180 ]; do sleep 1; i=$((i+1)); done
sleep 30
say "aurshell=$(systemctl is-active aurshell.service)"
say "shell=$(readlink /etc/auros/shell/active.shell)"
say "theme=$(cat /etc/auros/theme 2>/dev/null)"
say "user=$(stat -c %U /run/auros)"
say "lang=$(grep '^LANG=' /etc/default/locale)"
say "locales=$(locale -a 2>/dev/null | tr '\n' ' ')"
say "browser=$(command -v firefox || command -v epiphany-browser || true)"
say "failed=$(systemctl --failed --plain --no-legend 2>/dev/null | awk '{print $1}' | tr '\n' ' ' | sed 's/ $//')"
say "swap=$(swapon --show=NAME,TYPE,SIZE --noheadings 2>/dev/null | tr '\n' ';')"
say "earlyoom=$(systemctl is-active earlyoom.service 2>/dev/null)"
say "tty2=$(systemctl is-enabled getty@tty2.service 2>/dev/null)"
say "boot=$(systemd-analyze time 2>&1 | head -1)"
say "jobs=$(systemctl list-jobs --no-legend 2>/dev/null | awk '{print $2}' | tr '\n' ' ')"
say "mem=$(free -m | awk '/^Mem:/ {print "used " $3 " MiB of " $2}')"
say "rootdf=$(df -BM --output=used,size / | tail -1)"
say "kernel=$(uname -r)"
if grep -Eq '^profile[[:space:]]*=[[:space:]]*multilingual$' /etc/auros/policy.conf 2>/dev/null; then
    desktop_user=$(stat -c %U /run/auros)
    desktop_uid=$(id -u "$desktop_user" 2>/dev/null || true)
    as_desktop() {
        timeout 8 runuser -u "$desktop_user" -- env \
            XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" "$@"
    }
    user_env=$(as_desktop systemctl --user show-environment 2>/dev/null || true)
    env_value() { printf '%s\n' "$user_env" | sed -n "s/^$1=//p" | tail -1; }
    if { [ -x /usr/lib/auros-profiletest/fcitx-functional.sh ] && \
         [ ! -f /usr/lib/auros-profiletest/fcitx-production.enabled ]; } || \
       [ -x /usr/lib/auros-profiletest/ibus-functional.sh ]; then
        ibus_unit=$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null || true)
        ibus_gtk=$(env_value GTK_IM_MODULE)
        ibus_qt=$(env_value QT_IM_MODULE)
        ibus_xmodifiers=$(env_value XMODIFIERS)
        ibus_wayland=$(env_value WAYLAND_DISPLAY)
        ibus_display=$(env_value DISPLAY)
        ibus_engines=$(as_desktop env DISPLAY="$ibus_display" WAYLAND_DISPLAY="$ibus_wayland" \
            GTK_IM_MODULE="$ibus_gtk" QT_IM_MODULE="$ibus_qt" XMODIFIERS="$ibus_xmodifiers" \
            ibus list-engine --name-only 2>/dev/null || true)
        if printf '%s\n' "$ibus_engines" | grep -Fqx pinyin; then
            ibus_pinyin=yes
        else
            ibus_pinyin=no
        fi
        say "ibus_unit=$ibus_unit"
        say "ibus_gtk=$ibus_gtk"
        say "ibus_qt=$ibus_qt"
        say "ibus_xmodifiers=$ibus_xmodifiers"
        say "ibus_wayland=$ibus_wayland"
        say "ibus_display=$ibus_display"
        say "ibus_pinyin=$ibus_pinyin"
    else
        fcitx_unit=$(as_desktop systemctl --user is-active auros-fcitx.service 2>/dev/null || true)
        ibus_unit=$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null || true)
        fcitx_pid=$(as_desktop systemctl --user show -p MainPID --value auros-fcitx.service 2>/dev/null || true)
        fcitx_identity=missing; fcitx_owner=missing; fcitx_display=unknown; fcitx_wayland=unknown
        case "$fcitx_pid" in
          ''|0|*[!0-9]*) ;;
          *)
            if [ "$(readlink "/proc/$fcitx_pid/exe" 2>/dev/null)" = /usr/bin/fcitx5 ] && \
               [ "$(tr '\0' ' ' < "/proc/$fcitx_pid/cmdline" 2>/dev/null)" = '/usr/bin/fcitx5 -D ' ]; then
                fcitx_identity=exact
            else
                fcitx_identity=wrong
            fi
            owner=$(as_desktop busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
                org.freedesktop.DBus GetConnectionUnixProcessID s org.fcitx.Fcitx5 2>/dev/null \
                | awk '$1=="u" && $2 ~ /^[0-9]+$/ {print $2}')
            [ "$owner" = "$fcitx_pid" ] && fcitx_owner=exact || fcitx_owner=wrong
            proc_env=$(tr '\0' '\n' < "/proc/$fcitx_pid/environ" 2>/dev/null || true)
            printf '%s\n' "$proc_env" | grep -q '^DISPLAY=' \
                && fcitx_display=present || fcitx_display=absent
            printf '%s\n' "$proc_env" | grep -q '^WAYLAND_DISPLAY=' \
                && fcitx_wayland=present || fcitx_wayland=absent
            ;;
        esac
        if [ "$(as_desktop fcitx5-remote -m pinyin 2>/dev/null || true)" = pinyin ]; then
            fcitx_pinyin=yes
        else
            fcitx_pinyin=no
        fi
        say "fcitx_unit=$fcitx_unit"
        say "ibus_unit=$ibus_unit"
        say "fcitx_gtk=$(env_value GTK_IM_MODULE)"
        say "fcitx_qt=$(env_value QT_IM_MODULE)"
        say "fcitx_xmodifiers=$(env_value XMODIFIERS)"
        say "fcitx_identity=$fcitx_identity"
        say "fcitx_owner=$fcitx_owner"
        say "fcitx_display=$fcitx_display"
        say "fcitx_wayland=$fcitx_wayland"
        say "fcitx_pinyin=$fcitx_pinyin"
    fi
fi
if [ -x /usr/lib/auros-profiletest/fcitx-functional.sh ]; then
    exec /usr/lib/auros-profiletest/fcitx-functional.sh
fi
if [ -x /usr/lib/auros-profiletest/ibus-functional.sh ]; then
    exec /usr/lib/auros-profiletest/ibus-functional.sh
fi
journalctl -b -p err --no-pager -o cat 2>/dev/null | tail -20 | sed 's/^/PROFILETEST journal: /' > "$S"
say "done"
EOS
chmod 755 "$M/usr/lib/auros-profiletest/probe.sh"
if [ "$FCITX_CLIENT_PROBE" = 1 ] || [ "$FCITX_PRODUCTION" = 1 ]; then
    install -m 755 "$ROOT/tools/profiletest-fcitx-guest.sh" \
        "$M/usr/lib/auros-profiletest/fcitx-functional.sh"
    install -m 755 "$ROOT/tools/fcitx_wayland_trace.py" \
        "$M/usr/lib/auros-profiletest/fcitx_wayland_trace.py"
    if [ "$FCITX_PRODUCTION" = 1 ]; then
        : > "$M/usr/lib/auros-profiletest/fcitx-production.enabled"
    fi
    if [ "$FCITX_SECURITY_PROBE" = 1 ]; then
        install -m 755 "$ROOT/tools/fcitx_connection_probe.py" \
            "$M/usr/lib/auros-profiletest/fcitx_connection_probe.py"
        install -m 755 "$ROOT/tools/fcitx_password_server.py" \
            "$M/usr/lib/auros-profiletest/fcitx_password_server.py"
        : > "$M/usr/lib/auros-profiletest/fcitx-security.enabled"
    fi
elif [ "$IBUS_FUNCTIONAL" = 1 ]; then
    install -m 755 "$ROOT/tools/profiletest-ibus-guest.sh" \
        "$M/usr/lib/auros-profiletest/ibus-functional.sh"
    if [ "$IBUS_X11_PROBE" = 1 ]; then
        install -m 755 "$ROOT/tools/x11_xres_probe.py" \
            "$M/usr/lib/auros-profiletest/x11_xres_probe.py"
    fi
fi
if [ "$INPUT_FUNCTIONAL" = 1 ]; then
    # If this scratch OVMF happens to contain an AurOS boot entry, do not let
    # the install hand-over question cover the applications under test.
    install -d "$M/var/lib/auros"
    printf '%s\n' 'profiletest scratch image: conversion already confirmed' \
        > "$M/var/lib/auros/converted.confirmed"
fi
cat > "$M/etc/systemd/system/auros-profiletest.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: tools/profiletest.sh's probe
# Type=simple and no ordering: the start-up must not wait for the probe
# (a oneshot wanted by multi-user.target holds the target, and the boot
# never "finishes" while it runs, so systemd-analyze has no time to
# give), and After=multi-user.target on a unit that target wants is an
# ordering cycle systemd breaks by dropping a job.
[Service]
Type=simple
ExecStart=/usr/lib/auros-profiletest/probe.sh
[Install]
WantedBy=multi-user.target
EOU
ln -sf /etc/systemd/system/auros-profiletest.service \
    "$M/etc/systemd/system/multi-user.target.wants/auros-profiletest.service"
sync; umount "$M"; losetup -d "$LP"; LP=""

# ── start it ────────────────────────────────────────────────────────
accel=tcg; [ -w /dev/kvm ] && accel=kvm
cp "$VARS" "$T/vars.fd"
SER="$EV/$P-serial.txt"; : > "$SER"
Q="$T/qmp.sock"
VIRTFS_ARGS=""
if [ "$INPUT_FUNCTIONAL" = 1 ]; then
    CHAN="$T/host"; mkdir -p "$CHAN"; echo 0 > "$CHAN/acks"
    VIRTFS_ARGS="-virtfs local,path=$CHAN,mount_tag=profiletest,security_model=none,id=profiletest"
fi
# VIRTFS_ARGS is either empty or one test-only QEMU device whose path is the
# mktemp directory above (and therefore contains no whitespace).
# shellcheck disable=SC2086
qemu-system-x86_64 -machine q35,accel=$accel -cpu max -m "$MEM" -smp 2 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
    -drive if=pflash,format=raw,unit=1,file="$T/vars.fd" \
    -drive file="$DISK",format=raw,if=none,id=d0 \
    -device virtio-blk-pci,drive=d0,bootindex=0 \
    -device VGA,edid=on,xres=1280,yres=800 -display none \
    -device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 -device usb-kbd,bus=xhci.0 \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    $VIRTFS_ARGS \
    -serial file:"$SER" -qmp unix:"$Q",server=on,wait=off \
    >"$T/qemu.log" 2>&1 &
QP=$!
echo
echo "  started ($accel, ${MEM} MiB, 1280x800); waiting for its desktop"
shot() { # name -> sets SHOT_STAT
    rm -f "$T/s.ppm"
    if qmp "$Q" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$T/s.ppm\"}}" >/dev/null 2>&1 &&
       [ -s "$T/s.ppm" ]; then
        SHOT_STAT=$(shot_py "$T/s.ppm" "$EV/$1.png")
    else
        SHOT_STAT="no picture"
        return 1
    fi
}
send_qcode() { # one physical QMP qcode
    qmp "$Q" "{\"execute\":\"send-key\",\"arguments\":{\"keys\":[{\"type\":\"qcode\",\"data\":\"$1\"}],\"hold-time\":60}}"
}
send_chord() { # two simultaneous physical QMP qcodes
    qmp "$Q" "{\"execute\":\"send-key\",\"arguments\":{\"keys\":[{\"type\":\"qcode\",\"data\":\"$1\"},{\"type\":\"qcode\",\"data\":\"$2\"}],\"hold-time\":60}}"
}
screen_changed() { # before.ppm after.ppm -- generic 3% screen change
    python3 "$ROOT/tools/fcitx_marker_visibility.py" --generic "$1" "$2"
}
marker_changed() { # before.ppm after.ppm evidence -- fixed staircase marker
    python3 "$ROOT/tools/fcitx_marker_visibility.py" "$1" "$2" "$3"
}

candidate_changed() { # before.ppm after.ppm -- at least 0.25% changed
    python3 - "$1" "$2" <<'EOPY'
import sys
def ppm(path):
    d=open(path,'rb').read(); parts=[]; i=0
    while len(parts)<4:
        while d[i:i+1].isspace(): i+=1
        if d[i:i+1]==b'#':
            while d[i:i+1] not in (b'\n',b''): i+=1
            continue
        j=i
        while not d[j:j+1].isspace(): j+=1
        parts.append(d[i:j]); i=j
    i+=1
    return int(parts[1]),int(parts[2]),d[i:]
w,h,a=ppm(sys.argv[1]); w2,h2,b=ppm(sys.argv[2])
if (w,h)!=(w2,h2): raise SystemExit(1)
changed=total=0
for y in range(0,h,4):
    for x in range(0,w,4):
        p=(y*w+x)*3; total+=1
        if sum(abs(a[p+k]-b[p+k]) for k in range(3))>24: changed+=1
if changed*400 < total: raise SystemExit(1)
print("changed_samples=%d total_samples=%d" % (changed,total))
EOPY
}
screen_has_form() { # ppm -- print centre of the test page's unique background
    python3 - "$1" <<'EOPY'
import sys
d=open(sys.argv[1],'rb').read(); parts=[]; i=0
while len(parts)<4:
    while d[i:i+1].isspace(): i+=1
    j=i
    while not d[j:j+1].isspace(): j+=1
    parts.append(d[i:j]); i=j
i+=1
w,h=int(parts[1]),int(parts[2]); px=d[i:]; target=bytes((1,222,173))
count=0; xs=[]; ys=[]
for y in range(0,h,4):
    for x in range(0,w,4):
        p=(y*w+x)*3
        if px[p:p+3]==target: count+=1; xs.append(x); ys.append(y)
if count<1000: raise SystemExit(1)
print((min(xs)+max(xs))//2, (min(ys)+max(ys))//2)
EOPY
}
click_at() { # screen x y
    ax=$(( $1 * 32767 / 1280 )); ay=$(( $2 * 32767 / 800 ))
    qmp "$Q" \
      "{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":\"abs\",\"data\":{\"axis\":\"x\",\"value\":$ax}},{\"type\":\"abs\",\"data\":{\"axis\":\"y\",\"value\":$ay}}]}}" || return 1
    sleep 0.15
    qmp "$Q" \
      '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}' || return 1
    sleep 0.08
    qmp "$Q" \
      '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
}
click_center() { click_at 640 380; }
ibus_host_action() {
    [ "$INPUT_FUNCTIONAL" = 1 ] || return 0
    if [ "$FCITX_CLIENT_PROBE" = 1 ] || [ "$FCITX_PRODUCTION" = 1 ]; then
        action_marker=fcitx-do
    else
        action_marker=ibus-do
    fi
    line=$(tr -d '\r' < "$SER" | sed -n "s/^PROFILETEST $action_marker=//p" | tail -1)
    [ -n "$line" ] || return 0
    set -- $line
    id=${1:-0}; verb=${2:-}; shift 2 || return 0
    case "$id" in *[!0-9]*|'') return 0 ;; esac
    ack=$(cat "$CHAN/acks" 2>/dev/null || echo 0)
    [ "$id" -gt "$ack" ] || return 0
    if [ -f "$CHAN/failed-$id" ]; then
        printf '%s\n' "$id" > "$CHAN/.acks" && mv "$CHAN/.acks" "$CHAN/acks"
        return
    fi
    action_ok=0
    case "$verb" in
      type-pinyin)
        # The multilingual profile uses French AZERTY. Physical Q is logical
        # A, so these real key presses type the Pinyin spelling "nihao".
        for key in n i h q o; do
            send_qcode "$key" >/dev/null 2>&1 || { action_ok=1; break; }
            sleep 0.12
        done
        ;;
      key)
        case "${1:-}" in spc|ret|esc|backspace) send_qcode "$1" >/dev/null 2>&1 || action_ok=1 ;;
            *) action_ok=1 ;; esac
        ;;
      chord)
        if [ "${1:-}" = ctrl ] && { [ "${2:-}" = s ] || [ "${2:-}" = q ]; }; then
            send_chord ctrl "$2" >/dev/null 2>&1 || action_ok=1
        else
            action_ok=1
        fi
        ;;
      click-center)
        click_center >/dev/null 2>&1 || action_ok=1
        ;;
      click-form)
        if read -r form_x form_y < "$T/form-point" 2>/dev/null; then
            click_at "$form_x" "$form_y" >/dev/null 2>&1 || action_ok=1
        else
            action_ok=1
        fi
        ;;
      click-change)
        case "${1:-}" in ''|*[!A-Za-z0-9._-]*) action_ok=1 ;;
          *)
            if read -r change_x change_y < "$T/change-point-$1" 2>/dev/null; then
                click_at "$change_x" "$change_y" >/dev/null 2>&1 || action_ok=1
            else
                action_ok=1
            fi
            ;;
        esac
        ;;
      baseline)
        case "${1:-}" in ''|*[!A-Za-z0-9._-]*) action_ok=1 ;;
          *) qmp "$Q" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$T/baseline-$1.ppm\"}}" \
                 >/dev/null 2>&1 || action_ok=1 ;; esac
        ;;
      wait-change)
        case "${1:-}" in ''|*[!A-Za-z0-9._-]*) action_ok=1 ;;
          *)
            before="$T/baseline-$1.ppm"; changed=0; tries=0
            after="$T/change-$id.ppm"; retain=0
            case "$1" in fcitx-round*-mousepad-b) retain=1; visibility="$EV/$1-visibility.txt";;
              *) visibility="$T/change-visibility-$id.txt";; esac
            if [ "$retain" = 1 ]; then
                [ -s "$before" ] && shot_py "$before" "$EV/$1-before.png" >/dev/null \
                    || action_ok=1
            fi
            while [ "$tries" -lt 60 ] && [ -s "$before" ]; do
                qmp "$Q" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$after\"}}" \
                    >/dev/null 2>&1 || break
                if { [ "$retain" = 1 ] && marker_changed "$before" "$after" "$visibility" \
                     || [ "$retain" = 0 ] && screen_changed "$before" "$after"; } \
                    > "$T/change-point-$1"; then
                    changed=1; break
                fi
                sleep 1; tries=$((tries + 1))
            done
            if [ "$retain" = 1 ]; then
                [ -s "$after" ] && shot_py "$after" "$EV/$1-after.png" >/dev/null \
                    || action_ok=1
            fi
            [ "$changed" = 1 ] || action_ok=1
            ;;
        esac
        ;;
      wait-form)
        form_seen=0; tries=0
        while [ "$tries" -lt 60 ]; do
            qmp "$Q" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$T/form-$id.ppm\"}}" \
                >/dev/null 2>&1 || break
            if screen_has_form "$T/form-$id.ppm" > "$T/form-point"; then form_seen=1; break; fi
            sleep 1; tries=$((tries + 1))
        done
        [ "$form_seen" = 1 ] || action_ok=1
        ;;
      shot)
        case "${1:-}" in ''|*[!A-Za-z0-9._-]*) action_ok=1 ;;
            *) shot "$1" || action_ok=1 ;; esac
        ;;
      form-shot)
        case "${1:-}" in ''|*[!A-Za-z0-9._-]*) action_ok=1 ;;
          *)
            form_shot="$T/form-shot-$id.ppm"
            qmp "$Q" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$form_shot\"}}" \
                >/dev/null 2>&1 \
                && screen_has_form "$form_shot" >/dev/null \
                && shot_py "$form_shot" "$EV/$1.png" >/dev/null \
                || action_ok=1
            ;;
        esac
        ;;
      candidate-shot)
        case "${1:-}:${2:-}" in *[!A-Za-z0-9._:-]*|:|*:) action_ok=1 ;;
          *)
            before="$T/baseline-$1.ppm"; after="$T/candidate-$id.ppm"
            qmp "$Q" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$after\"}}" \
                >/dev/null 2>&1 \
                && candidate_changed "$before" "$after" > "$EV/$2-change.txt" \
                && shot_py "$after" "$EV/$2.png" >/dev/null \
                || action_ok=1
            ;;
        esac
        ;;
      *) action_ok=1 ;;
    esac
    if [ "$action_ok" = 0 ]; then
        printf '%s\n' "$id" > "$CHAN/.acks" && mv "$CHAN/.acks" "$CHAN/acks"
    else
        if : > "$CHAN/failed-$id"; then
            printf '%s\n' "$id" > "$CHAN/.acks" && mv "$CHAN/.acks" "$CHAN/acks"
        fi
    fi
}

T0=$(date +%s); up=""
while kill -0 "$QP" 2>/dev/null; do
    el=$(( $(date +%s) - T0 ))
    [ "$el" -ge "$TIMEOUT" ] && break
    grep -aq '^PROFILETEST up' "$SER" && [ -z "$up" ] && up=$el
    ibus_host_action
    grep -aq '^PROFILETEST done\|^PROFILETEST never-up' "$SER" && break
    sleep 2
done
if kill -0 "$QP" 2>/dev/null; then
    shot "$P"
    S1=$SHOT_STAT
else
    S1="the machine had stopped"
fi
tr -d '\r' < "$SER" | grep -a '^PROFILETEST' > "$EV/$P-report.txt"
[ -n "$QP" ] && kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
if [ "$FCITX_CLIENT_PROBE" = 1 ] || [ "$FCITX_PRODUCTION" = 1 ]; then
    sync
    LP=$(losetup --find --show -o $((RS * 512)) --sizelimit $(((RE - RS + 1) * 512)) "$DISK")
    mount "$LP" "$T/m" || { echo "  cannot remount Fcitx evidence"; exit 1; }
    rm -rf "$EV/fcitx"; mkdir -p "$EV/fcitx"
    cp -a "$T/m/var/lib/auros-profiletest/fcitx/." "$EV/fcitx/" 2>/dev/null || true
    cp "$EV"/fcitx-*-candidate-change.txt "$EV/fcitx/" 2>/dev/null || true
    umount "$T/m"; losetup -d "$LP"; LP=""
elif [ "$IBUS_FUNCTIONAL" = 1 ]; then
    sync
    LP=$(losetup --find --show -o $((RS * 512)) --sizelimit $(((RE - RS + 1) * 512)) "$DISK")
    mount "$LP" "$T/m" || { echo "  cannot remount functional evidence"; exit 1; }
    rm -rf "$EV/ibus"
    mkdir -p "$EV/ibus"
    cp -a "$T/m/var/lib/auros-profiletest/ibus/." "$EV/ibus/" 2>/dev/null || true
    umount "$T/m"; losetup -d "$LP"; LP=""
fi

echo
echo "  what the running system said"
judge_report "$P" "$EV/$P-report.txt"
if [ "$IBUS_FUNCTIONAL" = 1 ]; then
    judge_ibus_functional "$EV/ibus" "$EV"
    if [ "$IBUS_X11_PROBE" = 1 ]; then
        XEV="$EV/ibus/round1/x11-panel"
        judge_ibus_x11_probe "$XEV" "$EV"
    fi
fi
if [ "$FCITX_CLIENT_PROBE" = 1 ]; then
    judge_fcitx_client_probe "$EV/fcitx" "$EV"
    if [ "$FCITX_SECURITY_PROBE" = 1 ]; then
        judge_fcitx_security_probe "$EV/fcitx" "$EV"
    fi
fi
if [ "$FCITX_PRODUCTION" = 1 ]; then
    judge_fcitx_production "$EV/fcitx" "$EV"
    if [ "$FCITX_SECURITY_PROBE" = 1 ]; then
        judge_fcitx_security_probe "$EV/fcitx" "$EV" shipped-fcitx
    fi
fi
drawn_enough "$S1" && ok "the screen shows a desktop ($S1)" \
                   || bad "the screen shows a desktop" "$S1"
{
    echo "desktop_up_after_s=${up:-never}"
    echo "screen=$S1"
    echo "accel=$accel mem_mib=$MEM"
    sed -n 's/^PROFILETEST \(boot\|mem\|rootdf\|kernel\|swap\)=/\1=/p' "$EV/$P-report.txt"
} >> "$EV/$P-image.txt"
sed -n 's/^PROFILETEST \(boot\|mem\)=/    \1: /p' "$EV/$P-report.txt"
echo "    desktop up after ${up:-never} s (host clock, from power-on)"
echo "    evidence: $EV/$P.png, $P-report.txt, $P-serial.txt, $P-image.txt"
echo
[ "$fail" -eq 0 ] && { echo "$checked checks: $P builds what it says and starts to its desktop."; exit 0; }
echo "$fail of $checked wrong."
exit 1
