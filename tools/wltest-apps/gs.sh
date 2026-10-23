#!/bin/sh
# gs.sh -- the store ("Get more programs") on one app's page, on its own session bus.
# Run by tools/wltest-apps.sh through the click mode; APPID names the app, the
# program's Wayland traffic (WAYLAND_DEBUG=client) goes to /out/gs-wayland.log.
# the store insists on a system bus (it talks to PackageKit, polkit, ...), even if most of them are absent here
mkdir -p /run/dbus && rm -f /run/dbus/pid && dbus-daemon --system --fork
flatpak update --appstream >/dev/null 2>&1
exec dbus-run-session -- sh -c "gnome-software --details=${APPID:-org.gnome.Calculator} 2>/out/gs-wayland.log"
