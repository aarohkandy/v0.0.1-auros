#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  AurOS: keep "Get more programs" fresh, the first time it is online
#
#  Run by auros-software.service until it has worked once, then never
#  again (/var/lib/auros/software-ready).
#
#  THE STORE DOES NOT WAIT FOR THIS. The image carries Flathub's
#  catalogue from the day it was built (build/forge stage_flathub), so
#  the store shows programs the first time it opens, online or not, and
#  a search for "VLC" finds VLC. What this does is bring that catalogue
#  up to date, and fetch what the image deliberately does not carry:
#
#    1. Flathub, if the image does not already have it. The build adds
#       it when the builder can reach dl.flathub.org (GitHub's runners
#       can); otherwise it is added here. From the copy the build saved
#       if there is one, else from Flathub itself over https -- the
#       same command Flathub's own setup page gives everybody.
#    2. Flathub's catalogue, fresh. STAMP catalogue-ready.
#    3. apt's lists and the archive's catalogue (apt runs appstream's
#       hook): hundreds of megabytes, out of date the day after the
#       build. STAMP apt-ready.
#
#  EACH STEP HAS ITS OWN STAMP. They used to share one, and a failure of
#  step 3 (a slow mirror, a clock a few hours out) made step 2 run
#  again from the top, for as long as it took. Now a step that worked
#  is never repeated and never held up by one that did not; the first
#  try is quick, and the pause between tries grows to a minute.
#  software-ready is written when both are done, and ends this service.
#
#  When a step fails the reason goes to the journal in the tool's own
#  words (it used to go to /dev/null, and a store that stayed empty for
#  five minutes could not say why).
#
#  Nothing here can hold up the desktop: the unit is Type=simple and
#  waits for the network itself.
# ═══════════════════════════════════════════════════════════════════
set -u
STATE=${AUROS_STATE:-/var/lib/auros}
CATALOGUE=$STATE/catalogue-ready
APT=$STATE/apt-ready
STAMP=$STATE/software-ready
SAVED=${AUROS_FLATHUB_REPO:-/usr/share/auros/flathub.flatpakrepo}
URL=https://dl.flathub.org/repo/flathub.flatpakrepo

say() { printf 'software-ready: %s\n' "$*"; }
# The last two lines of what a tool said, on one line: enough to name the cause.
why() { printf '%s' "$1" | tail -n 2 | tr '\n' ' ' | cut -c1-300; }

online() {
    # NetworkManager's own answer first: it knows when the wifi is up.
    # nm-online exits 0 at once when it is, and waits up to the
    # timeout when it is not.
    if command -v nm-online >/dev/null 2>&1; then
        nm-online -q -t "${AUROS_ONLINE_WAIT:-60}" >/dev/null 2>&1 || return 1
    fi
    return 0
}

have_flathub() {
    flatpak remotes --system --columns=name 2>/dev/null | grep -qx flathub
}

stamp() { mkdir -p "$STATE" && date -Is > "$1"; }

catalogue() {
    if ! command -v flatpak >/dev/null 2>&1; then
        stamp "$CATALOGUE"; return 0                # no Flathub on this build
    fi
    if ! have_flathub; then
        if [ -s "$SAVED" ]; then src=$SAVED; else src=$URL; fi
        if out=$(flatpak remote-add --system --if-not-exists flathub "$src" 2>&1); then
            say "Flathub added (from $src)"
        else
            say "could not add Flathub yet: $(why "$out")"; return 1
        fi
    fi
    if out=$(flatpak update --system --appstream flathub 2>&1); then
        say "Flathub's catalogue fetched"; stamp "$CATALOGUE"; return 0
    fi
    say "Flathub's catalogue: not yet: $(why "$out")"; return 1
}

apt_lists() {
    if ! command -v apt-get >/dev/null 2>&1; then
        stamp "$APT"; return 0
    fi
    if out=$(apt-get -q update 2>&1); then
        say "apt's lists fetched"; stamp "$APT"; return 0
    fi
    say "apt's lists: not yet: $(why "$out")"; return 1
}

delay=${AUROS_FIRST_DELAY:-10}
while :; do
    if [ -e "$CATALOGUE" ] && [ -e "$APT" ]; then
        stamp "$STAMP"
        say "ready"
        exit 0
    fi
    if online; then
        [ -e "$CATALOGUE" ] || catalogue
        [ -e "$APT" ] || apt_lists
        [ -e "$CATALOGUE" ] && [ -e "$APT" ] && continue
    fi
    sleep "$delay"
    delay=$((delay * 2)); [ "$delay" -le 60 ] || delay=60
done
