#!/bin/sh
# NOT PART OF AUROS. profiletest installs this only into its scratch image.
# It drives real applications through the running user's session and asks the
# host to supply physical QMP keyboard input. It never writes the expected
# Chinese text: the applications and IBus must produce that themselves.
set -u

S=/dev/ttyS0
STATE=/var/lib/auros-profiletest/ibus
HOST=/run/auros-profiletest-host
say() { printf '\nPROFILETEST %s\n' "$*" > "$S"; }

select_ibus_engine() { # engine phase
    engine=$1
    phase=$2
    log="$OUT/$phase-engine-select.log"
    : > "$log"
    attempt=1
    while [ "$attempt" -le 3 ]; do
        printf 'attempt=%s command=ibus engine %s\n' "$attempt" "$engine" >> "$log"
        ibus_cmd ibus engine "$engine" >> "$log" 2>&1
        set_rc=$?
        printf 'attempt=%s set_rc=%s\n' "$attempt" "$set_rc" >> "$log"
        current=$(ibus_cmd ibus engine 2>> "$log")
        get_rc=$?
        current=$(printf '%s' "$current" | tr -d '\r\n')
        printf 'attempt=%s get_rc=%s current=%s\n' \
            "$attempt" "$get_rc" "$current" >> "$log"
        [ "$get_rc" -eq 0 ] && [ "$current" = "$engine" ] && return 0
        sleep "${IBUS_SELECT_RETRY_DELAY:-2}"
        attempt=$((attempt + 1))
    done
    return 1
}

if [ "${1:-}" = selftest ]; then
    OUT=$(mktemp -d "${TMPDIR:-/tmp}/profiletest-ibus-select.XXXXXX")
    trap 'rm -rf "$OUT"' EXIT
    IBUS_SELECT_RETRY_DELAY=0
    mock_state="$OUT/state"
    mock_calls="$OUT/calls"
    ibus_cmd() {
        printf '%s\n' "$*" >> "$mock_calls"
        if [ "${3:-}" = pinyin ]; then
            printf pinyin > "$mock_state"
            printf '%s\n' 'setter exceeded its foreground deadline' >&2
            return 124
        fi
        cat "$mock_state" 2>/dev/null || return 1
    }
    select_ibus_engine pinyin late \
        || { echo "late engine activation was not accepted" >&2; exit 1; }
    grep -Fqx 'attempt=1 set_rc=124' "$OUT/late-engine-select.log" \
        || { echo "setter failure was not retained" >&2; exit 1; }
    grep -Fqx 'ibus engine pinyin' "$mock_calls" \
        || { echo "selector did not request the exact engine ID" >&2; exit 1; }

    : > "$mock_calls"
    ibus_cmd() {
        printf '%s\n' "$*" >> "$mock_calls"
        if [ "${3:-}" = pinyin ]; then
            printf '%s\n' 'Set global engine failed.' >&2
            return 1
        fi
        printf '%s\n' 'No engine is set.' >&2
        return 1
    }
    if select_ibus_engine pinyin failed; then
        echo "a never-selected engine was accepted" >&2
        exit 1
    fi
    grep -Fq 'Set global engine failed.' "$OUT/failed-engine-select.log" \
        || { echo "setter stderr was discarded" >&2; exit 1; }
    [ "$(grep -c '^ibus engine pinyin$' "$mock_calls")" -eq 3 ] \
        || { echo "selector did not stop after three attempts" >&2; exit 1; }
    echo "IBus engine selector selftest: pass"
    exit 0
fi

mkdir -p "$STATE" "$HOST"
if ! mountpoint -q "$HOST" 2>/dev/null; then
    modprobe 9pnet_virtio 2>/dev/null || true
    modprobe 9p 2>/dev/null || true
    mount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=none profiletest "$HOST" 2>/dev/null || {
        say "ibus-fail=host acknowledgement channel unavailable"
        say done
        exit 0
    }
fi

round=$(cat "$STATE/next-round" 2>/dev/null || echo 1)
case "$round" in 1|2) ;; *) round=1 ;; esac
OUT="$STATE/round$round"
rm -rf "$OUT"
mkdir -p "$OUT"
RUNTIME="/tmp/auros-profiletest-ibus-$round"
rm -rf "$RUNTIME"
mkdir -p "$RUNTIME"

fail() {
    printf 'status=fail\nreason=%s\n' "$*" > "$OUT/result"
    sync
    say "ibus-round=$round fail=$*"
    say done
    exit 0
}

# Every request has a number that remains monotonic across the reboot. The
# host writes the last completed number through a read-only 9p share; this is
# the same bounded handshake used by tools/parity.
action=0
act() {
    action=$((action + 1))
    id=$((round * 100 + action))
    say "ibus-do=$id $*"
    i=0
    while [ "$i" -lt 180 ]; do
        [ -f "$HOST/failed-$id" ] && return 1
        ack=$(cat "$HOST/acks" 2>/dev/null || echo 0)
        case "$ack" in *[!0-9]*|'') ack=0 ;; esac
        [ "$ack" -ge "$id" ] && return 0
        sleep 1
        i=$((i + 1))
    done
    return 1
}

desktop_user=$(stat -c %U /run/auros 2>/dev/null) || fail "desktop user unavailable"
desktop_uid=$(id -u "$desktop_user" 2>/dev/null) || fail "desktop uid unavailable"
desktop_home=$(getent passwd "$desktop_user" | awk -F: '{print $6}')
[ -n "$desktop_home" ] || fail "desktop home unavailable"

as_desktop() {
    timeout 15 runuser -u "$desktop_user" -- env \
        HOME="$desktop_home" USER="$desktop_user" LOGNAME="$desktop_user" \
        XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" "$@"
}
user_env=$(as_desktop systemctl --user show-environment 2>/dev/null) \
    || fail "user manager environment unavailable"
env_value() { printf '%s\n' "$user_env" | sed -n "s/^$1=//p" | tail -1; }
gtk=$(env_value GTK_IM_MODULE)
qt=$(env_value QT_IM_MODULE)
xmod=$(env_value XMODIFIERS)
wayland=$(env_value WAYLAND_DISPLAY)
display=$(env_value DISPLAY)
[ "$gtk" = ibus ] || fail "GTK_IM_MODULE is not ibus"
[ "$qt" = ibus ] || fail "QT_IM_MODULE is not ibus"
[ "$xmod" = '@im=ibus' ] || fail "XMODIFIERS is not @im=ibus"
[ -n "$wayland" ] && [ -n "$display" ] || fail "session displays unavailable"
[ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = active ] \
    || fail "auros-ibus.service is not active"

ibus_cmd() {
    as_desktop env DISPLAY="$display" WAYLAND_DISPLAY="$wayland" \
        GTK_IM_MODULE="$gtk" QT_IM_MODULE="$qt" XMODIFIERS="$xmod" "$@"
}

if ! ibus_cmd ibus list-engine --name-only > "$OUT/ibus-engine-ids.txt" \
        2> "$OUT/ibus-engine-list.stderr"; then
    fail "could not list exact IBus engine IDs"
fi
grep -Fqx pinyin "$OUT/ibus-engine-ids.txt" \
    || fail "the exact pinyin IBus engine ID is unavailable"

wait_active() {
    unit=$1; i=0
    while [ "$i" -lt 30 ]; do
        [ "$(as_desktop systemctl --user is-active "$unit" 2>/dev/null)" = active ] && return 0
        sleep 1; i=$((i + 1))
    done
    return 1
}
wait_hex_file() {
    file=$1; want=$2; i=0
    while [ "$i" -lt 30 ]; do
        got=$(od -An -tx1 "$file" 2>/dev/null | tr -d ' \n')
        [ "$got" = "$want" ] && return 0
        sleep 1; i=$((i + 1))
    done
    return 1
}

# Mousepad: open an existing empty file so Ctrl+S never opens a filename
# dialog. The host proves the screen changed, then clicks the editor through
# the real USB tablet. All text still comes from the QMP USB keyboard.
mouse_file="$RUNTIME/mousepad.txt"
: > "$mouse_file"
chown -R "$desktop_user" "$RUNTIME"
mouse_unit="auros-profiletest-mousepad-$round.service"
act baseline "mousepad-$round" || fail "could not record the screen before Mousepad"
as_desktop systemd-run --user --quiet --collect --unit="$mouse_unit" \
    --property=Type=exec -- mousepad "$mouse_file" >/dev/null 2>&1 \
    || fail "Mousepad did not start"
wait_active "$mouse_unit" || fail "Mousepad unit did not become active"
act wait-change "mousepad-$round" || fail "Mousepad did not map a visible window"
act click-change "mousepad-$round" || fail "host did not focus Mousepad's editor"
select_ibus_engine pinyin mousepad \
    || fail "could not select pinyin for Mousepad; see mousepad-engine-select.log"
act type-pinyin || fail "host did not type into Mousepad"
act shot "ibus-round$round-mousepad-candidate" || fail "Mousepad screenshot failed"
act key spc || fail "host did not choose the Mousepad candidate"
act chord ctrl s || fail "host did not save Mousepad"
# e4bda0e5a5bd is UTF-8 for the required two-character composition.
wait_hex_file "$mouse_file" e4bda0e5a5bd || fail "Mousepad did not save the exact composition"
as_desktop systemctl --user stop "$mouse_unit" >/dev/null 2>&1 || true
cp "$mouse_file" "$OUT/mousepad.txt" || fail "could not retain Mousepad evidence"
printf 'mousepad=pass\n' > "$OUT/result"

# Firefox: the first GET is a machine-readable readiness signal. The final
# GET is exact browser-submitted UTF-8 evidence rather than pixels or OCR.
FORM="$RUNTIME/form"
mkdir -p "$FORM"
cat > "$FORM/index.html" <<'EOF'
<!doctype html><meta charset="utf-8">
<style>
html,body,form { width:100%; height:100%; margin:0 }
form { position:relative; background:#01dead }
input { position:absolute; left:15%; top:50%; transform:translateY(-50%);
        box-sizing:border-box; width:70%; height:140px; font-size:48px }
button { display:none }
</style>
<form method="get"><input name="text" autofocus aria-label="Pinyin test"><button>Submit</button></form>
EOF
http_log="$OUT/firefox-http.log"
python3 -m http.server 8765 --bind 127.0.0.1 --directory "$FORM" >"$http_log" 2>&1 &
http_pid=$!
i=0
while [ "$i" -lt 15 ]; do
    python3 -c 'import socket; s=socket.create_connection(("127.0.0.1",8765),.5); s.close()' \
        >/dev/null 2>&1 && break
    kill -0 "$http_pid" 2>/dev/null || fail "local Firefox test server stopped"
    sleep 1; i=$((i + 1))
done
[ "$i" -lt 15 ] || fail "local Firefox test server did not bind"
profile="$RUNTIME/firefox-profile"
mkdir -p "$profile"; chown -R "$desktop_user" "$profile"
cat > "$profile/user.js" <<'EOF'
// Test-only fresh profile: keep browser-owned first-run UI off the local form.
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.aboutwelcome.enabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
EOF
chown "$desktop_user" "$profile/user.js"
firefox_unit="auros-profiletest-firefox-$round.service"
act baseline "firefox-$round" || fail "could not record the screen before Firefox"
as_desktop systemd-run --user --quiet --collect --unit="$firefox_unit" \
    --property=Type=exec -- firefox --no-remote --new-instance \
    --profile "$profile" http://127.0.0.1:8765/ >/dev/null 2>&1 \
    || fail "Firefox did not start"
wait_active "$firefox_unit" || fail "Firefox unit did not become active"
i=0
while [ "$i" -lt 60 ] && ! grep -q '"GET / HTTP/[0-9.]*" 200' "$http_log" 2>/dev/null; do
    sleep 1; i=$((i + 1))
done
grep -q '"GET / HTTP/[0-9.]*" 200' "$http_log" 2>/dev/null \
    || fail "Firefox did not load the local form"
act wait-form || fail "Firefox did not show the local form in front"
act click-form || fail "host did not focus Firefox's form field"
firefox_pid=$(as_desktop systemctl --user show -p MainPID --value "$firefox_unit" 2>/dev/null)
case "$firefox_pid" in ''|0|*[!0-9]*) fail "Firefox MainPID unavailable" ;; esac
tr '\0' '\n' < "/proc/$firefox_pid/environ" 2>/dev/null \
    | grep -E '^(GTK_IM_MODULE|WAYLAND_DISPLAY|DISPLAY)=' > "$OUT/firefox.env" \
    || fail "Firefox environment unavailable"
grep -qx 'GTK_IM_MODULE=ibus' "$OUT/firefox.env" || fail "Firefox did not inherit GTK IBus"
select_ibus_engine pinyin firefox \
    || fail "could not select pinyin for Firefox; see firefox-engine-select.log"
act type-pinyin || fail "host did not type into Firefox"
act shot "ibus-round$round-firefox-candidate" || fail "Firefox screenshot failed"
act key spc || fail "host did not choose the Firefox candidate"
act key ret || fail "host did not submit the Firefox form"
i=0
want='"GET /?text=%E4%BD%A0%E5%A5%BD HTTP/'
while [ "$i" -lt 30 ] && ! grep -Fq "$want" "$http_log" 2>/dev/null; do
    sleep 1; i=$((i + 1))
done
grep -Fq "$want" "$http_log" 2>/dev/null \
    || fail "Firefox did not submit the exact composition"
printf 'firefox=pass\nstatus=pass\n' >> "$OUT/result"
as_desktop systemctl --user stop "$firefox_unit" >/dev/null 2>&1 || true
kill "$http_pid" 2>/dev/null || true
wait "$http_pid" 2>/dev/null || true
rm -rf "$RUNTIME"
say "ibus-round=$round mousepad=pass firefox=pass"

if [ "$round" = 1 ]; then
    echo 2 > "$STATE/next-round"
    sync
    say "ibus-rebooting=after-round-1"
    systemctl reboot || fail "reboot request failed"
    sleep 30
    fail "reboot did not happen"
fi

rm -f "$STATE/next-round"
sync
say "ibus-functional=pass rounds=2"
say done
