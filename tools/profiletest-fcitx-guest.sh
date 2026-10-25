#!/bin/sh
# NOT PART OF AUROS. Injected only into a disposable profiletest image.
# In legacy mode it installs Noble Fcitx packages into a retained IBus image.
# Production mode instead certifies the already-shipped Fcitx service without
# replacing it. Both prove composition and parented-popup protocol syntax;
# Firefox connection identity and candidate-pixel attribution remain unproven.
set -u
S=/dev/ttyS0
STATE=/var/lib/auros-profiletest/fcitx
HOST=/run/auros-profiletest-host
RUNTIME=/tmp/auros-profiletest-fcitx
say() { printf '\nPROFILETEST %s\n' "$*" > "$S"; }
mkdir -p "$STATE" "$HOST" "$RUNTIME"
SECURITY=0
[ -f /usr/lib/auros-profiletest/fcitx-security.enabled ] && SECURITY=1
PRODUCTION=0
[ -f /usr/lib/auros-profiletest/fcitx-production.enabled ] && PRODUCTION=1
boot_id=
if [ "$PRODUCTION" = 1 ] && [ "$SECURITY" = 1 ]; then
    boot_id=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || true)
    printf '%s\n' "$boot_id" \
        | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' || {
        printf 'status=fail\nreason=boot identity unavailable\n' > "$STATE/result"
        sync; say "fcitx-probe=fail reason=boot identity unavailable"; say done; exit 0
    }
fi
security_round=1
if [ "$SECURITY" = 1 ] && [ "$(cat "$STATE/security-next-round" 2>/dev/null)" = 2 ]; then
    security_round=2
    find "$STATE" -maxdepth 1 -type f ! -name security-next-round ! -name action-sequence -delete
fi

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
monotonic_ms() { awk '{printf "%d\n", $1 * 1000}' /proc/uptime; }
as_desktop_until() { # absolute monotonic millisecond deadline, command...
    deadline=$1; shift
    now=$(monotonic_ms); remaining=$((deadline-now))
    [ "$remaining" -gt 0 ] || return 124
    limit=$(awk -v ms="$remaining" 'BEGIN {printf "%.3fs", ms / 1000}')
    timeout "$limit" runuser -u "$desktop_user" -- env HOME="$desktop_home" \
        USER="$desktop_user" LOGNAME="$desktop_user" \
        XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" "$@"
}
ibus_identity_snapshot() { # deadline, pid; print authenticated process start time
    identity_deadline=$1; identity_pid=$2
    identity_now=$(monotonic_ms); identity_remaining=$((identity_deadline-identity_now))
    [ "$identity_remaining" -gt 0 ] || return 124
    identity_limit=$(awk -v ms="$identity_remaining" 'BEGIN {printf "%.3fs", ms / 1000}')
    timeout "$identity_limit" python3 - "$identity_pid" <<'PY'
import os, pathlib, sys
pid = sys.argv[1]
try:
    root = pathlib.Path("/proc") / pid
    executable = os.readlink(root / "exe")
    cmdline = (root / "cmdline").read_bytes().split(b"\0")
    stat = (root / "stat").read_text(encoding="ascii")
    fields = stat.rsplit(")", 1)[1].split()
except (OSError, UnicodeDecodeError, IndexError):
    raise SystemExit(1)
if executable != "/usr/bin/ibus-daemon":
    raise SystemExit(1)
if cmdline != [b"/usr/bin/ibus-daemon", b"--replace", b"--xim", b""]:
    raise SystemExit(1)
try:
    print(int(fields[19]))
except (ValueError, IndexError):
    raise SystemExit(1)
PY
}
user_env=$(as_desktop systemctl --user show-environment 2>/dev/null || true)
wayland=$(printf '%s\n' "$user_env" | sed -n 's/^WAYLAND_DISPLAY=//p' | tail -1)
display=$(printf '%s\n' "$user_env" | sed -n 's/^DISPLAY=//p' | tail -1)
[ -n "$wayland" ] && [ -n "$display" ] || {
    printf 'status=fail\nreason=session displays unavailable\n' > "$STATE/result"
    say "fcitx-probe=fail reason=session displays unavailable"; say done; exit 0
}

config_snapshot() {
    if [ "$PRODUCTION" = 1 ] && [ -f "$desktop_home/.config/fcitx5/profile" ]; then
        sha256sum "$desktop_home/.config/fcitx5/profile" | awk '{print $1}'
    elif [ -e "$desktop_home/.config/fcitx5" ]; then
        tar -C "$desktop_home/.config" -cf - fcitx5 2>/dev/null | sha256sum | awk '{print $1}'
    else
        printf absent
    fi
}
config_before=$(config_snapshot)

profile_snapshot() { # start|end; production-security evidence only
    profile_phase=$1
    [ "$PRODUCTION" = 1 ] && [ "$SECURITY" = 1 ] || return 0
    helper=/usr/lib/auros-profiletest/fcitx-profile-snapshot.py
    "$helper" create --source runtime --phase "$profile_phase" \
        --input "$desktop_home/.config/fcitx5/profile" \
        --output "$STATE/profile-runtime-$profile_phase.json" \
        2> "$STATE/profile-snapshot-$profile_phase.stderr" || return 1
    "$helper" create --source template --phase "$profile_phase" \
        --input /usr/share/auros/fcitx-profile \
        --output "$STATE/profile-template-$profile_phase.json" \
        2>> "$STATE/profile-snapshot-$profile_phase.stderr" || return 1
    [ ! -s "$STATE/profile-snapshot-$profile_phase.stderr" ] || return 1
    rm -f "$STATE/profile-snapshot-$profile_phase.stderr"
}

for unit in auros-profiletest-fcitx-mousepad auros-profiletest-fcitx-mousepad-b \
            auros-profiletest-fcitx-firefox auros-profiletest-fcitx-password \
            auros-profiletest-fcitx-daemon; do
    as_desktop systemctl --user stop "$unit" >/dev/null 2>&1 || true
done
printf 'diagnostic_only=yes\n' > "$STATE/probe.log"
printf 'source=%s\n' "$( [ "$PRODUCTION" = 1 ] && echo shipped-fcitx || echo retained-ibus-conversion )" \
    >> "$STATE/probe.log"
printf 'focus_password_reboot_proof=unproven\n' >> "$STATE/probe.log"
printf 'connection_identity=unproven\ncandidate_pixels_attributed=unproven\n' >> "$STATE/probe.log"
[ "$SECURITY" = 0 ] || printf 'security_stage=yes\nsecurity_round=%s\n' "$security_round" >> "$STATE/probe.log"

# Package failure is diagnostic unavailability, distinct from product failure.
if [ "$PRODUCTION" = 0 ] && ! timeout 300 apt-get update -q > "$STATE/apt-update.log" 2>&1; then
    printf 'status=unavailable\nreason=Noble package indexes unavailable\n' > "$STATE/result"
    printf 'ordinary_service_ready=%s\nuser_config_unchanged=%s\n' \
        "$( [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = active ] && echo 1 || echo 0 )" \
        "$( [ "$(config_snapshot)" = "$config_before" ] && echo yes || echo no )" > "$STATE/restore.txt"
    sync
    say "fcitx-probe=unavailable reason=Noble package indexes unavailable"; say done; exit 0
fi
if [ "$PRODUCTION" = 0 ] && ! DEBIAN_FRONTEND=noninteractive timeout 300 apt-get install -y -q --no-install-recommends \
        fcitx5 fcitx5-frontend-gtk3 fcitx5-pinyin > "$STATE/apt-install.log" 2>&1; then
    printf 'status=unavailable\nreason=Noble Fcitx packages unavailable\n' > "$STATE/result"
    printf 'ordinary_service_ready=%s\nuser_config_unchanged=%s\n' \
        "$( [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = active ] && echo 1 || echo 0 )" \
        "$( [ "$(config_snapshot)" = "$config_before" ] && echo yes || echo no )" > "$STATE/restore.txt"
    sync
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
    sync
    say "fcitx-probe=fail reason=source-reviewed package versions mismatch"; say done; exit 0
fi

if [ "$PRODUCTION" = 1 ]; then
    CFG="$desktop_home/.config"; CACHE="$desktop_home/.cache"; DATA="$desktop_home/.local/share"
    [ "$(as_desktop systemctl --user is-active auros-fcitx.service 2>/dev/null)" = active ] || {
        printf 'status=fail\nreason=shipped Fcitx service inactive\n' > "$STATE/result"
        sync; say "fcitx-probe=fail reason=shipped Fcitx service inactive"; say done; exit 0
    }
    [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = inactive ] || {
        printf 'status=fail\nreason=conflicting shipped IBus service active\n' > "$STATE/result"
        sync; say "fcitx-probe=fail reason=conflicting shipped IBus service active"; say done; exit 0
    }
    grep -qx 'Default Layout=fr' "$CFG/fcitx5/profile" 2>/dev/null && \
        grep -qx 'Name=keyboard-fr' "$CFG/fcitx5/profile" 2>/dev/null && \
        grep -qx 'DefaultIM=pinyin' "$CFG/fcitx5/profile" 2>/dev/null || {
            printf 'status=fail\nreason=shipped Fcitx profile mismatch\n' > "$STATE/result"
            sync; say "fcitx-probe=fail reason=shipped Fcitx profile mismatch"; say done; exit 0
        }
else
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
fi
chown -R "$desktop_user" "$RUNTIME"

ibus_stopped=0; fcitx_pid=; mouse_pid=; mouse_b_pid=; firefox_pid=; password_pid=; http_pid=; password_http_pid=; password_summary=; password_focus=; connection_probe_pid=; connection_b_probe_pid=; connection_b_done=; final_state=fail; final_reason=unexpected_exit
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
    if [ "$PRODUCTION" = 1 ]; then
        as_desktop env -u DISPLAY WAYLAND_DISPLAY="$wayland" "$@"
    else
        as_desktop env -u DISPLAY GTK_IM_MODULE=fcitx GDK_BACKEND=wayland WAYLAND_DISPLAY="$wayland" \
            XDG_CONFIG_HOME="$CFG" XDG_CACHE_HOME="$CACHE" XDG_DATA_HOME="$DATA" "$@"
    fi
}
fcitx_owner_pid() {
    as_fcitx_desktop busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus GetConnectionUnixProcessID s org.fcitx.Fcitx5 2>/dev/null \
        | awk '$1=="u" && $2 ~ /^[0-9]+$/ {print $2}'
}
fcitx_owner_name() {
    as_fcitx_desktop busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus GetNameOwner s org.fcitx.Fcitx5 2>/dev/null \
        | awk '$1=="s" && $2 ~ /^"\:[0-9]+\.[0-9]+"$/ {gsub(/"/, "", $2); print $2}'
}
verify_fcitx_identity() { # expected unique D-Bus owner
    expected_owner=$1
    [ "$(fcitx_owner_name)" = "$expected_owner" ] \
        && [ "$(fcitx_owner_pid)" = "$fcitx_pid" ] \
        && [ "$(awk '{print $22}' "/proc/$fcitx_pid/stat" 2>/dev/null)" = "$fcitx_start" ] \
        && [ "$(readlink "/proc/$fcitx_pid/exe" 2>/dev/null)" = /usr/bin/fcitx5 ] \
        && [ "$(tr '\0' ' ' < "/proc/$fcitx_pid/cmdline" 2>/dev/null)" = '/usr/bin/fcitx5 -D ' ]
}

select_exact_pinyin() { # focused application name
    selected_for=$1; i=0
    while [ "$i" -lt 30 ]; do
        if as_fcitx_desktop fcitx5-remote -s pinyin >/dev/null 2>&1 && \
           [ "$(as_fcitx_desktop fcitx5-remote -n 2>/dev/null)" = pinyin ]; then
            printf '%s=pinyin\n' "$selected_for" >> "$STATE/selected-engines.txt"
            return 0
        fi
        sleep 1; i=$((i+1))
    done
    return 1
}

restore_and_finish() {
    cleanup_ok=1
    password_server_unit_stopped=0
    if [ -n "${connection_b_probe_pid:-}" ]; then
        : > "$connection_b_done"
        wait "$connection_b_probe_pid" || cleanup_ok=0
        connection_b_probe_pid=
    fi
    if [ -n "${connection_probe_pid:-}" ]; then
        : > "$conn_done"
        wait "$connection_probe_pid" || cleanup_ok=0
        connection_probe_pid=
    fi
    : > "$STATE/cleanup-units.txt"
    for u in auros-profiletest-fcitx-password-http auros-profiletest-fcitx-password auros-profiletest-fcitx-firefox \
             auros-profiletest-fcitx-mousepad-b auros-profiletest-fcitx-mousepad \
             auros-profiletest-fcitx-daemon; do
        unit_cleanup_ok=1
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
            cleanup_ok=0; unit_cleanup_ok=0
        elif [ "$load_state" = not-found ]; then
            [ "$cgroup_empty" = yes ] || { cleanup_ok=0; unit_cleanup_ok=0; }
        else
            [ "$stop_rc" = 0 ] || { cleanup_ok=0; unit_cleanup_ok=0; }
            case "$unit_state" in inactive|failed) ;; *) cleanup_ok=0; unit_cleanup_ok=0 ;; esac
        fi
        [ "$cgroup_empty" = yes ] || { cleanup_ok=0; unit_cleanup_ok=0; }
        [ "$u" != auros-profiletest-fcitx-password-http ] \
            || password_server_unit_stopped=$unit_cleanup_ok
    done
    [ -z "${http_pid:-}" ] || { kill "$http_pid" 2>/dev/null || true; wait "$http_pid" 2>/dev/null || true; }
    password_server_pid_gone=1
    [ -z "${password_http_pid:-}" ] || wait_pid_gone "$password_http_pid" \
        || { cleanup_ok=0; password_server_pid_gone=0; }
    if [ "$password_server_unit_stopped" = 1 ] && [ "$password_server_pid_gone" = 1 ]; then
        as_desktop rm -f ${password_focus:+"$password_focus"} \
            ${password_summary:+"$password_summary"} "$RUNTIME/password-server.stderr" \
            || cleanup_ok=0
        [ -z "${password_focus:-}" ] || [ ! -e "$password_focus" ] || cleanup_ok=0
        [ -z "${password_summary:-}" ] || [ ! -e "$password_summary" ] || cleanup_ok=0
        [ ! -e "$RUNTIME/password-server.stderr" ] || cleanup_ok=0
    else
        cleanup_ok=0
    fi
    password_http_pid=
    [ -z "${connection_probe_pid:-}" ] || { kill "$connection_probe_pid" 2>/dev/null || true; wait "$connection_probe_pid" 2>/dev/null || true; }
    [ -z "${connection_b_probe_pid:-}" ] || { kill "$connection_b_probe_pid" 2>/dev/null || true; wait "$connection_b_probe_pid" 2>/dev/null || true; }
    cleanup_fcitx_pid=$fcitx_pid
    [ "$PRODUCTION" = 0 ] || cleanup_fcitx_pid=
    for p in "$password_pid" "$firefox_pid" "$mouse_b_pid" "$mouse_pid" "$cleanup_fcitx_pid"; do
        [ -z "$p" ] || wait_pid_gone "$p" || cleanup_ok=0
    done
    ibus_start_rc=not-needed
    if [ "$PRODUCTION" = 0 ] && [ "$ibus_stopped" = 1 ]; then
        ibus_start_rc=0
        as_desktop systemctl --user start auros-ibus.service >/dev/null 2>&1 \
            || { ibus_start_rc=$?; cleanup_ok=0; }
    fi
    ordinary=0
    ibus_active_seen=no; ibus_identity_seen=no; ibus_identity_stable_seen=no; ibus_pinyin_seen=no
    if [ "$PRODUCTION" = 1 ]; then
        service_pid=$(as_desktop systemctl --user show -p MainPID --value auros-fcitx.service 2>/dev/null)
        owner=$(fcitx_owner_pid)
        proc_env=$(tr '\0' '\n' < "/proc/$service_pid/environ" 2>/dev/null || true)
        if [ "$(as_desktop systemctl --user is-active auros-fcitx.service 2>/dev/null)" = active ] && \
           [ "$service_pid" = "$fcitx_pid" ] && [ "$owner" = "$fcitx_pid" ] && \
           [ "$(awk '{print $22}' "/proc/$service_pid/stat" 2>/dev/null)" = "$fcitx_start" ] && \
           [ "$(readlink "/proc/$service_pid/exe" 2>/dev/null)" = /usr/bin/fcitx5 ] && \
           [ "$(tr '\0' ' ' < "/proc/$service_pid/cmdline" 2>/dev/null)" = '/usr/bin/fcitx5 -D ' ] && \
           ! printf '%s\n' "$proc_env" | grep -Eq '^(DISPLAY|WAYLAND_DISPLAY)=' && \
           [ "$(as_fcitx_desktop fcitx5-remote -m pinyin 2>/dev/null)" = pinyin ] && \
           [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = inactive ]; then
            ordinary=1
        fi
    else
      ibus_deadline=$(( $(monotonic_ms) + 10000 ))
      while [ "$(monotonic_ms)" -lt "$ibus_deadline" ]; do
        active_before=; active_before_rc=0
        active_before=$(as_desktop_until "$ibus_deadline" systemctl --user is-active \
            auros-ibus.service 2>/dev/null) || active_before_rc=$?
        if [ "$active_before_rc" = 0 ] && [ "$active_before" = active ]; then
            ibus_active_seen=yes
            ipid=; ipid_rc=0
            ipid=$(as_desktop_until "$ibus_deadline" systemctl --user show \
                -p MainPID --value auros-ibus.service 2>/dev/null) || ipid_rc=$?
            identity_before=; identity_before_rc=0
            identity_before=$(ibus_identity_snapshot "$ibus_deadline" "$ipid" \
                2>/dev/null) || identity_before_rc=$?
            if [ "$ipid_rc" = 0 ] && [ "$identity_before_rc" = 0 ] && \
               [ -n "$identity_before" ]; then
                ibus_identity_seen=yes
                engines=; engines_rc=0
                engines=$(as_desktop_until "$ibus_deadline" env DISPLAY="$display" \
                    WAYLAND_DISPLAY="$wayland" GTK_IM_MODULE=ibus \
                    ibus list-engine --name-only 2>/dev/null) || engines_rc=$?
                if [ "$engines_rc" = 0 ] && printf '%s\n' "$engines" | grep -Fqx pinyin; then
                    ibus_pinyin_seen=yes
                    active_after=; active_after_rc=0
                    active_after=$(as_desktop_until "$ibus_deadline" systemctl --user \
                        is-active auros-ibus.service 2>/dev/null) || active_after_rc=$?
                    ipid_after=; ipid_after_rc=0
                    ipid_after=$(as_desktop_until "$ibus_deadline" systemctl --user show \
                        -p MainPID --value auros-ibus.service 2>/dev/null) || ipid_after_rc=$?
                    identity_after=; identity_after_rc=0
                    identity_after=$(ibus_identity_snapshot "$ibus_deadline" "$ipid_after" \
                        2>/dev/null) || identity_after_rc=$?
                    if [ "$active_after_rc" = 0 ] && [ "$ipid_after_rc" = 0 ] && \
                       [ "$identity_after_rc" = 0 ] && [ "$active_after" = active ] && \
                       [ "$ipid_after" = "$ipid" ] && \
                       [ -n "$identity_after" ] && [ "$identity_after" = "$identity_before" ]; then
                        ibus_identity_stable_seen=yes; ordinary=1; break
                    fi
                fi
            fi
        fi
        sleep .25
      done
    fi
    if ! profile_snapshot end; then
        cleanup_ok=0
        final_state=fail
        final_reason=bounded_Fcitx_profile_end_snapshot_failed
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
    printf 'source=%s\n' "$( [ "$PRODUCTION" = 1 ] && echo shipped-fcitx || echo retained-ibus-conversion )" \
        >> "$STATE/restore.txt"
    printf 'service_start_rc=%s\nservice_active_seen=%s\nservice_identity_seen=%s\n' \
        "$ibus_start_rc" "$ibus_active_seen" "$ibus_identity_seen" >> "$STATE/restore.txt"
    printf 'service_identity_stable_seen=%s\npinyin_engine_seen=%s\n' \
        "$ibus_identity_stable_seen" "$ibus_pinyin_seen" >> "$STATE/restore.txt"
    if [ "$ordinary" != 1 ] || [ "$unchanged" != yes ] || [ "$cleanup_ok" != 1 ]; then
        final_state=fail
        if [ "$PRODUCTION" = 1 ]; then final_reason=cleanup_or_shipped_Fcitx_readiness_failed
        else final_reason=cleanup_or_IBus_restoration_failed
        fi
    fi
    printf 'status=%s\nreason=%s\n' "$final_state" "$final_reason" > "$STATE/result"
    sync
    if [ "$SECURITY" = 1 ]; then
        round_dir="$STATE/round$security_round"; mkdir -p "$round_dir"
        find "$STATE" -maxdepth 1 -type f ! -name security-next-round -exec cp -p {} "$round_dir/" \;
        if [ "$security_round" = 1 ] && [ "$final_state" = pass ]; then
            printf '2\n' > "$STATE/security-next-round"
            sync; say "fcitx-security-round=1 rebooting"; systemctl reboot --no-wall
            exit 0
        fi
    fi
    sync
    say "fcitx-probe=$final_state evidence=fcitx reason=$final_reason"; say done; exit 0
}
fail() { final_state=fail; final_reason=$(printf '%s' "$*" | tr ' ' '_'); restore_and_finish; }
trap 'final_state=fail; final_reason=signal; restore_and_finish' HUP INT TERM

profile_snapshot start || fail "bounded Fcitx profile start snapshot failed"

actno=$(cat "$STATE/action-sequence" 2>/dev/null || echo 0)
case "$actno" in ''|*[!0-9]*) actno=0;; esac
act() {
    actno=$((actno+1)); printf '%s\n' "$actno" > "$STATE/action-sequence"
    id=$((100+actno)); say "fcitx-do=$id $*"; i=0
    while [ "$i" -lt 180 ]; do
        [ -f "$HOST/failed-$id" ] && return 1
        ack=$(cat "$HOST/acks" 2>/dev/null || echo 0); case "$ack" in *[!0-9]*|'') ack=0;; esac
        [ "$ack" -ge "$id" ] && return 0
        sleep 1; i=$((i+1))
    done
    return 1
}

if [ "$SECURITY" = 1 ] && [ "$security_round" = 2 ]; then
    if [ "$PRODUCTION" = 1 ]; then
        wait_active auros-fcitx.service || fail "shipped Fcitx did not autostart after reboot"
        [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = inactive ] \
            || fail "conflicting shipped IBus autostarted after reboot"
        printf 'shipped_fcitx_autostart_ready=yes\n' > "$STATE/reboot.txt"
    else
        if pgrep -u "$desktop_uid" -x fcitx5 >/dev/null 2>&1 || \
           [ "$(as_desktop systemctl --user is-active auros-profiletest-fcitx-daemon 2>/dev/null)" = active ]; then
            fail "Fcitx autostarted before explicit round two launch"
        fi
        printf 'fcitx_autostart_before_explicit_launch=no\n' > "$STATE/reboot.txt"
    fi
fi

if [ "$PRODUCTION" = 1 ]; then
    [ "$(as_desktop systemctl --user is-active auros-ibus.service 2>/dev/null)" = inactive ] \
        || fail "conflicting shipped IBus remained active"
    wait_active auros-fcitx.service || fail "shipped Fcitx did not stay active"
    fcitx_pid=$(as_desktop systemctl --user show -p MainPID --value auros-fcitx.service 2>/dev/null)
else
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
fi
case "$fcitx_pid" in ''|0|*[!0-9]*) fail "owned Fcitx PID unavailable";; esac
fcitx_start=$(awk '{print $22}' "/proc/$fcitx_pid/stat" 2>/dev/null)
case "$fcitx_start" in ''|*[!0-9]*) fail "owned Fcitx start time unavailable";; esac
[ "$(readlink "/proc/$fcitx_pid/exe" 2>/dev/null)" = /usr/bin/fcitx5 ] || fail "owned Fcitx executable mismatch"
[ "$(tr '\0' ' ' < "/proc/$fcitx_pid/cmdline" 2>/dev/null)" = '/usr/bin/fcitx5 -D ' ] \
    || fail "owned Fcitx arguments mismatch"
owner=$(fcitx_owner_pid); [ "$owner" = "$fcitx_pid" ] || fail "Fcitx D-Bus owner mismatch"
{
    printf 'pid=%s\nexe=/usr/bin/fcitx5\ncmdline=/usr/bin/fcitx5 -D\n' "$fcitx_pid"
    [ "$PRODUCTION" = 0 ] || printf 'unit=auros-fcitx.service\nibus_inactive=yes\n'
    [ -z "$boot_id" ] || printf 'boot_id=%s\n' "$boot_id"
    printf 'start_time=%s\n' "$fcitx_start"
    printf 'config_hash=%s\n' "$(sha256sum "$CFG/fcitx5/profile" | awk '{print $1}')"
    tr '\0' '\n' < "/proc/$fcitx_pid/environ" | grep -q '^DISPLAY=' && echo display_unset=no || echo display_unset=yes
    tr '\0' '\n' < "/proc/$fcitx_pid/environ" | grep -q '^WAYLAND_DISPLAY=' && echo wayland_display_unset=no || echo wayland_display_unset=yes
} > "$STATE/fcitx.process"

# CurrentInputMethod and SetCurrentIM operate on Fcitx's most recent input
# context. Before an application focuses a text field there is no such context,
# so -n is empty and -s is intentionally a no-op. Prove the entry exists now;
# select and verify it separately after each application gains focus below.
i=0
while [ "$i" -lt 30 ]; do
    [ "$(as_fcitx_desktop fcitx5-remote -m pinyin 2>/dev/null)" = pinyin ] && break
    sleep 1; i=$((i+1))
done
[ "$i" -lt 30 ] || fail "exact pinyin engine unavailable"

run_app() { # app file/url trace unit
    app=$1; target=$2; trace=$3; unit=$4
    case "$app" in
      mousepad) command="mousepad $target";;
      mousepad-isolated) command="/usr/bin/mousepad --disable-server $target";;
      firefox) command="firefox --no-remote --new-instance --profile $RUNTIME/firefox-profile $target";;
      *) return 1;;
    esac
    if [ "$PRODUCTION" = 1 ]; then
        # GTK_IM_MODULE and the Fcitx configuration must come from the image.
        # shellcheck disable=SC2086
        as_desktop systemd-run --user --quiet --collect --unit="$unit" --property=Type=exec \
            --property="StandardError=append:$trace" -- /usr/bin/env -u DISPLAY \
            HOME="$desktop_home" XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" \
            GDK_BACKEND=wayland WAYLAND_DISPLAY="$wayland" WAYLAND_DEBUG=client \
            $command >/dev/null 2>&1
    else
        # shellcheck disable=SC2086
        as_desktop systemd-run --user --quiet --collect --unit="$unit" --property=Type=exec \
            --property="StandardError=append:$trace" -- /usr/bin/env -u DISPLAY \
            HOME="$desktop_home" XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" \
            GDK_BACKEND=wayland GTK_IM_MODULE=fcitx WAYLAND_DISPLAY="$wayland" \
            XDG_CONFIG_HOME="$CFG" XDG_CACHE_HOME="$CACHE" XDG_DATA_HOME="$DATA" \
            WAYLAND_DEBUG=client $command >/dev/null 2>&1
    fi
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
select_exact_pinyin mousepad || fail "Mousepad Pinyin select failed"
if [ "$SECURITY" = 1 ]; then
    shell_main_pid=$(systemctl show -p MainPID --value aurshell.service 2>/dev/null)
    shell_control_group=$(systemctl show -p ControlGroup --value aurshell.service 2>/dev/null)
    shell_invocation_id=$(systemctl show -p InvocationID --value aurshell.service 2>/dev/null)
    case "$shell_main_pid" in ''|0|*[!0-9]*) fail "AurWL service MainPID unavailable";; esac
    case "$shell_control_group" in /*) :;; *) fail "AurWL service control group unavailable";; esac
    case "$shell_invocation_id" in
      [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) :;;
      *) fail "AurWL service invocation unavailable";;
    esac
    display_path="/run/user/$desktop_uid/$wayland"
    case "$wayland" in /*) display_path=$wayland;; esac
    conn_ready="$RUNTIME/connection.ready"; conn_done="$RUNTIME/connection.done"
    rm -f "$conn_ready" "$conn_done"
    python3 /usr/lib/auros-profiletest/fcitx_connection_probe.py \
        --app-pid "$mouse_pid" --service-main-pid "$shell_main_pid" \
        --service-control-group "$shell_control_group" \
        --service-invocation-id "$shell_invocation_id" --expected-uid "$desktop_uid" \
        --display-path "$display_path" \
        --trace "$mouse_trace" --ready "$conn_ready" --done "$conn_done" \
        --output "$STATE/mousepad-connection.txt" > "$RUNTIME/connection.stdout" 2> "$RUNTIME/connection.stderr" &
    connection_probe_pid=$!
    i=0; while [ "$i" -lt 30 ] && [ ! -f "$conn_ready" ]; do
        kill -0 "$connection_probe_pid" 2>/dev/null || break
        sleep .1; i=$((i+1))
    done
    [ -f "$conn_ready" ] || fail "Mousepad connection identity unavailable"
fi
act baseline fcitx-mousepad-candidate || fail "Mousepad candidate baseline failed"
boundary=$(wc -c < "$mouse_trace" | tr -d ' ')
act type-pinyin || fail "Mousepad typing failed"; sleep 1
act candidate-shot fcitx-mousepad-candidate fcitx-mousepad-candidate \
    || fail "Mousepad candidate was not visibly different"
python3 /usr/lib/auros-profiletest/fcitx_wayland_trace.py "$mouse_trace" --after-byte "$boundary" \
    > "$STATE/mousepad-popup.txt" 2> "$STATE/mousepad-popup.stderr" || fail "Mousepad parented-popup syntax missing"
trace_has_no_input_text "$mouse_trace" || fail "Mousepad protocol trace retained input text"
if [ "$SECURITY" = 1 ]; then
    away_boundary=$(wc -c < "$mouse_trace" | tr -d ' ')
    mouse_b_file="$RUNTIME/mousepad-b.txt"; : > "$mouse_b_file"
    i=1
    while [ "$i" -le 24 ]; do
        printf '%02d ' "$i" >> "$mouse_b_file"
        j=0; while [ "$j" -lt $((i * 2)) ]; do printf '█' >> "$mouse_b_file"; j=$((j + 1)); done
        printf '\n' >> "$mouse_b_file"
        i=$((i + 1))
    done
    mouse_b_marker_hash=$(sha256sum "$mouse_b_file" | awk '{print $1}')
    [ "$mouse_b_marker_hash" = 665cfe6eae08731ed61132f9c8ee8bea8b87da01eba84cc2215180b235a46fa1 ] \
        || fail "second Mousepad marker template mismatch"
    chown "$desktop_user" "$mouse_b_file"
    mouse_b_trace="$RUNTIME/mousepad-b.trace"; : > "$mouse_b_trace"; chown "$desktop_user" "$mouse_b_trace"
    mouse_b_evidence="fcitx-round$security_round-mousepad-b"
    act baseline "$mouse_b_evidence" || fail "second Mousepad baseline failed"
    run_app mousepad-isolated "$mouse_b_file" "$mouse_b_trace" auros-profiletest-fcitx-mousepad-b \
        || fail "second Mousepad did not start"
    wait_active auros-profiletest-fcitx-mousepad-b || fail "second Mousepad did not stay active"
    mouse_b_pid=$(as_desktop systemctl --user show -p MainPID --value auros-profiletest-fcitx-mousepad-b 2>/dev/null)
    case "$mouse_b_pid" in ''|0|*[!0-9]*) fail "second Mousepad PID unavailable";; esac
    python3 - "$mouse_b_pid" "$mouse_b_file" "$mouse_b_marker_hash" "$STATE/mousepad-b.process" <<'PY' \
        || fail "second Mousepad process identity mismatch"
import hashlib, os, pathlib, sys
pid, target, marker_hash, output = sys.argv[1:]
root = pathlib.Path('/proc') / pid
try:
    exe = os.readlink(root / 'exe')
    argv = (root / 'cmdline').read_bytes().split(b'\0')
    fields = (root / 'stat').read_text(encoding='ascii').rsplit(')', 1)[1].split()
    env = dict(item.split(b'=', 1) for item in (root / 'environ').read_bytes().split(b'\0') if b'=' in item)
    actual_hash = hashlib.sha256(pathlib.Path(target).read_bytes()).hexdigest()
except (OSError, UnicodeDecodeError, ValueError, IndexError):
    raise SystemExit(1)
expected_argv = [b'/usr/bin/mousepad', b'--disable-server', os.fsencode(target), b'']
if (exe != '/usr/bin/mousepad' or argv != expected_argv or actual_hash != marker_hash
        or env.get(b'GDK_BACKEND') != b'wayland'
        or env.get(b'GTK_IM_MODULE') != b'fcitx'
        or not env.get(b'WAYLAND_DISPLAY') or b'DISPLAY' in env):
    raise SystemExit(1)
start = int(fields[19])
pathlib.Path(output).write_text(
    f'unit=auros-profiletest-fcitx-mousepad-b\npid={pid}\nstart_time={start}\n'
    f'exe={exe}\nargv=/usr/bin/mousepad --disable-server <marker-file>\n'
    f'marker_sha256={marker_hash}\ngdk_backend=wayland\ngtk_im_module=fcitx\n'
    f'wayland_display_set=yes\ndisplay_unset=yes\n', encoding='ascii')
PY
    connection_b_ready="$RUNTIME/connection-b.ready"; connection_b_done="$RUNTIME/connection-b.done"
    rm -f "$connection_b_ready" "$connection_b_done"
    python3 /usr/lib/auros-profiletest/fcitx_connection_probe.py \
        --app-pid "$mouse_b_pid" --service-main-pid "$shell_main_pid" \
        --service-control-group "$shell_control_group" \
        --service-invocation-id "$shell_invocation_id" --expected-uid "$desktop_uid" \
        --display-path "$display_path" --trace "$mouse_b_trace" \
        --ready "$connection_b_ready" --done "$connection_b_done" \
        --output "$STATE/mousepad-b-connection.txt" \
        > "$RUNTIME/connection-b.stdout" 2> "$RUNTIME/connection-b.stderr" &
    connection_b_probe_pid=$!
    i=0; while [ "$i" -lt 30 ] && [ ! -f "$connection_b_ready" ]; do
        kill -0 "$connection_b_probe_pid" 2>/dev/null || break
        sleep .1; i=$((i+1))
    done
    [ -f "$connection_b_ready" ] || fail "second Mousepad connection identity unavailable"
    i=0
    while [ "$i" -lt 30 ]; do
        if python3 /usr/lib/auros-profiletest/fcitx_wayland_trace.py "$mouse_b_trace" \
            --after-byte 0 --toplevel > "$STATE/mousepad-b-toplevel.tmp" \
            2> "$STATE/mousepad-b-toplevel.stderr"; then
            mv "$STATE/mousepad-b-toplevel.tmp" "$STATE/mousepad-b-toplevel.txt"
            break
        fi
        sleep .1; i=$((i+1))
    done
    [ "$i" -lt 30 ] || fail "second Mousepad mapped toplevel unavailable"
    act wait-change "$mouse_b_evidence" || fail "second Mousepad distinct content was not visible"
    act click-change "$mouse_b_evidence" || fail "second Mousepad focus failed"
    act shot "fcitx-round$security_round-focus-away" || fail "focus-away screenshot failed"
    b_input_boundary=$(wc -c < "$mouse_trace" | tr -d ' ')
    # French AZERTY: physical Q is logical A, so Ctrl+Q selects all.
    act chord ctrl q || fail "second Mousepad marker select-all failed"
    act key backspace || fail "second Mousepad marker clear failed"
    act key spc || fail "second Mousepad sentinel input failed"
    act chord ctrl s || fail "second Mousepad sentinel save failed"
    wait_hex_file "$mouse_b_file" 20 || fail "second Mousepad sentinel missing"
    [ ! -s "$mouse_file" ] || fail "first Mousepad changed while focus was away"
    : > "$connection_b_done"
    wait "$connection_b_probe_pid" || fail "second Mousepad connection interval failed"
    connection_b_probe_pid=
    grep -qx 'status=pass' "$STATE/mousepad-b-connection.txt" \
        || fail "second Mousepad connection interval missing"
    as_desktop systemctl --user stop auros-profiletest-fcitx-mousepad-b >/dev/null 2>&1 \
        || fail "second Mousepad stop failed"
    wait_pid_gone "$mouse_b_pid" || fail "second Mousepad PID remained"
    mouse_b_pid=
    return_boundary=$(wc -c < "$mouse_trace" | tr -d ' ')
    act click-change fcitx-mousepad || fail "first Mousepad return focus failed"
    act key esc || fail "first Mousepad preedit cancel failed"
    # The profile uses AZERTY; physical Q is logical A.
    act chord ctrl q || fail "first Mousepad select-all failed"
    act key backspace || fail "first Mousepad clear failed"
    select_exact_pinyin "mousepad-round$security_round-return" || fail "return Pinyin select failed"
    act type-pinyin || fail "return Mousepad typing failed"; sleep 1
    python3 /usr/lib/auros-profiletest/fcitx_wayland_trace.py "$mouse_trace" \
        --after-byte "$boundary" --lifecycle --away-byte "$away_boundary" \
        --b-input-byte "$b_input_boundary" --return-byte "$return_boundary" \
        > "$STATE/focus-lifecycle.txt" \
        2> "$STATE/focus-lifecycle.stderr" || fail "Mousepad focus lifecycle failed"
    printf 'b_saved_sentinel=20\na_saved_file_unchanged=yes\n' >> "$STATE/focus-lifecycle.txt"
    printf '20\n' > "$STATE/focus-b-sentinel.hex"
    : > "$conn_done"
    wait "$connection_probe_pid" || fail "Mousepad connection interval failed"
    connection_probe_pid=
    grep -qx 'status=pass' "$STATE/mousepad-connection.txt" \
        || fail "Mousepad connection interval missing"
fi
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
<!doctype html><meta charset="utf-8"><style>html,body,form{width:100%;height:100%;margin:0}form{position:relative;background:#01dead}input{position:absolute;left:15%;top:calc(50% - 70px);width:70%;height:140px;font-size:48px}</style><form method="get" autocomplete="off"><input name="text" autocomplete="off" autofocus aria-label="Pinyin test"><button hidden>Submit</button></form>
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
select_exact_pinyin firefox || fail "Firefox Pinyin select failed"
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
if [ "$SECURITY" = 1 ]; then
    as_desktop systemctl --user stop auros-profiletest-fcitx-firefox >/dev/null 2>&1 \
        || fail "Firefox stop before password test failed"
    wait_pid_gone "$firefox_pid" || fail "Firefox PID remained before password test"
    firefox_pid=
    mkdir -p "$RUNTIME/password-profile"
    cp "$RUNTIME/firefox-profile/user.js" "$RUNTIME/password-profile/user.js"
    chown -R "$desktop_user" "$RUNTIME/password-profile"
    password_summary="$RUNTIME/password-summary"
    password_focus="$RUNTIME/password-focus"
    as_desktop rm -f "$password_summary" "$password_focus" \
        || fail "old reduced password evidence could not be removed"
    as_desktop systemd-run --user --quiet --collect --unit=auros-profiletest-fcitx-password-http \
        --property=Type=exec -- python3 /usr/lib/auros-profiletest/fcitx_password_server.py \
        --output "$password_summary" --focus-output "$password_focus" --port 8766 \
        >/dev/null 2>&1 || fail "password server did not start"
    wait_active auros-profiletest-fcitx-password-http || fail "password server did not stay active"
    password_http_pid=$(as_desktop systemctl --user show -p MainPID --value \
        auros-profiletest-fcitx-password-http 2>/dev/null)
    case "$password_http_pid" in ''|0|*[!0-9]*) fail "password server PID unavailable";; esac
    i=0; while [ "$i" -lt 30 ]; do
        python3 -c 'import urllib.request; urllib.request.urlopen("http://127.0.0.1:8766/", timeout=1).read(1)' \
            >/dev/null 2>&1 && break
        kill -0 "$password_http_pid" 2>/dev/null || break
        sleep .1; i=$((i+1))
    done
    [ "$i" -lt 30 ] || fail "password server unavailable"
    password_trace="$RUNTIME/password.trace"; : > "$password_trace"; chown "$desktop_user" "$password_trace"
    act baseline fcitx-password || fail "password page baseline failed"
    if [ "$PRODUCTION" = 1 ]; then
        as_desktop systemd-run --user --quiet --collect --unit=auros-profiletest-fcitx-password \
            --property=Type=exec --property="StandardError=append:$password_trace" -- \
            /usr/bin/env -u DISPLAY HOME="$desktop_home" XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" \
            GDK_BACKEND=wayland WAYLAND_DISPLAY="$wayland" WAYLAND_DEBUG=client \
            firefox --no-remote --new-instance --profile "$RUNTIME/password-profile" \
            http://127.0.0.1:8766/ >/dev/null 2>&1 || fail "password Firefox did not start"
    else
        as_desktop systemd-run --user --quiet --collect --unit=auros-profiletest-fcitx-password \
            --property=Type=exec --property="StandardError=append:$password_trace" -- \
            /usr/bin/env -u DISPLAY HOME="$desktop_home" XDG_RUNTIME_DIR="/run/user/$desktop_uid" \
            DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$desktop_uid/bus" \
            GDK_BACKEND=wayland GTK_IM_MODULE=fcitx WAYLAND_DISPLAY="$wayland" \
            XDG_CONFIG_HOME="$CFG" XDG_CACHE_HOME="$CACHE" XDG_DATA_HOME="$DATA" WAYLAND_DEBUG=client \
            firefox --no-remote --new-instance --profile "$RUNTIME/password-profile" \
            http://127.0.0.1:8766/ >/dev/null 2>&1 || fail "password Firefox did not start"
    fi
    wait_active auros-profiletest-fcitx-password || fail "password Firefox did not stay active"
    password_pid=$(as_desktop systemctl --user show -p MainPID --value auros-profiletest-fcitx-password 2>/dev/null)
    case "$password_pid" in ''|0|*[!0-9]*) fail "password Firefox PID unavailable";; esac
    tr '\0' '\n' < "/proc/$password_pid/environ" \
        | grep -E '^(GDK_BACKEND|GTK_IM_MODULE|WAYLAND_DISPLAY|DISPLAY|XDG_CONFIG_HOME)=' \
        > "$STATE/password.env"
    grep -qx 'GDK_BACKEND=wayland' "$STATE/password.env" \
        && grep -qx 'GTK_IM_MODULE=fcitx' "$STATE/password.env" \
        && ! grep -q '^DISPLAY=' "$STATE/password.env" \
        || fail "password Firefox native Wayland input environment missing"
    if [ "$PRODUCTION" = 1 ] && grep -q '^XDG_CONFIG_HOME=' "$STATE/password.env"; then
        fail "password Firefox did not inherit production configuration"
    fi
    act wait-form || fail "password form not visible"
    act baseline "fcitx-round$security_round-password-focus" || fail "password focus baseline failed"
    password_fcitx_owner=$(fcitx_owner_name)
    [ -n "$password_fcitx_owner" ] || fail "password Fcitx D-Bus owner unavailable"
    verify_fcitx_identity "$password_fcitx_owner" \
        || fail "Fcitx identity changed before password focus"
    printf 'pid=%s\nstart_time=%s\nexe=/usr/bin/fcitx5\ncmdline=/usr/bin/fcitx5 -D\ndbus_owner=%s\nidentity_before_sensitive=yes\n' \
        "$fcitx_pid" "$fcitx_start" "$password_fcitx_owner" \
        > "$STATE/service-owner-sensitive.txt"
    password_boundary=$(wc -c < "$password_trace" | tr -d ' ')
    [ ! -e "$password_focus" ] || fail "password focused before injected click"
    act click-form || fail "password field focus failed"
    i=0; while [ "$i" -lt 30 ] && ! grep -qx 'focus_event_observed=yes' \
        "$password_focus" 2>/dev/null; do sleep .1; i=$((i+1)); done
    [ "$i" -lt 30 ] || fail "password focus event unavailable"
    act form-shot "fcitx-round$security_round-password-focused" \
        || fail "focused password form was not visible"
    password_input_boundary=$(wc -c < "$password_trace" | tr -d ' ')
    act type-pinyin || fail "synthetic password input failed"
    act shot "fcitx-round$security_round-password-masked" || fail "masked password screenshot failed"
    password_screenshot_boundary=$(wc -c < "$password_trace" | tr -d ' ')
    password_submit_boundary=$(wc -c < "$password_trace" | tr -d ' ')
    act key ret || fail "synthetic password submit failed"
    i=0; while [ "$i" -lt 30 ] && [ ! -s "$password_summary" ]; do sleep .1; i=$((i+1)); done
    [ "$i" -lt 30 ] || fail "password result unavailable"
    grep -qx 'ascii_value_exact=yes' "$password_summary" \
        && grep -qx 'composition_start_count=0' "$password_summary" \
        && grep -qx 'composition_update_count=0' "$password_summary" \
        && grep -qx 'composition_end_count=0' "$password_summary" \
        && grep -qx 'composition_input_count=0' "$password_summary" \
        || fail "password field composition suppression failed"
    cat "$password_focus" > "$STATE/password-focus.txt"
    cat "$password_summary" > "$STATE/password-summary.txt"
    verify_fcitx_identity "$password_fcitx_owner" \
        || fail "Fcitx identity changed during password test"
    printf 'identity_after_sensitive=yes\n' >> "$STATE/service-owner-sensitive.txt"
    python3 /usr/lib/auros-profiletest/fcitx_wayland_trace.py "$password_trace" \
        --after-byte "$password_boundary" --input-byte "$password_input_boundary" \
        --screenshot-byte "$password_screenshot_boundary" \
        --submit-byte "$password_submit_boundary" --no-popup \
        > "$STATE/password-popup-events.txt" \
        2> "$STATE/password-suppression.stderr" || fail "password field popup suppression failed"
    grep -E '^(candidate_popup_(creations|maps|configures|grabs)|layer_shell_objects|observed_candidate_popup)=' \
        "$STATE/password-popup-events.txt" > "$STATE/password-suppression.txt" \
        || fail "reduced password popup summary missing"
    cat "$password_focus" >> "$STATE/password-suppression.txt"
    cat "$password_summary" >> "$STATE/password-suppression.txt"
    trace_has_no_input_text "$password_trace" || fail "password protocol trace retained input text"
    as_desktop systemctl --user stop auros-profiletest-fcitx-password-http >/dev/null 2>&1 \
        || fail "password server stop failed"
    wait_pid_gone "$password_http_pid" || fail "password server PID remained"
    password_http_pid=
    as_desktop rm -f "$password_focus" "$password_summary" "$RUNTIME/password-server.stderr"
    [ ! -e "$password_focus" ] && [ ! -e "$password_summary" ] \
        && [ ! -e "$RUNTIME/password-server.stderr" ] \
        || fail "password value or event logs remained"
    printf 'raw_password_focus_log_retained=no\nraw_password_value_log_retained=no\nraw_password_event_log_retained=no\n' \
        >> "$STATE/password-suppression.txt"
fi
printf 'service_owner_stable=yes\n' >> "$STATE/fcitx.process"
if [ "$SECURITY" = 1 ]; then
    final_state=pass; final_reason=bounded_connection_focus_password_round_passed
else
    final_state=pass; final_reason=composition_and_parented_popup_syntax_observed
fi
restore_and_finish
