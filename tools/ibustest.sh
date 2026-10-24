#!/bin/sh
# Source-level and build-wiring regression test for the optional IBus session.
# This proves readiness wiring, not that Pinyin has worked in a booted image.
set -eu
cd "$(dirname "$0")/.."

fail=0
check() { if "$@"; then :; else echo "FAIL: $*" >&2; fail=$((fail + 1)); fi; }
has_word() { file=$1; word=$2; grep -Eq "(^|[[:space:]\"])+${word}([[:space:]\"]|$)" "$file"; }

for package in ibus ibus-pinyin ibus-m17n ibus-gtk ibus-gtk3 ibus-gtk4 \
               language-pack-gnome-fr language-pack-gnome-zh-hans \
               language-pack-gnome-es language-pack-gnome-ar; do
    check has_word profiles/multilingual.profile "$package"
done
check grep -qx 'input_method="ibus"' profiles/multilingual.profile
check grep -q '^EnvironmentFile=-/etc/auros/input-method.env$' rootfs/etc/systemd/system/aurshell.service
check grep -q '^ConditionFileIsExecutable=/usr/bin/ibus-daemon$' rootfs/etc/systemd/user/auros-ibus.service
check grep -q '^ExecStart=/usr/lib/auros/ibus-session$' rootfs/etc/systemd/user/auros-ibus.service
check grep -q 'systemctl --user show-environment' rootfs/usr/lib/auros/ibus-session
check grep -q 'exec /usr/bin/ibus-daemon --replace --xim' rootfs/usr/lib/auros/ibus-session

t=$(mktemp -d "${TMPDIR:-/tmp}/auros-ibus-test.XXXXXX")
trap 'rm -rf "$t"' EXIT HUP INT TERM

# Run the shipped session script with only its two absolute executables
# replaced by recorders. WAYLAND_DISPLAY deliberately arrives first: starting
# then would make --xim race the Xwayland address it requires.
cat > "$t/systemctl" <<'EOF'
#!/bin/sh
[ "$*" = "--user show-environment" ] || exit 90
n=0
[ ! -f "$AUROS_IBUS_TEST_STATE" ] || read -r n < "$AUROS_IBUS_TEST_STATE"
n=$((n + 1)); printf '%s\n' "$n" > "$AUROS_IBUS_TEST_STATE"
printf '%s\n' 'GTK_IM_MODULE=ibus' 'WAYLAND_DISPLAY=wayland-3'
if [ "${AUROS_IBUS_TEST_MODE:-}" = delayed ] && [ "$n" -ge 3 ]; then
    printf '%s\n' 'DISPLAY=:17'
fi
EOF
cat > "$t/ibus-daemon" <<'EOF'
#!/bin/sh
{
    printf 'argc=%s\n' "$#"
    for arg in "$@"; do printf 'arg=%s\n' "$arg"; done
    printf 'WAYLAND_DISPLAY=%s\nDISPLAY=%s\n' "$WAYLAND_DISPLAY" "$DISPLAY"
} > "$AUROS_IBUS_TEST_RECORD"
EOF
chmod +x "$t/systemctl" "$t/ibus-daemon"
sed -e "s#/usr/bin/systemctl#$t/systemctl#g" \
    -e "s#/usr/bin/ibus-daemon#$t/ibus-daemon#g" \
    rootfs/usr/lib/auros/ibus-session > "$t/ibus-session"
chmod +x "$t/ibus-session"

AUROS_IBUS_TEST_STATE="$t/state" AUROS_IBUS_TEST_RECORD="$t/record" \
AUROS_IBUS_TEST_MODE=delayed AUROS_IBUS_WAIT_ATTEMPTS=5 "$t/ibus-session"
cat > "$t/expected" <<'EOF'
argc=2
arg=--replace
arg=--xim
WAYLAND_DISPLAY=wayland-3
DISPLAY=:17
EOF
check cmp -s "$t/expected" "$t/record"
check test "$(cat "$t/state")" -eq 3

rm -f "$t/state" "$t/record"
if AUROS_IBUS_TEST_STATE="$t/state" AUROS_IBUS_TEST_RECORD="$t/record" \
   AUROS_IBUS_TEST_MODE=never AUROS_IBUS_WAIT_ATTEMPTS=2 \
   "$t/ibus-session" 2> "$t/not-ready.err"; then
    echo "FAIL: IBus started without DISPLAY" >&2
    fail=$((fail + 1))
fi
check test ! -e "$t/record"
check grep -q 'did not publish both WAYLAND_DISPLAY and DISPLAY' "$t/not-ready.err"

mkdir -p "$t/etc/systemd/user"
cp rootfs/etc/systemd/user/auros-ibus.service "$t/etc/systemd/user/"

sh build/configure-input-method "$t" ibus
check grep -qx 'GTK_IM_MODULE=ibus' "$t/etc/auros/input-method.env"
check grep -qx 'QT_IM_MODULE=ibus' "$t/etc/auros/input-method.env"
check grep -qx 'XMODIFIERS=@im=ibus' "$t/etc/auros/input-method.env"
check test -L "$t/etc/environment.d/80-auros-input-method.conf"
check test -L "$t/etc/systemd/user/default.target.wants/auros-ibus.service"

# Reconfiguring the same rootfs as the ordinary desktop must remove every
# IBus opt-in. This catches stale state in forge's deliberately reused rootfs.
sh build/configure-input-method "$t" ''
check test ! -e "$t/etc/auros/input-method.env"
check test ! -L "$t/etc/environment.d/80-auros-input-method.conf"
check test ! -L "$t/etc/systemd/user/default.target.wants/auros-ibus.service"
if sh build/configure-input-method "$t" invented >/dev/null 2>&1; then
    echo "FAIL: an unknown input method was accepted" >&2
    fail=$((fail + 1))
fi

if [ "$fail" -ne 0 ]; then echo "$fail IBus wiring checks failed" >&2; exit 1; fi
echo "IBus wiring checks passed"
echo "Functional Pinyin evidence still requires tools/ibus-functional-test.md on a booted multilingual image."
