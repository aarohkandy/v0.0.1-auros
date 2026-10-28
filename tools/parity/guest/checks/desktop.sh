# The floor every other check stands on: the desktop is up, the network is up,
# the sound server answers.
. /usr/lib/auros-parity/lib.sh
bad=""
systemctl is-active --quiet aurshell.service || bad="$bad aurshell-not-active"
[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] || [ -S "$WAYLAND_DISPLAY" ] || bad="$bad no-wayland-socket"
wait_for 90 nm-online -q -t 5 || bad="$bad network-not-up"
wait_for 60 wpctl status || bad="$bad no-sound-server"
note "env WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_DATA_DIRS=$XDG_DATA_DIRS"
note "wpctl: $(wpctl status 2>&1 | tr '\n' '|' | cut -c1-600)"
note "video: $(ls /dev/video* 2>&1 | tr '\n' ' ') uptime $(cut -d' ' -f1 /proc/uptime)"
note "modprobe: $(tr '\n' '|' < /var/lib/auros-parity/modprobe.log | cut -c1-700)"
note "failed units: $(systemctl --failed --plain --no-legend | awk '{print $1}' | tr '\n' ' ')"
snap desktop
if [ -z "$bad" ]; then result desktop pass "desktop, network and sound are up"; else result desktop fail "$bad"; fi
