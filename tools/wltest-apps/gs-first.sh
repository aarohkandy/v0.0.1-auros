#!/bin/sh
# gs-first.sh -- the store ("Get more programs") at its FIRST open, on whatever
# catalogue /fp holds (tools/storeoffline.sh prepares it, and runs this with no
# network at all). Run by tools/wltest-apps.sh as custom:'sh /w/tools/wltest-apps/gs-first.sh'.
# Nothing here refreshes anything: what the store shows is what was shipped.
mkdir -p /run/dbus && rm -f /run/dbus/pid && dbus-daemon --system --fork
exec dbus-run-session -- sh -c 'gnome-software 2>/out/gs-first.log'
