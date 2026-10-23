#!/bin/sh
# polkit.sh -- the remote-control gate under a REAL polkitd (noble's 124,
# duktape), a real system bus, and the desktop's own listener.
#
# remoteaccess.js runs the rule against a stand-in; this proves the
# parts a stand-in cannot: that polkit's JavaScript accepts the file,
# that polkit.spawn really runs the helper and turns its exit status
# into NO, that the helper's signal reaches a NON-ROOT program on the
# system bus (src/aurshell/guard.c, through tools/guardtest.c --listen),
# that her answer, written by that code, lets the next try through, and
# that a stand-in for build/forge's 49-auros.rules grant would otherwise
# have said yes. --prove then removes the rule and watches the
# AnyDesk check turn green, i.e. this test red.
#
# Runs as root in a throwaway Ubuntu 24.04 container, never on a real
# machine (it starts its own dbus-daemon and polkitd):
#
#   docker run --rm -v "$PWD":/w -w /w ubuntu:24.04 sh tools/scamtest/polkit.sh [--prove]
set -eu
cd "$(dirname "$0")/../.."
[ "$(id -u)" = 0 ] && [ -f /.dockerenv ] || { echo "polkit.sh: run me as root in a container"; exit 2; }

apt_try() { for i in 1 2 3; do timeout 300 apt-get "$@" && return 0; sleep 5; done; return 1; }
export DEBIAN_FRONTEND=noninteractive
if ! command -v pkcheck >/dev/null || ! command -v cc >/dev/null; then
    apt_try update -qq >/dev/null
    apt_try install -y -qq --no-install-recommends polkitd pkexec dbus dbus-bin \
        build-essential pkg-config libdbus-1-dev ca-certificates >/dev/null
fi
# The two actions' definitions, straight from the packages that own them.
work=$(mktemp -d)
( cd "$work" && apt_try download -qq flatpak gdebi >/dev/null 2>&1 )
for d in "$work"/*.deb; do dpkg-deb -x "$d" "$work/x"; done
cp "$work"/x/usr/share/polkit-1/actions/org.freedesktop.Flatpak.policy \
   "$work"/x/usr/share/polkit-1/actions/com.ubuntu.pkexec.gdebi-gtk.policy \
   /usr/share/polkit-1/actions/

# The image's pieces, where the image puts them.
install -m 0644 rootfs/etc/polkit-1/rules.d/40-auros-remote-access.rules /etc/polkit-1/rules.d/
install -D -m 0755 rootfs/usr/lib/auros/remote-access-gate /usr/lib/auros/remote-access-gate
install -d -m 1777 /run/auros-remote-access
# A stand-in for build/forge's grant: she may install and remove without
# a password. Without our rule, every check below would say yes.
cat > /etc/polkit-1/rules.d/49-auros.rules <<'EOL'
polkit.addRule(function(action, subject) {
    if (subject.user !== "her") { return undefined; }
    switch (action.id) {
    case "org.freedesktop.Flatpak.app-install":
    case "org.freedesktop.Flatpak.app-uninstall":
    case "com.ubuntu.pkexec.gdebi.gtk":
        return polkit.Result.YES;
    }
    return undefined;
});
EOL
id her >/dev/null 2>&1 || useradd -m her
id other >/dev/null 2>&1 || useradd -m other

# The desktop's listener: guard.c, built exactly as the shell links it.
cc -O2 -std=gnu11 -o /usr/local/bin/guardlisten tools/guardtest.c src/aurshell/guard.c \
   src/aurshell/foot.c src/aurshell/draw.c src/aurshell/shellcommon.c src/aurshell/anim.c \
   src/aurshell/layouts/[a-z]*.c src/common/theme.c src/common/font.c \
   -I src/aurshell -I src/common $(pkg-config --cflags --libs dbus-1) -lm

mkdir -p /run/dbus
dbus-daemon --system --fork
setpriv --reuid=polkitd --regid=polkitd --init-groups /usr/lib/polkit-1/polkitd --no-debug >/tmp/polkitd.log 2>&1 &
for i in $(seq 1 50); do
    dbus-send --system --print-reply --dest=org.freedesktop.DBus / \
        org.freedesktop.DBus.NameHasOwner string:org.freedesktop.PolicyKit1 2>/dev/null \
        | grep -q true && break
    sleep 0.2
done
su her -s /bin/sh -c 'exec sleep 600' &
sleep 0.5
HER=$(pgrep -u her -x sleep | head -1)

fail=0
check() {   # what  want-exit  action  key  value
    rc=0
    pkcheck --action-id "$3" --process "$HER" --detail "$4" "$5" >/dev/null 2>&1 || rc=$?
    if [ "$rc" = "$2" ]; then r=ok; else r="FAIL (pkcheck exit $rc, wanted $2)"; fail=$((fail + 1)); fi
    printf '    %-62s %s\n' "$1" "$r"
}
FP=org.freedesktop.Flatpak
DEB="/usr/bin/pkexec gdebi-gtk --non-interactive /home/her/Downloads"

echo "  under a real polkitd ($(dpkg-query -W -f '${Version}' polkitd))"
su her -s /bin/sh -c 'exec /usr/local/bin/guardlisten --listen 20' >/tmp/heard 2>&1 &
LISTENER=$!
sleep 1
check "AnyDesk from the store: refused until she answers" 1 $FP.app-install ref app/com.anydesk.Anydesk/x86_64/stable
lrc=0; wait "$LISTENER" || lrc=$?
printf '    %-62s %s\n' "the desktop, as her, heard it and she said yes" \
    "$([ "$lrc" = 0 ] && echo ok || { echo "FAIL: $(cat /tmp/heard)"; })"
[ "$lrc" = 0 ] || fail=$((fail + 1))
owner=$(stat -c %U /run/auros-remote-access/anydesk 2>/dev/null || echo none)
printf '    %-62s %s\n' "her answer is a file of hers" "$([ "$owner" = her ] && echo ok || echo "FAIL ($owner)")"
[ "$owner" = her ] || fail=$((fail + 1))
check "AnyDesk again, after her yes: allowed"             0 $FP.app-install ref app/com.anydesk.Anydesk/x86_64/stable
check "VLC from the store: as before"                     0 $FP.app-install ref app/org.videolan.VLC/x86_64/stable
check "removing AnyDesk: never in the way"                0 $FP.app-uninstall ref app/com.anydesk.Anydesk/x86_64/stable
check "TeamViewer .deb through gdebi: refused"            1 com.ubuntu.pkexec.gdebi.gtk command_line "$DEB/teamviewer_15.58.4_amd64.deb"
check "removing it through gdebi: never in the way"       0 com.ubuntu.pkexec.gdebi.gtk command_line "$DEB/teamviewer_15.58.4_amd64.deb --remove"
su other -s /bin/sh -c 'touch /run/auros-remote-access/rustdesk'
check "RustDesk 'yes' written by someone else: refused"   1 $FP.app-install ref app/com.rustdesk.RustDesk/x86_64/stable

if [ "${1:-}" = "--prove" ]; then
    echo "  the rule removed, the same check must change its answer"
    rm -f /run/auros-remote-access/anydesk /etc/polkit-1/rules.d/40-auros-remote-access.rules
    sleep 2                                     # polkitd rereads rules.d on its own
    rc=0
    pkcheck --action-id $FP.app-install --process "$HER" --detail ref \
        app/com.anydesk.Anydesk/x86_64/stable >/dev/null 2>&1 || rc=$?
    if [ "$rc" = 0 ]; then printf '    %-62s %s\n' "without the rule, AnyDesk goes straight on" "red, ok"
    else printf '    %-62s %s\n' "without the rule, AnyDesk goes straight on" "STILL REFUSED ($rc)"; fail=$((fail + 1)); fi
fi
if [ "$fail" -ne 0 ]; then
    echo "--- the listener said:"; cat /tmp/heard
    echo "--- answers:"; ls -la /run/auros-remote-access
    echo "--- polkitd said:"; tail -20 /tmp/polkitd.log
fi
echo "$([ "$fail" = 0 ] && echo "polkit: ok" || echo "polkit: FAIL ($fail)")"
[ "$fail" = 0 ]
