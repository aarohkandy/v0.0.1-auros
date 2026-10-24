#!/bin/sh
# NOT PART OF AUROS. Injected only into a disposable profiletest image.
# It installs Noble Fcitx packages in that copy, then proves composition and a
# parented-popup protocol syntax. Connection identity and candidate pixels stay
# explicitly unproven. It never writes expected text.
set -u
S=/dev/ttyS0
STATE=/var/lib/auros-profiletest/fcitx
HOST=/run/auros-profiletest-host
RUNTIME=/tmp/auros-profiletest-fcitx
say() { printf '\nPROFILETEST %s\n' "$*" > "$S"; }
mkdir -p "$STATE" "$HOST" "$RUNTIME"

if ! mountpoint -q "$HOST" 2>/dev/null; then
    modprobe 9pnet_virtio 2>/dev/null || true
    modprobe 9p 2>/dev/null || true
    mount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=none profiletest "$HOST" 2>/dev/null || {
        printf 'status=fail\nreason=host acknowledgement channel unavailable\n' > "$STATE/result"
        say "fcitx-probe=fail reason=host acknowledgement channel unavailable"; say done; exit 0
    }
fi

desktop_user=$(stat -c %U /run/auros 2>/dev/null || true)
desktop_uid=$(id -u "$desktop_user" 2>/dev/null || true)
desktop_home=$(getent passwd "$desktop_user" | awk -F: '{print $6}')
if [ -z "$desktop_user" ] || [ -z "$desktop_uid" ] || [ -z "$desktop_home" ]; then
    printf 'status=fail\nreason=desktop identity unavailable\n' > "$STATE/result"
    say "fcitx-probe=fail reason=desktop identity unavailable"; say done; exit 0
fi

as_desktop() {
    timeout 20 runuser -u "$desktop_user" -- env HOME="$desktop_home" \
        USER="$desktop_user" LOGNAME="$desktop_user" \
        XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" "$@"
}
user_env=$(as_desktop systemctl --user show-environment 2>/dev/null || true)
wayland=$(printf '%s\n' "$user_env" | sed -n 's/^WAYLAND_DISPLAY=//p' | tail -1)
display=$(printf '%s\n' "$user_env" | sed -n 's/^DISPLAY=//p' | tail -1)
[ -n "$wayland" ] && [ -n "$display" ] || {
    printf 'status=fail\nreason=session displays unavailable\n' > "$STATE/result"
    say "fcitx-probe=fail reason=session displays unavailable"; say done; exit 0
}

config_snapshot() {
    if [ -e "$desktop_home/.config/fcitx5" ]; then
        tar -C "$desktop_home/.config" -cf - fcitx5 2>/dev/null | sha256sum | awk '{print $1}'
    else
        printf absent
    fi
}
config_before=$(config_snapshot)

for unit in auros-profiletest-fcitx-mousepad auros-profiletest-fcitx-firefox auros-profiletest-fcitx-daemon; do
    as_desktop systemctl --user stop "$unit" >/dev/null 2>&1 || true
done
printf 'diagnostic_only=yes\n' > "$STATE/probe.log"
printf 'focus_password_reboot_proof=unproven\n' >> "$STATE/probe.log"
printf 'connection_identity=unproven\ncandidate_pixels_attributed=unproven\n' >> "$STATE/probe.log"

# Package failure is diagnostic unavailability, distinct from product failure.
if ! timeout 300 apt-get update -q > "$STATE/apt-update.log" 2>&1; then
    printf 'status=unavailable\nreason=Noble package indexes unavailable\n' > "$STATE/result"
    printf 'ordinary_service_ready=%s\nuser_config_unchanged=%s\n' \
        "$( [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = active ] && echo 1 || echo 0 )" \
        "$( [ "$(config_snapshot)" = "$config_before" ] && echo yes || echo no )" > "$STATE/restore.txt"
    say "fcitx-probe=unavailable reason=Noble package indexes unavailable"; say done; exit 0
fi
if ! DEBIAN_FRONTEND=noninteractive timeout 300 apt-get install -y -q --no-install-recommends \
        fcitx5 fcitx5-frontend-gtk3 fcitx5-pinyin > "$STATE/apt-install.log" 2>&1; then
    printf 'status=unavailable\nreason=Noble Fcitx packages unavailable\n' > "$STATE/result"
    printf 'ordinary_service_ready=%s\nuser_config_unchanged=%s\n' \
        "$( [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = active ] && echo 1 || echo 0 )" \
        "$( [ "$(config_snapshot)" = "$config_before" ] && echo yes || echo no )" > "$STATE/restore.txt"
    say "fcitx-probe=unavailable reason=Noble Fcitx packages unavailable"; say done; exit 0
fi
dpkg-query -W -f='${Package}=${Version}\n' fcitx5 fcitx5-frontend-gtk3 fcitx5-pinyin \
    > "$STATE/package-versions.txt" 2>&1
if ! grep -Fqx 'fcitx5=5.1.7-1build3' "$STATE/package-versions.txt" || \
   ! grep -Fqx 'fcitx5-frontend-gtk3=5.1.1-1build2' "$STATE/package-versions.txt" || \
   ! grep -Fqx 'fcitx5-pinyin=5.1.3-1build3' "$STATE/package-versions.txt"; then
    printf 'status=fail\nreason=installed packages do not match source-reviewed Noble versions\n' > "$STATE/result"
    printf 'ordinary_service_ready=%s\nuser_config_unchanged=%s\n' \
        "$( [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = active ] && echo 1 || echo 0 )" \
        "$( [ "$(config_snapshot)" = "$config_before" ] && echo yes || echo no )" > "$STATE/restore.txt"
    say "fcitx-probe=fail reason=source-reviewed package versions mismatch"; say done; exit 0
fi

CFG="$RUNTIME/config"; CACHE="$RUNTIME/cache"; DATA="$RUNTIME/data"
mkdir -p "$CFG/fcitx5" "$CACHE" "$DATA"
cat > "$CFG/fcitx5/profile" <<'EOF'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=pinyin

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[Groups/0/Items/1]
Name=pinyin
Layout=

[GroupOrder]
0=Default
EOF
chown -R "$desktop_user" "$RUNTIME"

ibus_stopped=0; fcitx_pid=; mouse_pid=; firefox_pid=; http_pid=; final_state=fail; final_reason=unexpected_exit
pid_gone() { [ -z "${1:-}" ] || [ ! -d "/proc/$1" ]; }
wait_pid_gone() { p=$1; i=0; while [ "$i" -lt 30 ]; do pid_gone "$p" && return 0; sleep .1; i=$((i+1)); done; return 1; }
wait_active() { u=$1; i=0; while [ "$i" -lt 30 ]; do [ "$(as_desktop systemctl --user is-active "$u" 2>/dev/null)" = active ] && return 0; sleep 1; i=$((i+1)); done; return 1; }
wait_hex_file() { f=$1; want=$2; i=0; while [ "$i" -lt 30 ]; do [ "$(od -An -tx1 "$f" 2>/dev/null | tr -d ' \n')" = "$want" ] && return 0; sleep 1; i=$((i+1)); done; return 1; }
trace_has_no_input_text() {
    python3 - "$1" <<'PY'
import sys
raw = open(sys.argv[1], "rb").read()
raise SystemExit(1 if b"nihao" in raw.lower() or bytes.fromhex("e4bda0e5a5bd") in raw else 0)
PY
}

as_fcitx_desktop() {
    as_desktop env -u DISPLAY GTK_IM_MODULE=fcitx GDK_BACKEND=wayland WAYLAND_DISPLAY="$wayland" \
        XDG_CONFIG_HOME="$CFG" XDG_CACHE_HOME="$CACHE" XDG_DATA_HOME="$DATA" "$@"
}
fcitx_owner_pid() {
    as_fcitx_desktop busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus GetConnectionUnixProcessID s org.fcitx.Fcitx5 2>/dev/null \
        | awk '$1=="u" && $2 ~ /^[0-9]+$/ {print $2}'
}

restore_and_finish() {
    cleanup_ok=1
    : > "$STATE/cleanup-units.txt"
    for u in auros-profiletest-fcitx-firefox auros-profiletest-fcitx-mousepad auros-profiletest-fcitx-daemon; do
        stop_rc=0; as_desktop systemctl --user stop "$u" >/dev/null 2>&1 || stop_rc=$?
        show_rc=0
        unit_show=$(as_desktop systemctl --user show -p LoadState -p ActiveState -p ControlGroup \
            "$u" 2>/dev/null) || show_rc=$?
        load_state=$(printf '%s\n' "$unit_show" | sed -n 's/^LoadState=//p')
        unit_state=$(printf '%s\n' "$unit_show" | sed -n 's/^ActiveState=//p')
        control_group=$(printf '%s\n' "$unit_show" | sed -n 's/^ControlGroup=//p')
        cgroup_empty=yes
        [ -z "$control_group" ] || [ ! -s "/sys/fs/cgroup$control_group/cgroup.procs" ] || cgroup_empty=no
        printf 'unit=%s stop_rc=%s show_rc=%s load_state=%s active_state=%s cgroup_empty=%s\n' \
            "$u" "$stop_rc" "$show_rc" "${load_state:-missing}" \
            "${unit_state:-missing}" "$cgroup_empty" >> "$STATE/cleanup-units.txt"
        if [ "$show_rc" != 0 ]; then
            cleanup_ok=0
        elif [ "$load_state" = not-found ]; then
            [ "$cgroup_empty" = yes ] || cleanup_ok=0
        else
            [ "$stop_rc" = 0 ] || cleanup_ok=0
            case "$unit_state" in inactive|failed) ;; *) cleanup_ok=0 ;; esac
        fi
        [ "$cgroup_empty" = yes ] || cleanup_ok=0
    done
    [ -z "${http_pid:-}" ] || { kill "$http_pid" 2>/dev/null || true; wait "$http_pid" 2>/dev/null || true; }
    for p in "$firefox_pid" "$mouse_pid" "$fcitx_pid"; do
        [ -z "$p" ] || wait_pid_gone "$p" || cleanup_ok=0
    done
    if [ "$ibus_stopped" = 1 ]; then as_desktop systemctl --user start auros-ibus.service >/dev/null 2>&1 || cleanup_ok=0; fi
    ordinary=0
    if [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = active ]; then
        ipid=$(as_desktop systemctl --user show -p MainPID --value auros-ibus.service 2>/dev/null)
        if [ -n "$ipid" ] && [ "$(readlink "/proc/$ipid/exe" 2>/dev/null)" = /usr/bin/ibus-daemon ] && \
           tr '\0' ' ' < "/proc/$ipid/cmdline" 2>/dev/null | grep -qx '/usr/bin/ibus-daemon --replace --xim '; then
            i=0
            while [ "$i" -lt 20 ]; do
                if as_desktop env DISPLAY="$display" WAYLAND_DISPLAY="$wayland" GTK_IM_MODULE=ibus \
                    ibus list-engine --name-only 2>/dev/null | grep -Fqx pinyin; then ordinary=1; break; fi
                sleep .25; i=$((i+1))
            done
        fi
    fi
    unchanged=no; [ "$(config_snapshot)" = "$config_before" ] && unchanged=yes
    find "$RUNTIME" -name '*.trace' -type f -delete 2>/dev/null || cleanup_ok=0
    if find "$RUNTIME" -name '*.trace' -type f -print -quit 2>/dev/null | grep -q .; then
        raw_retained=yes; cleanup_ok=0
    else
        raw_retained=no
    fi
    cp "$RUNTIME/fcitx.stdout" "$STATE/fcitx.stdout" 2>/dev/null || true
    cp "$RUNTIME/fcitx.stderr" "$STATE/fcitx.stderr" 2>/dev/null || true
    printf 'raw_protocol_logs_retained=%s\n' "$raw_retained" > "$STATE/protocol-summary.txt"
    printf 'ordinary_service_ready=%s\nuser_config_unchanged=%s\ncleanup_pids_gone=%s\n' \
        "$ordinary" "$unchanged" "$cleanup_ok" > "$STATE/restore.txt"
    if [ "$ordinary" != 1 ] || [ "$unchanged" != yes ] || [ "$cleanup_ok" != 1 ]; then
        final_state=fail; final_reason=cleanup_or_IBus_restoration_failed
    fi
    printf 'status=%s\nreason=%s\n' "$final_state" "$final_reason" > "$STATE/result"
    sync
    say "fcitx-probe=$final_state evidence=fcitx reason=$final_reason"; say done; exit 0
}
fail() { final_state=fail; final_reason=$(printf '%s' "$*" | tr ' ' '_'); restore_and_finish; }
trap 'final_state=fail; final_reason=signal; restore_and_finish' HUP INT TERM

actno=0
act() {
    actno=$((actno+1)); id=$((100+actno)); say "fcitx-do=$id $*"; i=0
    while [ "$i" -lt 180 ]; do
        [ -f "$HOST/failed-$id" ] && return 1
        ack=$(cat "$HOST/acks" 2>/dev/null || echo 0); case "$ack" in *[!0-9]*|'') ack=0;; esac
        [ "$ack" -ge "$id" ] && return 0
        sleep 1; i=$((i+1))
    done
    return 1
}

as_desktop systemctl --user stop auros-ibus.service >/dev/null 2>&1 || fail "could not stop shipped IBus"
ibus_stopped=1
[ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = inactive ] \
    || fail "shipped IBus remained active"

as_desktop systemd-run --user --quiet --collect --unit=auros-profiletest-fcitx-daemon \
    --property=Type=exec --setenv="XDG_CONFIG_HOME=$CFG" --setenv="XDG_CACHE_HOME=$CACHE" \
    --setenv="XDG_DATA_HOME=$DATA" --property="StandardOutput=append:$RUNTIME/fcitx.stdout" \
    --property="StandardError=append:$RUNTIME/fcitx.stderr" \
    -- /usr/bin/env -u DISPLAY -u WAYLAND_DISPLAY HOME="$desktop_home" \
    XDG_RUNTIME_DIR="/run/user/$desktop_uid" DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" \
    /usr/bin/fcitx5 -D \
    || fail "could not start owned Fcitx"
wait_active auros-profiletest-fcitx-daemon || fail "owned Fcitx did not stay active"
fcitx_pid=$(as_desktop systemctl --user show -p MainPID --value auros-profiletest-fcitx-daemon 2>/dev/null)
case "$fcitx_pid" in ''|0|*[!0-9]*) fail "owned Fcitx PID unavailable";; esac
[ "$(readlink "/proc/$fcitx_pid/exe" 2>/dev/null)" = /usr/bin/fcitx5 ] || fail "owned Fcitx executable mismatch"
[ "$(tr '\0' ' ' < "/proc/$fcitx_pid/cmdline" 2>/dev/null)" = '/usr/bin/fcitx5 -D ' ] \
    || fail "owned Fcitx arguments mismatch"
owner=$(fcitx_owner_pid); [ "$owner" = "$fcitx_pid" ] || fail "Fcitx D-Bus owner mismatch"
{
    printf 'pid=%s\nexe=/usr/bin/fcitx5\ncmdline=/usr/bin/fcitx5 -D\n' "$fcitx_pid"
    tr '\0' '\n' < "/proc/$fcitx_pid/environ" | grep -q '^DISPLAY=' && echo display_unset=no || echo display_unset=yes
    tr '\0' '\n' < "/proc/$fcitx_pid/environ" | grep -q '^WAYLAND_DISPLAY=' && echo wayland_display_unset=no || echo wayland_display_unset=yes
} > "$STATE/fcitx.process"

i=0
while [ "$i" -lt 30 ]; do
    if as_fcitx_desktop fcitx5-remote -s pinyin >/dev/null 2>&1 && \
       [ "$(as_fcitx_desktop fcitx5-remote -n 2>/dev/null)" = pinyin ]; then break; fi
    sleep 1; i=$((i+1))
done
[ "$i" -lt 30 ] || fail "exact pinyin engine unavailable"

run_app() { # app file/url trace unit
    app=$1; target=$2; trace=$3; unit=$4
    case "$app" in
      mousepad) command="mousepad $target";;
      firefox) command="firefox --no-remote --new-instance --profile $RUNTIME/firefox-profile $target";;
      *) return 1;;
    esac
    # shellcheck disable=SC2086
    as_desktop systemd-run --user --quiet --collect --unit="$unit" --property=Type=exec \
        --property="StandardError=append:$trace" -- /usr/bin/env -u DISPLAY \
        HOME="$desktop_home" XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" \
        GDK_BACKEND=wayland GTK_IM_MODULE=fcitx WAYLAND_DISPLAY="$wayland" \
        XDG_CONFIG_HOME="$CFG" XDG_CACHE_HOME="$CACHE" XDG_DATA_HOME="$DATA" \
        WAYLAND_DEBUG=client $command >/dev/null 2>&1
}

mouse_file="$RUNTIME/mousepad.txt"; : > "$mouse_file"; chown "$desktop_user" "$mouse_file"
mouse_trace="$RUNTIME/mousepad.trace"; : > "$mouse_trace"; chown "$desktop_user" "$mouse_trace"
act baseline fcitx-mousepad || fail "Mousepad baseline failed"
run_app mousepad "$mouse_file" "$mouse_trace" auros-profiletest-fcitx-mousepad || fail "Mousepad did not start"
wait_active auros-profiletest-fcitx-mousepad || fail "Mousepad did not stay active"
mouse_pid=$(as_desktop systemctl --user show -p MainPID --value auros-profiletest-fcitx-mousepad 2>/dev/null)
case "$mouse_pid" in ''|0|*[!0-9]*) fail "Mousepad PID unavailable";; esac
tr '\0' '\n' < "/proc/$mouse_pid/environ" | grep -E '^(GDK_BACKEND|GTK_IM_MODULE|WAYLAND_DISPLAY|DISPLAY)=' > "$STATE/mousepad.env"
grep -qx 'GDK_BACKEND=wayland' "$STATE/mousepad.env" && grep -qx 'GTK_IM_MODULE=fcitx' "$STATE/mousepad.env" \
    && ! grep -q '^DISPLAY=' "$STATE/mousepad.env" || fail "Mousepad native Wayland isolation missing"
act wait-change fcitx-mousepad || fail "Mousepad did not map"
act click-change fcitx-mousepad || fail "Mousepad focus click failed"
as_fcitx_desktop fcitx5-remote -s pinyin >/dev/null 2>&1 || fail "Mousepad Pinyin select failed"
act baseline fcitx-mousepad-candidate || fail "Mousepad candidate baseline failed"
boundary=$(wc -c < "$mouse_trace" | tr -d ' ')
act type-pinyin || fail "Mousepad typing failed"; sleep 1
act candidate-shot fcitx-mousepad-candidate fcitx-mousepad-candidate \
    || fail "Mousepad candidate was not visibly different"
python3 /usr/lib/auros-profiletest/fcitx_wayland_trace.py "$mouse_trace" --after-byte "$boundary" \
    > "$STATE/mousepad-popup.txt" 2> "$STATE/mousepad-popup.stderr" || fail "Mousepad parented-popup syntax missing"
trace_has_no_input_text "$mouse_trace" || fail "Mousepad protocol trace retained input text"
act key spc || fail "Mousepad candidate selection failed"
act chord ctrl s || fail "Mousepad save failed"
wait_hex_file "$mouse_file" e4bda0e5a5bd || fail "Mousepad exact composition missing"
act shot fcitx-mousepad-committed || fail "Mousepad committed screenshot failed"
cp "$mouse_file" "$STATE/mousepad.txt"
[ "$(fcitx_owner_pid)" = "$fcitx_pid" ] || fail "Fcitx service owner changed after Mousepad"
as_desktop systemctl --user stop auros-profiletest-fcitx-mousepad >/dev/null 2>&1 || fail "Mousepad stop failed"
wait_pid_gone "$mouse_pid" || fail "Mousepad PID remained"
mouse_pid=

FORM="$RUNTIME/form"; mkdir -p "$FORM" "$RUNTIME/firefox-profile"
cat > "$FORM/index.html" <<'EOF'
<!doctype html><meta charset="utf-8"><style>html,body,form{width:100%;height:100%;margin:0}form{position:relative;background:#01dead}input{position:absolute;left:15%;top:50%;width:70%;height:140px;font-size:48px}</style><form method="get" autocomplete="off"><input name="text" autocomplete="off" autofocus aria-label="Pinyin test"><button hidden>Submit</button></form>
EOF
cat > "$RUNTIME/firefox-profile/user.js" <<'EOF'
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.aboutwelcome.enabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
EOF
chown -R "$desktop_user" "$FORM" "$RUNTIME/firefox-profile"
python3 -m http.server 8765 --bind 127.0.0.1 --directory "$FORM" > "$STATE/firefox-http.log" 2>&1 & http_pid=$!
firefox_trace="$RUNTIME/firefox.trace"; : > "$firefox_trace"; chown "$desktop_user" "$firefox_trace"
act baseline fcitx-firefox || fail "Firefox baseline failed"
run_app firefox http://127.0.0.1:8765/ "$firefox_trace" auros-profiletest-fcitx-firefox || fail "Firefox did not start"
wait_active auros-profiletest-fcitx-firefox || fail "Firefox did not stay active"
firefox_pid=$(as_desktop systemctl --user show -p MainPID --value auros-profiletest-fcitx-firefox 2>/dev/null)
case "$firefox_pid" in ''|0|*[!0-9]*) fail "Firefox PID unavailable";; esac
tr '\0' '\n' < "/proc/$firefox_pid/environ" | grep -E '^(GDK_BACKEND|GTK_IM_MODULE|WAYLAND_DISPLAY|DISPLAY)=' > "$STATE/firefox.env"
grep -qx 'GDK_BACKEND=wayland' "$STATE/firefox.env" && grep -qx 'GTK_IM_MODULE=fcitx' "$STATE/firefox.env" \
    && ! grep -q '^DISPLAY=' "$STATE/firefox.env" || fail "Firefox native Wayland isolation missing"
i=0; while [ "$i" -lt 60 ] && ! grep -q '"GET / HTTP/' "$STATE/firefox-http.log" 2>/dev/null; do sleep 1; i=$((i+1)); done
[ "$i" -lt 60 ] || fail "Firefox form unavailable"
act wait-form || fail "Firefox form not visible"
act click-form || fail "Firefox field focus failed"
as_fcitx_desktop fcitx5-remote -s pinyin >/dev/null 2>&1 || fail "Firefox Pinyin select failed"
act baseline fcitx-firefox-candidate || fail "Firefox candidate baseline failed"
boundary=$(wc -c < "$firefox_trace" | tr -d ' ')
act type-pinyin || fail "Firefox typing failed"; sleep 1
act candidate-shot fcitx-firefox-candidate fcitx-firefox-candidate \
    || fail "Firefox candidate was not visibly different"
python3 /usr/lib/auros-profiletest/fcitx_wayland_trace.py "$firefox_trace" --after-byte "$boundary" \
    > "$STATE/firefox-popup.txt" 2> "$STATE/firefox-popup.stderr" || fail "Firefox parented-popup syntax missing"
trace_has_no_input_text "$firefox_trace" || fail "Firefox protocol trace retained input text"
act key spc || fail "Firefox candidate selection failed"
act key ret || fail "Firefox submit failed"
i=0; while [ "$i" -lt 30 ] && ! grep -Fq 'GET /?text=%E4%BD%A0%E5%A5%BD HTTP/' "$STATE/firefox-http.log"; do sleep 1; i=$((i+1)); done
[ "$i" -lt 30 ] || fail "Firefox exact composition missing"
python3 - "$STATE/firefox-http.log" "$STATE/firefox.txt" <<'PY'
import sys, urllib.parse
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if 'GET /?text=' in line:
        value=line.split('GET /?text=',1)[1].split(' HTTP/',1)[0]
        open(sys.argv[2],'wb').write(urllib.parse.unquote_to_bytes(value)); break
else: raise SystemExit(1)
PY
act shot fcitx-firefox-submitted || fail "Firefox submitted screenshot failed"
[ "$(fcitx_owner_pid)" = "$fcitx_pid" ] || fail "Fcitx service owner changed after Firefox"
printf 'service_owner_stable=yes\n' >> "$STATE/fcitx.process"
final_state=pass; final_reason=composition_and_parented_popup_syntax_observed
restore_and_finish
