#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  updatepolicytest — does everything on a built AurOS keep itself up
#  to date, without asking her and without interrupting her?
#  (docs/security/UPDATES.md is the policy; this is its proof.)
#
#    static ROOTFS   on a forged rootfs (root: it chroots):
#      - the apt policy as apt itself reads it: unattended-upgrades on,
#        AurOS + Ubuntu -updates + Firefox origins allowed, setting
#        conflicts answered in advance, no automatic restart
#      - every apt source in the image is one something updates from:
#        a source nothing allows is software that never gets a fix
#      - Firefox is a package from an allowed origin, not the tarball
#        (which nothing updates)
#      - the base is up to date (forge recorded pending_updates=0)
#      - Flathub programs: the update timer is enabled
#      - the store never offers updates or a "Restart & Install"
#      - fwupd is absent (the decision in UPDATES.md: firmware is not
#        updated from AurOS)
#      - AurOS's own channel: on, or said plainly that it is off
#        (a FAIL with AUROS_RELEASE=1)
#    firefox         in a throwaway ubuntu:24.04 with network: Mozilla's
#      repository set up exactly as build/forge does it, AurOS's apt
#      policy, an old firefox installed -> unattended-upgrades would
#      upgrade it; with --break=origin (53auros-updates without the
#      Mozilla line) it would not, and the test fails.
#
#  Exit 0 all passed, 1 a check failed, 2 it could not run.
# ═══════════════════════════════════════════════════════════════════
set -u
export LC_ALL=C
ROOT=$(cd "$(dirname "$0")/.." && pwd)

fail=0; checked=0
ok()   { checked=$((checked+1)); printf '    %-66s ok\n' "$1"; }
bad()  { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s FAIL\n' "$1"
         shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
note() { printf '    %-66s NOTE\n' "$1"; shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
need() { echo "updatepolicytest: $*" >&2; exit 2; }
done_() {
    echo
    if [ "$fail" -eq 0 ]; then echo "updatepolicytest: $checked checks, all passed"; exit 0; fi
    echo "updatepolicytest: $fail of $checked checks FAILED"; exit 1
}

static() {
    # Absolute: it is written into an apt source's Signed-By below.
    local R; R=$(cd "${1:?ROOTFS}" 2>/dev/null && pwd) || need "no rootfs at $1"
    # An enabled unit is a symlink whose target is absolute *inside the
    # image* (/etc/systemd/system/x.timer): seen from outside it dangles
    # unless the host happens to have the same file, so -e is not the
    # question; -L is (as build/forge's enable_unit asks it).
    enabled() { [ -L "$1" ] || [ -e "$1" ]; }
    [ "$(id -u)" = 0 ] || need "static needs root (it asks apt inside the image)"
    [ -x "$R/usr/bin/apt-config" ] || need "no apt in $R"
    inr() { chroot "$R" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin LC_ALL=C "$@"; }
    local cfg; cfg=$(inr apt-config dump 2>/dev/null) || need "apt-config dump failed in $R"
    has() { printf '%s\n' "$cfg" | grep -qF -- "$1"; }

    echo
    echo "== the apt policy, as apt reads it in $R"
    has 'APT::Periodic::Unattended-Upgrade "1";' && has 'APT::Periodic::Update-Package-Lists "1";' &&
        ok "lists fetched and updates installed every day" || bad "lists fetched and updates installed every day"
    [ -x "$R/usr/bin/unattended-upgrade" ] && ok "unattended-upgrades is installed" || bad "unattended-upgrades is installed"
    for o in 'origin=AurOS,label=AurOS,archive=stable' 'origin=Ubuntu,archive=${distro_codename}-updates' \
             'site=packages.mozilla.org' 'origin=LP-PPA-mozillateam'; do
        has "Unattended-Upgrade::Origins-Pattern:: \"$o\";" && ok "allowed: $o" || bad "allowed: $o"
    done
    printf '%s\n' "$cfg" | grep -q 'Unattended-Upgrade::Allowed-Origins:: "${distro_id}:${distro_codename}-security";' &&
        ok "allowed: Ubuntu -security (Ubuntu's own default)" || bad "allowed: Ubuntu -security (Ubuntu's own default)"
    has 'DPkg::Options:: "--force-confold";' && has 'DPkg::Options:: "--force-confdef";' &&
        ok "a changed setting never stops an update" || bad "a changed setting never stops an update"
    if printf '%s\n' "$cfg" | grep -qE '^Unattended-Upgrade::Automatic-Reboot "(true|1|yes)";'; then
        bad "never an automatic restart"
    else ok "never an automatic restart"; fi
    grep -qx 'ExecStartPre=/usr/lib/auros/hold-running-apps' "$R/etc/systemd/system/apt-daily-upgrade.service.d/60-auros-quiet.conf" 2>/dev/null &&
        [ -x "$R/usr/lib/auros/hold-running-apps" ] &&
        ok "a program she has open is not updated under her" || bad "a program she has open is not updated under her"
    for t in apt-daily.timer apt-daily-upgrade.timer; do
        enabled "$R/etc/systemd/system/timers.target.wants/$t" && ok "$t is enabled" || bad "$t is enabled"
    done

    echo
    echo "== every apt source is one something updates from"
    local f u n=0
    for f in "$R"/etc/apt/sources.list "$R"/etc/apt/sources.list.d/*.list "$R"/etc/apt/sources.list.d/*.sources; do
        [ -f "$f" ] || continue
        # deb lines (one-line format) and URIs: lines (deb822).
        for u in $(sed -n -e 's/^[[:space:]]*deb[[:space:]]\+\(\[[^]]*\][[:space:]]\+\)\{0,1\}\([^[:space:]]*\).*/\2/p' \
                          -e 's/^URIs:[[:space:]]*//p' "$f"); do
            n=$((n+1))
            case "$u" in
                http://archive.ubuntu.com/*|http://security.ubuntu.com/*|https://archive.ubuntu.com/*|https://security.ubuntu.com/*|\
                http://*.archive.ubuntu.com/*|http://ports.ubuntu.com/*)
                    ok "Ubuntu (-security, -updates): ${u}" ;;
                https://packages.mozilla.org/*)
                    ok "Mozilla (site=packages.mozilla.org, firefox* only): $u" ;;
                https://ppa.launchpadcontent.net/mozillateam/*)
                    ok "mozillateam PPA (origin=LP-PPA-mozillateam): $u" ;;
                https://github.com/*/releases/download/apt/|https://updates.*/apt/)
                    ok "AurOS's channel: $u" ;;
                *)  bad "nothing updates what comes from $u" "(${f#$R})" ;;
            esac
        done
    done
    [ "$n" -gt 0 ] || bad "the image has apt sources"
    if [ -s "$R/etc/apt/preferences.d/mozilla" ]; then
        awk 'BEGIN{RS=""} /Pin: origin packages.mozilla.org/ && /Pin-Priority: 1000/ {print $2}' "$R/etc/apt/preferences.d/mozilla" |
            grep -qvx 'firefox\*' && bad "Mozilla's repository may supply Firefox only" "$(cat "$R/etc/apt/preferences.d/mozilla")" ||
            ok "Mozilla's repository may supply Firefox only"
    fi

    echo
    echo "== Firefox"
    if [ -e "$R/opt/firefox" ]; then
        bad "Firefox is a package, updated by apt" "/opt/firefox is the tarball: its updater is off and apt does not know it -- it never updates"
    elif inr dpkg-query -W -f='${Status}' firefox 2>/dev/null | grep -q 'ok installed'; then
        if grep -rqsE 'packages\.mozilla\.org|mozillateam' "$R"/etc/apt/sources.list.d/; then
            ok "Firefox is a package, from an origin unattended-upgrades takes"
        else
            bad "Firefox is a package, from an origin unattended-upgrades takes" "firefox is installed, and no Mozilla source is configured"
        fi
    else
        note "no Firefox in this image" "(build-warnings: $(grep '^browser=' "$R/etc/auros/build-warnings" 2>/dev/null))"
    fi

    echo
    echo "== the base"
    local p; p=$(sed -n 's/^pending_updates=//p' "$R/etc/auros/build-warnings" 2>/dev/null | tail -1)
    if [ "$p" = 0 ]; then ok "no package ships with a known fix left out"
    elif [ -z "$p" ]; then bad "no package ships with a known fix left out" "forge recorded no pending_updates= line"
    else bad "no package ships with a known fix left out" "forge recorded pending_updates=$p"; fi

    echo
    echo "== programs from Flathub"
    if [ -x "$R/usr/bin/flatpak" ]; then
        enabled "$R/etc/systemd/system/timers.target.wants/auros-flatpak-update.timer" &&
            ok "auros-flatpak-update.timer is enabled" || bad "auros-flatpak-update.timer is enabled"
        grep -q '^ExecStart=/usr/bin/flatpak update --system --noninteractive' "$R/etc/systemd/system/auros-flatpak-update.service" 2>/dev/null &&
            ok "it updates the system installation, asking nothing" || bad "it updates the system installation, asking nothing"
    else
        note "no flatpak in this image"
    fi

    echo
    echo "== what she sees"
    if [ -x "$R/usr/bin/gnome-software" ]; then
        local k v
        for k in allow-updates download-updates download-updates-notify; do
            v=$(inr env GSETTINGS_BACKEND=memory gsettings get org.gnome.software "$k" 2>&1)
            [ "$v" = false ] && ok "the store: $k is off" || bad "the store: $k is off" "gsettings says: $v"
        done
    fi
    # needrestart restarts services (the network among them) after an
    # update, under her; update-notifier/-manager ask her things.
    for pkg in update-notifier update-manager needrestart fwupd; do
        inr dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q 'ok installed' &&
            bad "$pkg is not installed" || ok "$pkg is not installed"
    done

    echo
    echo "== AurOS's own channel"
    local src=$R/etc/apt/sources.list.d/auros.sources key=$R/usr/share/keyrings/auros-archive-keyring.asc
    if [ -s "$src" ] && [ -s "$key" ] && grep -q '^Signed-By: /usr/share/keyrings/auros-archive-keyring.asc$' "$src"; then
        ok "on: $(sed -n 's/^URIs: //p' "$src"), Signed-By the archive key"
        keycheck "$R" "$src" "$key"
        inr dpkg-query -W -f='${Status}' auros 2>/dev/null | grep -q 'ok installed' &&
            ok "AurOS's own files are packages (auros $(inr dpkg-query -W -f='${Version}' auros))" ||
            bad "AurOS's own files are packages"
    elif [ "${AUROS_RELEASE:-}" = 1 ]; then
        bad "on" "$(grep '^update_channel=' "$R/etc/auros/build-warnings" 2>/dev/null)"
    else
        note "OFF in this image: AurOS's own fixes reach none of its computers" \
             "$(grep '^update_channel=' "$R/etc/auros/build-warnings" 2>/dev/null)"
    fi
    done_
}

# The image's own key file, used by apt the way the image's source uses
# it: a repository signed by another key is refused; the real channel,
# once something is published there, is accepted.
keycheck() { # ROOTFS SOURCE KEY
    local R=$1 src=$2 key=$3 T url
    T=$(mktemp -d); mkdir -p "$T/parts" "$T/lists/partial" "$T/cache/archives/partial" "$T/repo" "$T/d/DEBIAN"
    local O=(-o Dir::Etc::sourcelist=/dev/null -o Dir::Etc::sourceparts="$T/parts"
             -o Dir::State::lists="$T/lists" -o Dir::Cache="$T/cache" -o APT::Sandbox::User=root
             -o Dir::Etc::preferencesparts=/dev/null)
    aptq() { apt-get -q "${O[@]}" "$@"; }
    aptc() { apt-cache "${O[@]}" "$@"; }
    printf 'Package: auros\nVersion: 99\nArchitecture: amd64\nMaintainer: x <x@invalid>\nDescription: x\n x\n' > "$T/d/DEBIAN/control"
    dpkg-deb --root-owner-group --build "$T/d" "$T/repo/auros_99_amd64.deb" >/dev/null
    ( export GNUPGHOME="$T/g"; mkdir -m 700 "$GNUPGHOME"
      gpg --batch --quiet --passphrase '' --quick-gen-key 'not AurOS' ed25519 sign 1d 2>/dev/null
      bash "$ROOT/build/aptchannel" repo "$T/repo" "$T/srv" >/dev/null &&
      bash "$ROOT/build/aptchannel" sign "$T/srv" 'not AurOS' >/dev/null ) || { bad "a throwaway repository could be made"; rm -rf "$T"; return; }
    local port=$((8900 + RANDOM % 90))
    python3 -m http.server "$port" --bind 127.0.0.1 --directory "$T/srv" >/dev/null 2>&1 & local hp=$!
    sleep 1
    sed -e "s#^URIs: .*#URIs: http://127.0.0.1:$port/#" -e "s#^Signed-By: #Signed-By: $R#" "$src" > "$T/parts/auros.sources"
    aptq update > "$T/u1.log" 2>&1
    kill $hp 2>/dev/null
    grep -q NO_PUBKEY "$T/u1.log" && ! grep -q "^Get:.*Packages" "$T/u1.log" &&
        ok "the image's key refuses a repository signed by another key" ||
        bad "the image's key refuses a repository signed by another key" "$(sed 's/^/  | /' "$T/u1.log")"
    url=$(sed -n 's/^URIs: //p' "$src")
    if curl -fsSL -o /dev/null "${url}InRelease" 2>/dev/null; then
        sed "s#^Signed-By: #Signed-By: $R#" "$src" > "$T/parts/auros.sources"
        rm -rf "$T/lists"; mkdir -p "$T/lists/partial"
        aptq update > "$T/u2.log" 2>&1
        ! grep -qE '^(W|E):' "$T/u2.log" && aptc policy auros 2>/dev/null | grep -q 'Candidate: [0-9]' &&
            ok "the image's key accepts the published channel ($url)" ||
            bad "the image's key accepts the published channel" "$(sed 's/^/  | /' "$T/u2.log")"
    else
        note "nothing published at $url yet: acceptance is proven by aptrepo.yml's publish"
    fi
    rm -rf "$T"
}

firefox() {
    local BREAK=${1:-}
    [ "$(id -u)" = 0 ] || need "run as root, in a throwaway container"
    [ -f /.dockerenv ] || [ -f /run/.containerenv ] || need "refusing to run outside a container"
    for t in gpg curl apt-get unattended-upgrade dpkg-deb; do command -v $t >/dev/null || need "needs $t"; done
    local T; T=$(mktemp -d)
    # Exactly build/forge's try_mozilla_apt: key, fingerprint, source, pin.
    local want; want=$(sed -n 's/^MOZILLA_KEY_FPR="\([0-9A-F]*\)"/\1/p' "$ROOT/build/forge")
    install -d -m 0755 /etc/apt/keyrings
    curl -fsSL --max-time 30 https://packages.mozilla.org/apt/repo-signing-key.gpg -o /etc/apt/keyrings/packages.mozilla.org.asc ||
        need "packages.mozilla.org is not reachable"
    local fpr; fpr=$(gpg --show-keys --with-colons /etc/apt/keyrings/packages.mozilla.org.asc 2>/dev/null | awk -F: '$1=="fpr"{print $10; exit}')
    [ "$fpr" = "$want" ] || need "Mozilla's key is $fpr, forge expects $want"
    # The first heredoc writing each file is try_mozilla_apt's (the PPA
    # route writes its own preferences.d/mozilla further down).
    heredoc() { awk -v f="$1" 'p && /^EOL$/ {exit} p {print} index($0, "cat > \"$RFS" f "\" <<") {p=1}' "$ROOT/build/forge"; }
    heredoc /etc/apt/sources.list.d/mozilla.list > /etc/apt/sources.list.d/mozilla.list
    heredoc /etc/apt/preferences.d/mozilla > /etc/apt/preferences.d/mozilla
    grep -q packages.mozilla.org /etc/apt/sources.list.d/mozilla.list && grep -q 'firefox\*' /etc/apt/preferences.d/mozilla ||
        need "could not read Mozilla's source and pin out of build/forge"
    # And forge's pin_no_snap: Ubuntu's own firefox is a snap stub (with
    # an epoch, so it would look newer than any of Mozilla's).
    printf 'Package: firefox*\nPin: release o=Ubuntu\nPin-Priority: -1\n' > /etc/apt/preferences.d/auros-no-snap
    cp "$ROOT/rootfs/etc/apt/apt.conf.d/53auros-updates" /etc/apt/apt.conf.d/
    [ "$BREAK" = origin ] && sed -i '/packages.mozilla.org/d' /etc/apt/apt.conf.d/53auros-updates
    # An old firefox, as a computer has after a while: a stand-in at
    # version 1, so nothing big is installed to prove it.
    mkdir -p "$T/ff/DEBIAN"
    printf 'Package: firefox\nVersion: 100.0\nArchitecture: amd64\nMaintainer: nobody <nobody@invalid>\nDescription: old\n old\n' > "$T/ff/DEBIAN/control"
    dpkg-deb --root-owner-group --build "$T/ff" "$T/ff.deb" >/dev/null && dpkg -i "$T/ff.deb" >/dev/null || need "could not install the stand-in"
    timeout 300 apt-get update -q > "$T/u.log" 2>&1 || { cat "$T/u.log"; need "apt-get update failed"; }
    apt-cache policy firefox > "$T/p.log" 2>&1
    # Firefox open: hold-running-apps (apt-daily-upgrade's ExecStartPre)
    # holds it, and the real unattended-upgrade leaves it alone.
    cp /bin/sleep "$T/firefox"; "$T/firefox" 300 & local ffpid=$!
    sh "$ROOT/rootfs/usr/lib/auros/hold-running-apps"
    unattended-upgrade -v > "$T/uu-open.log" 2>&1
    local open_ver; open_ver=$(dpkg-query -W -f='${Version}' firefox)
    kill "$ffpid" 2>/dev/null; wait "$ffpid" 2>/dev/null
    # Closed: the next run takes it. The real thing, not a dry run: what
    # it installs is the answer.
    sh "$ROOT/rootfs/usr/lib/auros/hold-running-apps"
    unattended-upgrade -v > "$T/uu.log" 2>&1

    echo
    echo "== Firefox from packages.mozilla.org, under AurOS's update policy${BREAK:+   [--break=$BREAK]}"
    local cand now
    cand=$(sed -n 's/^ *Candidate: //p' "$T/p.log")
    case "$cand" in *~build*) ok "apt's candidate is Mozilla's Firefox ($cand)" ;;
        *) bad "apt's candidate is Mozilla's Firefox" "$(sed 's/^/  | /' "$T/p.log")" ;; esac
    [ "$open_ver" = 100.0 ] && ok "while Firefox is open, it is not updated under her" ||
        bad "while Firefox is open, it is not updated under her" "it went to $open_ver"
    now=$(dpkg-query -W -f='${Version}' firefox)
    [ "$now" = "$cand" ] && [ -x /usr/lib/firefox/firefox ] &&
        ok "unattended-upgrades updated it by itself, to $now" ||
        bad "unattended-upgrades updated it by itself" "firefox is now $now (wanted $cand)" "$(tail -15 "$T/uu.log" | sed 's/^/  | /')"
    rm -rf "$T"
    done_
}

case "${1:-}" in
    static)  shift; static "$@" ;;
    keycheck) shift  # ROOTFS: only the key part (a rootfs with just the source and key will do)
              R=$(cd "${1:?ROOTFS}" && pwd) || need "no rootfs"
              keycheck "$R" "$R/etc/apt/sources.list.d/auros.sources" "$R/usr/share/keyrings/auros-archive-keyring.asc"; done_ ;;
    firefox) shift; case "${1:-}" in --break=origin) firefox origin ;; '') firefox ;; *) need "unknown $1" ;; esac ;;
    *) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
