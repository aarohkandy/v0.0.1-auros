#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."

T=$(mktemp -d "${TMPDIR:-/tmp}/auros-first-session-probe.XXXXXX")
trap 'rm -rf "$T"' EXIT INT TERM
fail=0
ok() { printf 'ok   %s\n' "$1"; }
bad() { printf 'FAIL %s%s\n' "$1" "${2:+: $2}"; fail=$((fail + 1)); }
has() { printf '%s\n' "$2" | grep -q "$3" && ok "$1" || bad "$1" "$2"; }

mkdir "$T/bin" "$T/run"
cat > "$T/bin/aa" <<'EOF'
#!/bin/sh
[ -z "${FAKE_AA_AFTER:-}" ] || {
    n=$(cat "$FAKE_COUNT" 2>/dev/null || echo 0)
    [ "$n" -ge $((FAKE_AA_AFTER - 1)) ]
    exit
}
exit "${FAKE_AA_RC:-0}"
EOF
cat > "$T/bin/ip" <<'EOF'
#!/bin/sh
n=$(cat "$FAKE_COUNT" 2>/dev/null || echo 0); n=$((n + 1)); echo "$n" > "$FAKE_COUNT"
[ "$n" -ge "${FAKE_ROUTE_AFTER:-1}" ] && echo 'default via 10.0.2.2 dev enp0s1'
EOF
cat > "$T/bin/getent" <<'EOF'
#!/bin/sh
[ "${FAKE_DNS_RC:-0}" -eq 0 ] && echo '140.82.112.4 STREAM github.com'
exit "${FAKE_DNS_RC:-0}"
EOF
cat > "$T/bin/stat" <<'EOF'
#!/bin/sh
printf '%s\n' "${FAKE_RUN_MODE:-755}"
EOF
cat > "$T/bin/sleep" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$T/bin/timeout" <<'EOF'
#!/bin/sh
[ "$1" = 5 ] || exit 99
shift
exec "$@"
EOF
chmod 755 "$T/bin/"*

run_probe() {
    env AUROS_PROBE_AA_ENABLED="$T/bin/aa" AUROS_PROBE_IP="$T/bin/ip" \
        AUROS_PROBE_GETENT="$T/bin/getent" AUROS_PROBE_STAT="$T/bin/stat" \
        AUROS_PROBE_SLEEP="$T/bin/sleep" AUROS_PROBE_TIMEOUT="$T/bin/timeout" \
        AUROS_PROBE_PROFILES="$T/profiles" AUROS_PROBE_RUN="$T/run" \
        AUROS_PROBE_SAVED_BIN="$T/saved.bin" AUROS_PROBE_MAX_WAIT="${PROBE_WAIT:-4}" \
        FAKE_COUNT="$T/count" FAKE_ROUTE_AFTER="${FAKE_ROUTE_AFTER:-1}" \
        FAKE_AA_RC="${FAKE_AA_RC:-0}" FAKE_AA_AFTER="${FAKE_AA_AFTER:-}" \
        FAKE_DNS_RC="${FAKE_DNS_RC:-0}" \
        FAKE_RUN_MODE="${FAKE_RUN_MODE:-755}" \
        sh rootfs/usr/lib/auros/first-session-probe
}

printf 'profile one\nprofile two\n' > "$T/profiles"
: > "$T/count"
FAKE_ROUTE_AFTER=3 FAKE_AA_AFTER=3 out=$(run_probe)
[ "$(cat "$T/count")" = 3 ] && ok "the probe retries until AppArmor and the default route are ready" \
    || bad "the probe retries until AppArmor and the default route are ready" "$(cat "$T/count") attempts"
has "a healthy first session is one exact record" "$out" \
    '^v1 apparmor=enabled profiles=2 route=default dns=ok run_mode=0755 saved_bin=absent$'

: > "$T/count"; : > "$T/profiles"; : > "$T/saved.bin"
PROBE_WAIT=3 FAKE_ROUTE_AFTER=99 FAKE_AA_AFTER= FAKE_AA_RC=1 FAKE_DNS_RC=1 FAKE_RUN_MODE=700 out=$(run_probe)
[ "$(cat "$T/count")" = 3 ] && ok "negative polling stops at its explicit bound" \
    || bad "negative polling stops at its explicit bound" "$(cat "$T/count") attempts"
has "negative facts are reported honestly and the script still succeeds" "$out" \
    '^v1 apparmor=disabled profiles=0 route=missing dns=failed run_mode=0700 saved_bin=present$'
: > "$T/count"
PROBE_WAIT=999 FAKE_ROUTE_AFTER=99 FAKE_AA_AFTER= FAKE_AA_RC=1 out=$(run_probe)
[ "$(cat "$T/count")" = 45 ] && ok "even a bad requested wait is capped at 45 seconds" \
    || bad "even a bad requested wait is capped at 45 seconds" "$(cat "$T/count") attempts"
grep -q 'AA_ENABLED:-/usr/bin/aa-enabled' rootfs/usr/lib/auros/first-session-probe &&
    grep -q 'GETENT:-/usr/bin/getent' rootfs/usr/lib/auros/first-session-probe &&
    grep -q 'TIMEOUT:-/usr/bin/timeout' rootfs/usr/lib/auros/first-session-probe &&
    ok "the producer uses the paths shipped by the desktop image" \
    || bad "the producer uses the paths shipped by the desktop image"

unit=rootfs/etc/systemd/system/auros-first-session-probe.service
grep -q '^Type=simple$' "$unit" && ok "the diagnostic runs asynchronously from the boot target" \
    || bad "the diagnostic runs asynchronously from the boot target"
grep -q '^RuntimeMaxSec=50$' "$unit" && ok "the diagnostic has a service-level runtime ceiling" \
    || bad "the diagnostic has a service-level runtime ceiling"
grep -q '^After=apparmor.service NetworkManager.service$' "$unit" &&
    ! grep -q '^Before=' "$unit" && ok "it waits for services but never holds the desktop" \
    || bad "it waits for services but never holds the desktop"
grep -q '^SyslogIdentifier=auros-first-session-probe$' "$unit" &&
    grep -q '^StandardOutput=journal$' "$unit" && ok "the one record stays in the local journal" \
    || bad "the one record stays in the local journal"
grep -q '^ProtectSystem=strict$' "$unit" && grep -q '^NoNewPrivileges=yes$' "$unit" &&
    grep -q '^ReadOnlyPaths=/run /sys/kernel/security$' "$unit" &&
    grep -q '^RestrictNamespaces=yes$' "$unit" && ok "the read-only probe is sandboxed" \
    || bad "the read-only probe is sandboxed"
grep -q 'chmod 755 .*first-session-probe' build/forge &&
    grep -q 'enable_unit auros-first-session-probe.service multi-user.target' build/forge &&
    [ "$(grep -c 'multi-user.target.wants/auros-first-session-probe.service' build/forge)" = 1 ] &&
    ok "forge makes the producer executable, enables it, and checks the link" \
    || bad "forge makes the producer executable, enables it, and checks the link"

printf '%d failed\n' "$fail"
exit "$fail"
