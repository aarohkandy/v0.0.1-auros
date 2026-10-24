#!/bin/bash
# storeoffline-inner.sh -- runs INSIDE a privileged container with NO network
# (tools/storeoffline.sh starts it). The image's own root file system, read
# only, under a throw-away overlay; the image's own aurshell, headless, with
# the image's own GNOME Software on its desktop, as the image's own user.
#   env: OFF SIZE (bytes: the root partition in /img), MODE=first|vlc, SECS
set -eu
R=/merged
mkdir -p /lower /upper /merged /out
LD=$(losetup -f --show -r -o "$OFF" --sizelimit "$SIZE" /img.raw)
mount -o ro,noload "$LD" /lower
mount -t tmpfs -o size=3g tmpfs /upper; mkdir /upper/u /upper/w
mount -t overlay overlay -o lowerdir=/lower,upperdir=/upper/u,workdir=/upper/w "$R"
mount --bind /dev "$R/dev"; mount -t proc proc "$R/proc"; mount -t sysfs sys "$R/sys"
mount -t tmpfs tmpfs "$R/run"; mount -t tmpfs tmpfs "$R/tmp"
mkdir -p "$R/run/dbus" "$R/run/user/1000" "$R/out" "$R/home/auros"
chown 1000:1000 "$R/run/user/1000" "$R/home/auros"; chmod 700 "$R/run/user/1000"
mount --bind /out "$R/out"; chmod 777 /out
echo "network in here: $(ls /sys/class/net | tr '\n' ' ')"
chroot "$R" dbus-daemon --system --fork
# The search provider answers what the Shell would ask when she types "vlc".
# (Run from inside the session the shell starts, so it sees the same bus.)
if [ "$MODE" = vlc ]; then
    APP="sh -c 'gnome-software 2>/out/gs-vlc.log & sleep $((SECS - 12)); gdbus call --session --dest org.gnome.Software --object-path /org/gnome/Software/SearchProvider --method org.gnome.Shell.SearchProvider2.GetInitialResultSet \"[\\\"vlc\\\"]\" > /out/search.txt 2>&1; wait'"
    TYPE="--type vlc"; PNG=/out/vlc.png
else
    APP="sh -c 'gnome-software 2>/out/gs-first.log'"
    TYPE=""; PNG=/out/first.png
fi
chroot "$R" runuser -u auros -- env HOME=/home/auros XDG_RUNTIME_DIR=/run/user/1000 LANG=C.UTF-8 \
    dbus-run-session -- /usr/bin/aurshell --shell /usr/share/auros/shells/taskbar.shell \
    --size 1280 800 --png "$PNG" $TYPE --app-wait "$SECS" --with-app "$APP" 2>&1 | grep -a -v "Could not resolve keysym" | tail -8
