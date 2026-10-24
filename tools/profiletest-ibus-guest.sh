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

record_process() { # pid output-prefix
    pid=$1; prefix=$2
    {
        printf 'pid=%s\n' "$pid"
        printf 'exe='; readlink "/proc/$pid/exe" 2>&1 || true
        printf 'cmdline='; tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true; printf '\n'
        sed -n 's/^\(Name\|Uid\|Gid\|PPid\):[[:space:]]*/\L\1=/p' "/proc/$pid/status" 2>/dev/null
    } > "$prefix.process"
    : > "$prefix.ancestry"
    depth=0
    while [ "$depth" -lt 12 ] && [ "$pid" -gt 1 ] 2>/dev/null; do
        name=$(cat "/proc/$pid/comm" 2>/dev/null || echo gone)
        ppid=$(sed -n 's/^PPid:[[:space:]]*//p' "/proc/$pid/status" 2>/dev/null)
        printf 'depth=%s pid=%s ppid=%s name=%s\n' \
            "$depth" "$pid" "${ppid:-unknown}" "$name" >> "$prefix.ancestry"
        case "$ppid" in ''|*[!0-9]*) break ;; esac
        pid=$ppid; depth=$((depth + 1))
    done
}

desktop_processes() { # executable basename
    wanted=$1
    for status in /proc/[0-9]*/status; do
        [ -r "$status" ] || continue
        uid=$(sed -n 's/^Uid:[[:space:]]*\([0-9]*\).*/\1/p' "$status")
        [ "$uid" = "$desktop_uid" ] || continue
        pid=${status#/proc/}; pid=${pid%/status}
        exe=$(readlink "/proc/$pid/exe" 2>/dev/null) || continue
        [ "${exe##*/}" = "$wanted" ] && printf '%s\n' "$pid"
    done
}

run_x11_panel_probe() {
    XOUT="$OUT/x11-panel"
    XRUN="$RUNTIME/x11-panel"
    rm -rf "$XOUT" "$XRUN"
    mkdir -p "$XOUT" "$XRUN"
    chown "$desktop_user" "$XRUN"
    result=complete
    restoration_failed=0
    probe_failure_kind=
    note() { printf '%s\n' "$*" >> "$XOUT/probe.log"; }
    incomplete() { result=incomplete; note "ERROR: $*"; }
    restoration_incomplete() {
        restoration_failed=1
        incomplete "$*"
    }
    inconclusive() {
        [ "$result" = complete ] && result=inconclusive
        note "INCONCLUSIVE: $*"
    }
    # One phase deadline bounds both inventories and every reply/event they
    # consume.  Individual helper calls get only the remaining time.
    probe_deadline=$(( $(cut -d. -f1 /proc/uptime) + 120 ))
    probe_remaining() {
        now=$(cut -d. -f1 /proc/uptime)
        remaining=$((probe_deadline - now))
        [ "$remaining" -gt 0 ] || return 1
        [ "$remaining" -le 60 ] || remaining=60
        printf '%s\n' "$remaining"
    }
    pid_gone() { [ -z "$1" ] || [ ! -d "/proc/$1" ]; }
    wait_pid_gone() {
        gone_pid=$1; gone_i=0
        while [ "$gone_i" -lt 30 ]; do
            pid_gone "$gone_pid" && return 0
            sleep 0.1; gone_i=$((gone_i + 1))
        done
        return 1
    }
    daemon_pid= panel_pid= retained_pid= mouse_x11_pid=

    note "diagnostic_only=yes"
    note "candidate_visibility_is_not_a_success_gate=yes"
    note "display=$display wayland=$wayland desktop_user=$desktop_user"
    desktop_processes ibus-daemon > "$XOUT/processes-before-daemon.txt"
    desktop_processes ibus-ui-gtk3 > "$XOUT/processes-before-panel.txt"

    as_desktop systemctl --user stop auros-ibus.service >/dev/null 2>&1 \
        || incomplete "could not stop the ordinary IBus service"
    i=0
    while [ "$i" -lt 20 ] && { [ -n "$(desktop_processes ibus-daemon)" ] || \
                                  [ -n "$(desktop_processes ibus-ui-gtk3)" ]; }; do
        sleep 0.25; i=$((i + 1))
    done
    [ -z "$(desktop_processes ibus-daemon)" ] \
        || incomplete "an old ibus-daemon remained before the isolated launch"
    [ -z "$(desktop_processes ibus-ui-gtk3)" ] \
        || incomplete "an old ibus-ui-gtk3 remained before the isolated launch"

    daemon_unit=auros-profiletest-ibus-x11-daemon
    panel_unit=auros-profiletest-ibus-x11-panel
    mouse_x11_unit=auros-profiletest-ibus-x11-mousepad
    as_desktop systemd-run --user --quiet --collect --unit="$daemon_unit" \
        --property=Type=exec \
        --property="StandardOutput=append:$XRUN/daemon.stdout" \
        --property="StandardError=append:$XRUN/daemon.stderr" \
        -- /usr/bin/ibus-daemon --replace --xim --panel=disable \
        || incomplete "could not launch ibus-daemon --panel=disable"
    wait_active "$daemon_unit" || incomplete "the panel-disabled daemon did not stay active"
    daemon_pid=$(as_desktop systemctl --user show -p MainPID --value "$daemon_unit" 2>/dev/null)
    case "$daemon_pid" in ''|0|*[!0-9]*) incomplete "panel-disabled daemon MainPID unavailable" ;;
      *)
        record_process "$daemon_pid" "$XOUT/daemon"
        grep -qx 'exe=/usr/bin/ibus-daemon' "$XOUT/daemon.process" \
            || incomplete "daemon MainPID is not /usr/bin/ibus-daemon"
        grep -qx 'cmdline=/usr/bin/ibus-daemon --replace --xim --panel=disable ' \
            "$XOUT/daemon.process" \
            || incomplete "daemon arguments were not exactly the isolated probe arguments"
        ;;
    esac
    bus_ready=0; i=0
    while [ "$i" -lt 20 ]; do
        if ibus_cmd ibus list-engine --name-only > "$XOUT/daemon-engine-ids.txt" \
                2> "$XOUT/daemon-engine-list.stderr"; then
            bus_ready=1; break
        fi
        sleep 0.25; i=$((i + 1))
    done
    if [ "$bus_ready" = 1 ]; then
        grep -Fqx pinyin "$XOUT/daemon-engine-ids.txt" \
            || incomplete "panel-disabled daemon did not expose pinyin"
    else
        incomplete "panel-disabled daemon did not answer on the real IBus bus"
    fi
    : > "$XOUT/panels-after-daemon.txt"
    default_panel_count=0; i=0
    while [ "$i" -lt 20 ]; do
        desktop_processes ibus-ui-gtk3 > "$XOUT/panels-after-daemon.sample"
        if [ -s "$XOUT/panels-after-daemon.sample" ]; then
            cp "$XOUT/panels-after-daemon.sample" "$XOUT/panels-after-daemon.txt"
            default_panel_count=$(wc -l < "$XOUT/panels-after-daemon.txt" | tr -d ' ')
            break
        fi
        sleep 0.25; i=$((i + 1))
    done
    rm -f "$XOUT/panels-after-daemon.sample"
    note "default_panel_quiescence_s=5"
    note "default_panel_count=$default_panel_count"
    [ "$default_panel_count" = 0 ] \
        || incomplete "ibus-daemon --panel=disable spawned a default panel"

    if [ "$default_panel_count" = 0 ] && \
       as_desktop systemd-run --user --quiet --collect --unit="$panel_unit" \
        --property=Type=exec --setenv=GDK_BACKEND=x11 \
        --property="StandardOutput=append:$XRUN/panel.stdout" \
        --property="StandardError=append:$XRUN/panel.stderr" \
        -- /usr/libexec/ibus-ui-gtk3; then
        wait_active "$panel_unit" || incomplete "the exact X11 panel did not stay active"
    else
        incomplete "could not launch exact /usr/libexec/ibus-ui-gtk3"
    fi
    panel_pid=$(as_desktop systemctl --user show -p MainPID --value "$panel_unit" 2>/dev/null)
    case "$panel_pid" in ''|0|*[!0-9]*) incomplete "X11 panel MainPID unavailable" ;;
      *)
        record_process "$panel_pid" "$XOUT/panel-start"
        panel_exe=$(readlink "/proc/$panel_pid/exe" 2>/dev/null || true)
        [ "$panel_exe" = /usr/libexec/ibus-ui-gtk3 ] \
            || incomplete "panel MainPID is not the exact ibus-ui-gtk3 executable"
        sleep 3
        retained_pid=$(as_desktop systemctl --user show -p MainPID --value "$panel_unit" 2>/dev/null)
        note "panel_start_pid=$panel_pid panel_retained_pid=$retained_pid"
        [ "$retained_pid" = "$panel_pid" ] && [ -d "/proc/$panel_pid" ] \
            || incomplete "ibus-ui-gtk3 did not retain its spawned PID"
        ;;
    esac

    mouse_x11_file="$XRUN/mousepad.txt"
    : > "$mouse_x11_file"
    as_desktop systemd-run --user --quiet --collect --unit="$mouse_x11_unit" \
        --property=Type=exec --setenv=GDK_BACKEND=wayland \
        -- mousepad "$mouse_x11_file" >/dev/null 2>&1 \
        || incomplete "native Wayland Mousepad did not start"
    wait_active "$mouse_x11_unit" || incomplete "native Wayland Mousepad did not stay active"
    mouse_x11_pid=$(as_desktop systemctl --user show -p MainPID --value "$mouse_x11_unit" 2>/dev/null)
    case "$mouse_x11_pid" in ''|0|*[!0-9]*) incomplete "native Mousepad MainPID unavailable" ;;
      *)
        tr '\0' '\n' < "/proc/$mouse_x11_pid/environ" 2>/dev/null \
            | grep -E '^(GDK_BACKEND|WAYLAND_DISPLAY|DISPLAY)=' > "$XOUT/mousepad.env" || true
        grep -qx 'GDK_BACKEND=wayland' "$XOUT/mousepad.env" \
            || incomplete "Mousepad was not explicitly forced to native Wayland"
        ;;
    esac
    act wait-change "x11-probe-mousepad" \
        || incomplete "native Wayland Mousepad did not map visibly"
    act click-change "x11-probe-mousepad" \
        || incomplete "native Wayland Mousepad was not focused"
    select_ibus_engine pinyin x11-panel \
        || incomplete "pinyin could not be selected for the X11 panel probe"
    act shot ibus-x11-before || incomplete "pre-probe screenshot failed"
    if [ -n "${panel_pid:-}" ] && [ -d "/proc/$panel_pid" ]; then
        if xres_timeout=$(probe_remaining); then
            as_desktop python3 /usr/lib/auros-profiletest/x11_xres_probe.py \
                --display "$display" --pid "$panel_pid" --timeout "$xres_timeout" \
                > "$XOUT/xres-before.json" 2> "$XOUT/xres-before.stderr" \
                || incomplete "XRes inventory before typing failed"
        else
            incomplete "XRes whole-probe deadline expired before baseline"
        fi
    fi
    act type-pinyin || incomplete "host did not type Pinyin preedit"
    act shot ibus-x11-preedit || incomplete "preedit screenshot failed"
    if [ -n "${panel_pid:-}" ] && [ -d "/proc/$panel_pid" ]; then
        record_process "$panel_pid" "$XOUT/panel-after-preedit"
        if xres_timeout=$(probe_remaining); then
            as_desktop python3 /usr/lib/auros-profiletest/x11_xres_probe.py \
                --display "$display" --pid "$panel_pid" --timeout "$xres_timeout" \
                > "$XOUT/xres-preedit.json" 2> "$XOUT/xres-preedit.stderr" \
                || incomplete "XRes inventory during preedit failed"
        else
            incomplete "XRes whole-probe deadline expired before preedit"
        fi
        if [ -s "$XOUT/xres-preedit.json" ] && ! python3 - "$XOUT/xres-preedit.json" "$panel_pid" \
                > "$XOUT/candidate-summary.txt" <<'EOPY'
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
pid = int(sys.argv[2])
major, minor = map(int, d["xres_version"].split("."))
if (major, minor) < (1, 2) or d.get("wanted_pid") != pid:
    raise SystemExit("XRes version or requested PID mismatch")
w = d.get("windows", [])
if any(x.get("pid") != pid for x in w):
    raise SystemExit("server returned a window with a different LOCAL_CLIENT_PID")
clients = [x for x in d.get("local_clients", []) if x.get("pid") == pid]
if len(clients) > 1:
    raise SystemExit("server returned multiple LOCAL_CLIENT_PID records")
mapped = [x for x in w if x.get("mapped")]
override = [x for x in mapped if x.get("override_redirect")]
state = "mapped" if mapped else "identity-only" if len(clients) == 1 else "inconclusive"
print("xres_version=%s" % d["xres_version"])
print("identity_state=%s" % state)
print("server_local_client_pid_records=%d" % len(clients))
print("server_pid_windows=%d" % len(w))
print("mapped_server_pid_windows=%d" % len(mapped))
print("mapped_override_redirect_windows=%d" % len(override))
for x in mapped:
    g = x["absolute"]
    print("mapped xid=%s override_redirect=%s x=%s y=%s width=%s height=%s pid=%s" %
          (x["xid"], str(x["override_redirect"]).lower(), g["x"], g["y"],
           g["width"], g["height"], x["pid"]))
EOPY
        then
            incomplete "XRes LOCAL_CLIENT_PID evidence was inconsistent"
        fi
        if [ -s "$XOUT/candidate-summary.txt" ]; then
            cat "$XOUT/candidate-summary.txt" >> "$XOUT/probe.log"
            grep -qx 'identity_state=inconclusive' "$XOUT/candidate-summary.txt" \
                && inconclusive "XRes found neither a mapped panel window nor one server PID record"
        else
            incomplete "XRes candidate classification unavailable"
        fi
    fi

    for cleanup_unit in "$mouse_x11_unit" "$panel_unit" "$daemon_unit"; do
        if ! as_desktop systemctl --user stop "$cleanup_unit" >/dev/null 2>&1; then
            restoration_incomplete "could not stop transient unit $cleanup_unit"
        fi
        cleanup_state=$(as_desktop systemctl --user is-active "$cleanup_unit" 2>/dev/null || true)
        note "cleanup_unit=$cleanup_unit active_state=${cleanup_state:-unloaded}"
        case "$cleanup_state" in inactive|failed|unknown|'') ;;
          *) restoration_incomplete "transient unit $cleanup_unit remained $cleanup_state" ;;
        esac
    done
    for cleanup_pid in "$mouse_x11_pid" "$panel_pid" "$daemon_pid"; do
        [ -n "$cleanup_pid" ] || continue
        if wait_pid_gone "$cleanup_pid"; then
            note "cleanup_pid=$cleanup_pid gone=yes"
        else
            restoration_incomplete "transient PID $cleanup_pid remained after unit stop"
        fi
    done
    # Stop first so stdio buffers and journald pipes are closed before their
    # files are copied out of the desktop-owned runtime directory.
    cp -a "$XRUN/." "$XOUT/runtime-output/" 2>/dev/null || true
    as_desktop systemctl --user start auros-ibus.service >/dev/null 2>&1 \
        || restoration_incomplete "ordinary IBus service did not restart"
    wait_active auros-ibus.service \
        || restoration_incomplete "ordinary IBus service was not active after probe cleanup"
    shipped_pid=$(as_desktop systemctl --user show -p MainPID --value \
        auros-ibus.service 2>/dev/null)
    case "$shipped_pid" in ''|0|*[!0-9]*)
        restoration_incomplete "ordinary IBus service MainPID unavailable" ;;
      *)
        record_process "$shipped_pid" "$XOUT/shipped-service"
        grep -qx 'exe=/usr/bin/ibus-daemon' "$XOUT/shipped-service.process" \
            || restoration_incomplete "ordinary service MainPID is not /usr/bin/ibus-daemon"
        grep -qx 'cmdline=/usr/bin/ibus-daemon --replace --xim ' \
            "$XOUT/shipped-service.process" \
            || restoration_incomplete "ordinary service arguments were not exactly shipped arguments"
        ;;
    esac
    shipped_ready=0; i=0
    while [ "$i" -lt 20 ]; do
        if ibus_cmd ibus list-engine --name-only \
                > "$XOUT/shipped-engine-ids.txt" \
                2> "$XOUT/shipped-engine-list.stderr" && \
           grep -Fqx pinyin "$XOUT/shipped-engine-ids.txt"; then
            shipped_ready=1; break
        fi
        sleep 0.25; i=$((i + 1))
    done
    [ "$shipped_ready" = 1 ] \
        || restoration_incomplete "ordinary IBus service was not ready with exact pinyin engine"
    note "ordinary_service_ready=$shipped_ready main_pid=${shipped_pid:-missing}"
    note "status=$result"
    sync
    say "ibus-x11-probe=$result evidence=round$round/x11-panel"
    if [ "$result" = incomplete ]; then
        if [ "$restoration_failed" = 1 ]; then
            probe_failure_kind=restoration
        else
            probe_failure_kind=diagnostic
        fi
        return 1
    fi
    return 0
}

if [ "$round" = 1 ] && [ -x /usr/lib/auros-profiletest/x11_xres_probe.py ]; then
    # Take the baseline before opening the diagnostic Mousepad; the host uses
    # it only to locate/focus that real window.
    act baseline x11-probe-mousepad || fail "could not record X11 probe baseline"
    if ! run_x11_panel_probe; then
        case "$probe_failure_kind" in
          restoration) fail "X11 panel diagnostic restoration failed" ;;
          *) fail "X11 panel diagnostic was incomplete" ;;
        esac
    fi
fi

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
