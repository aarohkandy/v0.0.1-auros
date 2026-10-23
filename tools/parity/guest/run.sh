#!/bin/sh
# NOT PART OF AUROS. The checks, run as the desktop's own user, in its session.
. /usr/lib/auros-parity/config
. /usr/lib/auros-parity/lib.sh
say "note guest up, user $(id -un), checks: $CHECKS, options: $OPTS"

export XDG_RUNTIME_DIR=/run/user/$(id -u)
wait_for 180 test -S "$XDG_RUNTIME_DIR/bus" || { result desktop fail "no user session bus after 180 s"; say done; exit 0; }
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
# What the desktop publishes for the programs it starts (WAYLAND_DISPLAY,
# XDG_DATA_DIRS with Flathub's exports, ...): take exactly that.
have_wl() { systemctl --user show-environment | grep -q '^WAYLAND_DISPLAY='; }
wait_for 180 have_wl || { result desktop fail "the desktop never published WAYLAND_DISPLAY"; say done; exit 0; }
eval "$(systemctl --user show-environment | sed -n "s/^\(WAYLAND_DISPLAY\|XDG_DATA_DIRS\|XDG_CURRENT_DESKTOP\|XDG_SESSION_TYPE\)=\(.*\)$/export \1='\2'/p")"

# What aurshell's own launcher (src/aurwl/aurwl.c aurwl_spawn) sets for every program it starts:
export GDK_BACKEND=wayland QT_QPA_PLATFORM=wayland SDL_VIDEODRIVER=wayland CLUTTER_BACKEND=wayland MOZ_ENABLE_WAYLAND=1
unset DISPLAY

# firstboot grows the disk before the desktop; do not start measuring before it is done
wait_for 120 test -f /var/lib/auros/firstboot.done

# The first thing she sees: the welcome card ("Let me look first" is the button she would press).
sleep 3; click 620 703; sleep 1; snap welcome-dismissed

for c in $CHECKS; do
    f=$P/checks/$c.sh
    if [ -f "$f" ]; then
        say "note ---- $c"
        if [ "$c" = adhoc ]; then
            timeout 900 sh "$f" 2>&1 | while IFS= read -r l; do say "note | $l"; done
            result adhoc pass "ran"; rc=0
        else
            timeout 1500 sh "$f"; rc=$?
        fi
        [ "$rc" -eq 124 ] && result "$c" fail "the check ran out of time"
    else
        result "$c" fail "no such check"
    fi
done
say done
sleep 5
