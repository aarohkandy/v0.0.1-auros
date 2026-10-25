#!/bin/sh
# Source-level and build-wiring regression test for optional input sessions.
# This proves readiness wiring, not that Pinyin has worked in a booted image.
set -eu
cd "$(dirname "$0")/.."

fail=0
check() { if "$@"; then :; else echo "FAIL: $*" >&2; fail=$((fail + 1)); fi; }
has_word() { file=$1; word=$2; grep -Eq "(^|[[:space:]\"])+${word}([[:space:]\"]|$)" "$file"; }
profile_extra=$(sh -c '. profiles/multilingual.profile; printf "%s\n" "$packages_extra"')
profile_exclude=$(sh -c '. profiles/multilingual.profile; printf "%s\n" "$packages_exclude"')
words_have() { printf '%s\n' "$1" | grep -Eq "(^|[[:space:]])$2([[:space:]]|$)"; }

for package in fcitx5 fcitx5-frontend-gtk3 fcitx5-pinyin \
               language-pack-gnome-fr language-pack-gnome-zh-hans \
               language-pack-gnome-es language-pack-gnome-ar; do
    check words_have "$profile_extra" "$package"
done
for package in ibus ibus-pinyin ibus-m17n ibus-gtk ibus-gtk3 ibus-gtk4; do
    if words_have "$profile_extra" "$package"; then
        echo "FAIL: multilingual still installs $package" >&2
        fail=$((fail + 1))
    fi
    check words_have "$profile_exclude" "$package"
done
check grep -qx 'input_method="fcitx"' profiles/multilingual.profile
check grep -q '^EnvironmentFile=-/etc/auros/input-method.env$' rootfs/etc/systemd/system/aurshell.service
check grep -q '^ConditionFileIsExecutable=/usr/bin/ibus-daemon$' rootfs/etc/systemd/user/auros-ibus.service
check grep -q '^ExecStart=/usr/lib/auros/ibus-session$' rootfs/etc/systemd/user/auros-ibus.service
check grep -q 'systemctl --user show-environment' rootfs/usr/lib/auros/ibus-session
check grep -q 'exec /usr/bin/ibus-daemon --replace --xim' rootfs/usr/lib/auros/ibus-session
check grep -q '^Type=dbus$' rootfs/etc/systemd/user/auros-fcitx.service
check grep -q '^BusName=org.fcitx.Fcitx5$' rootfs/etc/systemd/user/auros-fcitx.service
check grep -q '^Conflicts=auros-ibus.service$' rootfs/etc/systemd/user/auros-fcitx.service
check grep -q '^UnsetEnvironment=DISPLAY WAYLAND_DISPLAY$' rootfs/etc/systemd/user/auros-fcitx.service
check grep -q '^ExecStart=/usr/lib/auros/fcitx-session$' rootfs/etc/systemd/user/auros-fcitx.service
check grep -q '^exec /usr/bin/fcitx5 -D$' rootfs/usr/lib/auros/fcitx-session

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

rm -f "$t/state" "$t/record"
sed 's/sleep 0.1/:/' "$t/ibus-session" > "$t/ibus-fast"
if AUROS_IBUS_TEST_STATE="$t/state" AUROS_IBUS_TEST_RECORD="$t/record" \
   AUROS_IBUS_TEST_MODE=never AUROS_IBUS_WAIT_ATTEMPTS=999 \
   sh "$t/ibus-fast" 2> "$t/not-ready.err"; then
    echo "FAIL: IBus started without a display after an excessive wait override" >&2
    fail=$((fail + 1))
fi
check test "$(cat "$t/state")" -eq 100
check test ! -e "$t/record"

mkdir -p "$t/etc/systemd/user"
cp rootfs/etc/systemd/user/auros-ibus.service "$t/etc/systemd/user/"
cp rootfs/etc/systemd/user/auros-fcitx.service "$t/etc/systemd/user/"
mkdir -p "$t/usr/lib/auros" "$t/usr/bin"
cp rootfs/usr/lib/auros/fcitx-session "$t/usr/lib/auros/"
: > "$t/usr/bin/fcitx5"
chmod +x "$t/usr/bin/fcitx5"
mkdir -p "$t/var/lib/dpkg"
cat > "$t/var/lib/dpkg/status" <<'EOF'
Package: fcitx5
Status: install ok installed
Version: 5.1.7-1build3

Package: fcitx5-frontend-gtk3
Status: install ok installed
Version: 5.1.1-1build2

Package: fcitx5-pinyin
Status: install ok installed
Version: 5.1.3-1build3
EOF
sh build/configure-input-method "$t" fcitx fr

# The production Fcitx launcher creates the diagnostic-proven default once,
# preserves a user's later choice, and never gives the daemon a display.
cat > "$t/fcitx5" <<'EOF'
#!/bin/sh
{
    printf 'argc=%s\n' "$#"
    for arg in "$@"; do printf 'arg=%s\n' "$arg"; done
    env | grep -E '^(DISPLAY|WAYLAND_DISPLAY)=' || true
} > "$AUROS_FCITX_TEST_RECORD"
EOF
chmod +x "$t/fcitx5"
sed -e "s#/usr/bin/fcitx5#$t/fcitx5#g" \
    -e "s#/usr/share/auros/fcitx-profile#$t/usr/share/auros/fcitx-profile#g" \
    rootfs/usr/lib/auros/fcitx-session > "$t/fcitx-session"
chmod +x "$t/fcitx-session"
mkdir -p "$t/home"
DISPLAY=:17 WAYLAND_DISPLAY=wayland-3 HOME="$t/home" XDG_CONFIG_HOME="$t/config" \
    AUROS_FCITX_TEST_RECORD="$t/fcitx-record" "$t/fcitx-session"
cat > "$t/fcitx-expected" <<'EOF'
argc=1
arg=-D
EOF
check cmp -s "$t/fcitx-expected" "$t/fcitx-record"
check grep -qx 'DefaultIM=pinyin' "$t/config/fcitx5/profile"
check grep -qx 'Default Layout=fr' "$t/config/fcitx5/profile"
check grep -qx 'Name=keyboard-fr' "$t/config/fcitx5/profile"
check grep -qx 'Name=pinyin' "$t/config/fcitx5/profile"
mode=$(python3 -c 'import os,stat,sys; print(oct(stat.S_IMODE(os.stat(sys.argv[1]).st_mode)))' \
    "$t/config/fcitx5/profile")
check test "$mode" = 0o600

printf 'user-owned=yes\n' > "$t/config/fcitx5/profile"
DISPLAY=:19 WAYLAND_DISPLAY=wayland-9 HOME="$t/home" XDG_CONFIG_HOME="$t/config" \
    AUROS_FCITX_TEST_RECORD="$t/fcitx-record" "$t/fcitx-session"
check grep -qx 'user-owned=yes' "$t/config/fcitx5/profile"

mv "$t/usr/share/auros/fcitx-profile" "$t/usr/share/auros/fcitx-profile.saved"
rm -f "$t/fcitx-record"
if HOME="$t/home" XDG_CONFIG_HOME="$t/config" \
   AUROS_FCITX_TEST_RECORD="$t/fcitx-record" "$t/fcitx-session" 2> "$t/fcitx-template.err"; then
    echo "FAIL: Fcitx started without its root-owned default profile" >&2
    fail=$((fail + 1))
fi
check test ! -e "$t/fcitx-record"
check grep -q 'fcitx-profile is missing' "$t/fcitx-template.err"
mv "$t/usr/share/auros/fcitx-profile.saved" "$t/usr/share/auros/fcitx-profile"

rm -f "$t/fcitx-record"
: > "$t/not-a-directory"
if HOME="$t/home" XDG_CONFIG_HOME="$t/not-a-directory" \
   AUROS_FCITX_TEST_RECORD="$t/fcitx-record" "$t/fcitx-session" 2> "$t/fcitx-fail.err"; then
    echo "FAIL: Fcitx started without a writable configuration" >&2
    fail=$((fail + 1))
fi
check test ! -e "$t/fcitx-record"
check grep -q 'cannot create' "$t/fcitx-fail.err"
if find "$t" -name 'profile.auros.*' -print | grep -q .; then
    echo "FAIL: Fcitx left a staged profile after startup failure" >&2
    fail=$((fail + 1))
fi

mkdir -p "$t/fail-bin"
cat > "$t/fail-bin/ln" <<'EOF'
#!/bin/sh
exit 1
EOF
chmod +x "$t/fail-bin/ln"
rm -rf "$t/fail-config" "$t/fcitx-record"
if PATH="$t/fail-bin:$PATH" HOME="$t/home" XDG_CONFIG_HOME="$t/fail-config" \
   AUROS_FCITX_TEST_RECORD="$t/fcitx-record" "$t/fcitx-session" 2> "$t/fcitx-link.err"; then
    echo "FAIL: Fcitx started after its default profile could not be installed" >&2
    fail=$((fail + 1))
fi
check test ! -e "$t/fcitx-record"
check grep -q 'cannot install the default profile' "$t/fcitx-link.err"
if find "$t/fail-config" -name 'profile.auros.*' -print | grep -q .; then
    echo "FAIL: Fcitx left its staged profile after install failure" >&2
    fail=$((fail + 1))
fi

sh build/configure-input-method "$t" ibus
check grep -qx 'GTK_IM_MODULE=ibus' "$t/etc/auros/input-method.env"
check grep -qx 'QT_IM_MODULE=ibus' "$t/etc/auros/input-method.env"
check grep -qx 'XMODIFIERS=@im=ibus' "$t/etc/auros/input-method.env"
check test -L "$t/etc/environment.d/80-auros-input-method.conf"
check test -L "$t/etc/systemd/user/default.target.wants/auros-ibus.service"
check test ! -L "$t/etc/systemd/user/default.target.wants/auros-fcitx.service"

sh build/configure-input-method "$t" fcitx fr
check grep -qx 'GTK_IM_MODULE=fcitx' "$t/etc/auros/input-method.env"
check test "$(wc -l < "$t/etc/auros/input-method.env" | tr -d ' ')" -eq 1
check test -L "$t/etc/environment.d/80-auros-input-method.conf"
check test -L "$t/etc/systemd/user/default.target.wants/auros-fcitx.service"
check test ! -L "$t/etc/systemd/user/default.target.wants/auros-ibus.service"
check grep -qx 'Default Layout=fr' "$t/usr/share/auros/fcitx-profile"
check grep -qx 'Name=keyboard-fr' "$t/usr/share/auros/fcitx-profile"
check test "$(python3 -c 'import os,stat,sys; print(oct(stat.S_IMODE(os.stat(sys.argv[1]).st_mode)))' "$t/usr/share/auros/fcitx-profile")" = 0o644

cp "$t/var/lib/dpkg/status" "$t/status.good"
sed 's/5.1.3-1build3/5.1.3-unreviewed/' "$t/status.good" > "$t/var/lib/dpkg/status"
if sh build/configure-input-method "$t" fcitx fr > /dev/null 2> "$t/version.err"; then
    echo "FAIL: an unreviewed Fcitx package version was accepted" >&2
    fail=$((fail + 1))
fi
check grep -q "fcitx5-pinyin state/version 'install ok installed|5.1.3-unreviewed' is not reviewed" "$t/version.err"
check test ! -e "$t/etc/auros/input-method.env"
check test ! -L "$t/etc/systemd/user/default.target.wants/auros-fcitx.service"
cp "$t/status.good" "$t/var/lib/dpkg/status"

awk 'BEGIN { changed=0 }
     !changed && $0 == "Status: install ok installed" {
         print "Status: deinstall ok config-files"; changed=1; next
     }
     { print }' "$t/var/lib/dpkg/status" > "$t/status.not-installed"
mv "$t/status.not-installed" "$t/var/lib/dpkg/status"
if sh build/configure-input-method "$t" fcitx fr > /dev/null 2> "$t/state.err"; then
    echo "FAIL: a non-installed Fcitx package was accepted" >&2
    fail=$((fail + 1))
fi
check grep -q "fcitx5 state/version 'deinstall ok config-files|5.1.7-1build3'" "$t/state.err"
cp "$t/status.good" "$t/var/lib/dpkg/status"

# A reused rootfs must not carry an installed IBus stack into the Fcitx image.
cp "$t/var/lib/dpkg/status" "$t/status.clean"
cat >> "$t/var/lib/dpkg/status" <<'EOF'

Package: ibus
Status: install ok installed
Version: 1.5.29~rc1-1build3
EOF
if sh build/configure-input-method "$t" fcitx fr > /dev/null 2> "$t/conflict.err"; then
    echo "FAIL: a reused rootfs retained conflicting IBus packages" >&2
    fail=$((fail + 1))
fi
check grep -q 'conflicting input-method package remains installed: ibus' "$t/conflict.err"
mv "$t/status.clean" "$t/var/lib/dpkg/status"

if sh build/configure-input-method "$t" fcitx 'fr;bad' > /dev/null 2> "$t/layout.err"; then
    echo "FAIL: an unsafe Fcitx keyboard layout was accepted" >&2
    fail=$((fail + 1))
fi
check grep -q 'invalid Fcitx keyboard layout' "$t/layout.err"

sh build/configure-input-method "$t" fcitx fr
check grep -qx 'GTK_IM_MODULE=fcitx' "$t/etc/auros/input-method.env"
check test -L "$t/etc/systemd/user/default.target.wants/auros-fcitx.service"

# Reconfiguring the same rootfs as the ordinary desktop must remove every
# input-method opt-in. This catches stale state in forge's reused rootfs.
sh build/configure-input-method "$t" ''
check test ! -e "$t/etc/auros/input-method.env"
check test ! -L "$t/etc/environment.d/80-auros-input-method.conf"
check test ! -L "$t/etc/systemd/user/default.target.wants/auros-ibus.service"
check test ! -L "$t/etc/systemd/user/default.target.wants/auros-fcitx.service"
if sh build/configure-input-method "$t" invented >/dev/null 2>&1; then
    echo "FAIL: an unknown input method was accepted" >&2
    fail=$((fail + 1))
fi

if [ "$fail" -ne 0 ]; then echo "$fail input-method wiring checks failed" >&2; exit 1; fi
echo "Input-method wiring checks passed"
echo "Fresh-image Fcitx certification still requires tools/profiletest.sh on a booted multilingual image."
