#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  softwarereadytest -- rootfs/usr/lib/auros/software-ready.sh
#
#  The script that keeps "Get more programs" fresh the first time the
#  computer is online. Run here against stand-ins for flatpak, apt-get,
#  nm-online and sleep (nothing is fetched), it must:
#
#    1. fetch Flathub's catalogue ONCE when it works, and never again,
#       however often apt's lists fail (they used to share one stamp, so
#       a failing mirror re-ran the catalogue from the top every minute);
#    2. fetch apt's lists once when they work, however often the
#       catalogue fails;
#    3. say WHY a step failed, in the tool's own words (it used to go to
#       /dev/null: "catalogue: not yet" for five minutes and no reason);
#    4. try again quickly at first, then back off to a minute;
#    5. do nothing while the computer is offline;
#    6. write software-ready (which ends the service) only when both
#       are done, and add Flathub from the copy the build saved if the
#       image has none.
#
#  (A build with no flatpak at all skips the catalogue step in three
#  lines; not tested: a machine that has flatpak cannot hide it.)
#
#    --break=stamp   the script is changed so a worked catalogue is
#                    fetched again on every pass: the test must FAIL.
#    --break=why     the tools' words are thrown away again: must FAIL.
#    --break=wake    a network coming up no longer ends the pause: must FAIL.
#
#  No root, no network. POSIX sh.
# ═══════════════════════════════════════════════════════════════════
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$ROOT/rootfs/usr/lib/auros/software-ready.sh"
DISPATCHER="$ROOT/rootfs/etc/NetworkManager/dispatcher.d/31-auros-software"
UNIT="$ROOT/rootfs/etc/systemd/system/auros-software.service"
BREAK=""; case "${1:-}" in --break=*) BREAK=${1#--break=} ;; esac
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-70s ok\n' "$1"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-70s FAIL\n' "$1"; shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
check() { # NAME EXPECTED ACTUAL
    if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "expected: $2" "     got: $3"; fi
}
has() { # NAME FILE PATTERN
    if grep -q -- "$3" "$2" 2>/dev/null; then ok "$1"; else bad "$1" "no line matching: $3" "in: $(tr '\n' '|' < "$2" | cut -c1-300)"; fi
}

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
S="$SCRIPT"
case "$BREAK" in
    stamp) S="$T/mutant.sh"
           sed 's/\[ -e "\$CATALOGUE" \] || catalogue/catalogue/' "$SCRIPT" > "$S"
           cmp -s "$S" "$SCRIPT" && { echo "softwarereadytest: the mutation changed nothing" >&2; exit 2; } ;;
    wake)  S="$T/mutant.sh"
           sed 's/^trap : USR1$//' "$SCRIPT" > "$S"
           cmp -s "$S" "$SCRIPT" && { echo "softwarereadytest: the mutation changed nothing" >&2; exit 2; } ;;
    why)   S="$T/mutant.sh"
           sed 's/^why() .*/why() { :; }/' "$SCRIPT" > "$S"
           cmp -s "$S" "$SCRIPT" && { echo "softwarereadytest: the mutation changed nothing" >&2; exit 2; } ;;
    "")    ;;
    *)     echo "softwarereadytest: unknown --break=$BREAK" >&2; exit 2 ;;
esac

# Stand-ins. Each counts its calls in $W/<name>.n and fails the first
# $W/<name>.failures times (a number in a file), saying what is in
# $W/<name>.words on stderr.
mkdir -p "$T/bin"
cat > "$T/bin/standin" <<'EOF'
#!/bin/sh
name=$(basename "$0"); W=$STANDIN_DIR
n=$(cat "$W/$name.n" 2>/dev/null || echo 0); n=$((n+1)); echo $n > "$W/$name.n"
echo "$*" >> "$W/$name.args"
f=$(cat "$W/$name.failures" 2>/dev/null || echo 0)
if [ "$n" -le "$f" ]; then cat "$W/$name.words" >&2 2>/dev/null; exit 1; fi
exit 0
EOF
chmod +x "$T/bin/standin"
cat > "$T/bin/flatpak" <<'EOF'
#!/bin/sh
# `flatpak remotes` lists flathub only if $W/have-flathub exists; the rest
# are counted by the stand-in under the name of the sub-command.
W=$STANDIN_DIR
case "$1" in
    remotes) [ -e "$W/have-flathub" ] && echo flathub; exit 0 ;;
    remote-add) echo "$*" >> "$W/remote-add.args"; touch "$W/have-flathub"; exit 0 ;;
    update) n=$(cat "$W/update.n" 2>/dev/null || echo 0); n=$((n+1)); echo $n > "$W/update.n"
            f=$(cat "$W/update.failures" 2>/dev/null || echo 0)
            if [ "$n" -le "$f" ]; then echo "error: Error updating appstream2: No such ref 'appstream2/x86_64' in remote flathub" >&2; exit 1; fi
            exit 0 ;;
esac
exit 0
EOF
cat > "$T/bin/apt-get" <<'EOF'
#!/bin/sh
W=$STANDIN_DIR
n=$(cat "$W/apt.n" 2>/dev/null || echo 0); n=$((n+1)); echo $n > "$W/apt.n"
f=$(cat "$W/apt.failures" 2>/dev/null || echo 0)
if [ "$n" -le "$f" ]; then echo "E: Release file for http://archive.ubuntu.com/ is not valid yet" >&2; exit 1; fi
exit 0
EOF
cat > "$T/bin/nm-online" <<'EOF'
#!/bin/sh
W=$STANDIN_DIR
n=$(cat "$W/online.n" 2>/dev/null || echo 0); n=$((n+1)); echo $n > "$W/online.n"
[ -e "$W/offline" ] && [ "$n" -le "$(cat "$W/offline")" ] && exit 1
exit 0
EOF
cat > "$T/bin/systemd-notify" <<'EOF'
#!/bin/sh
echo "$*" >> "$STANDIN_DIR/systemd-notify.args"
[ -e "$STANDIN_DIR/notify.failures" ] && exit 1
exit 0
EOF
cat > "$T/bin/sleep" <<'EOF'
#!/bin/sh
W=$STANDIN_DIR
echo "$1" >> "$W/sleeps"
[ -e "$W/block" ] && exec /bin/sleep 20       # a pause that really lasts, for the wake-up case
[ "$(wc -l < "$W/sleeps")" -le 30 ] || kill -TERM "$PPID"      # a loop that never ends
exit 0
EOF
chmod +x "$T/bin/flatpak" "$T/bin/apt-get" "$T/bin/nm-online" "$T/bin/systemd-notify" "$T/bin/sleep"

# run NAME KEY=VALUE...  -> $W is that scenario's directory, $W/out its output
run() {
    name=$1; shift
    W="$T/$name"; rm -rf "$W"; mkdir -p "$W/state"
    for kv in "$@"; do k=${kv%%=*}; v=${kv#*=}
        case $k in
            offline) echo "$v" > "$W/offline" ;;
            have-flathub) touch "$W/have-flathub" ;;
            saved) echo "[Flatpak Repo]" > "$W/saved.flatpakrepo" ;;
            *) echo "$v" > "$W/$k.failures" ;;
        esac
    done
    STANDIN_DIR="$W" PATH="$T/bin:$PATH" AUROS_STATE="$W/state" AUROS_FLATHUB_REPO="$W/saved.flatpakrepo" \
        AUROS_ONLINE_WAIT=1 sh "$S" > "$W/out" 2>&1
    echo $? > "$W/rc"
}
count() { cat "$W/$1" 2>/dev/null || echo 0; }

echo "  software-ready.sh${BREAK:+ (mutant: $BREAK)}"

# 1. the catalogue works at once; apt fails three times, then works
run apt-fails update=0 apt=3 have-flathub=1
check "catalogue fetched once, however often apt fails"      1 "$(count update.n)"
check "apt tried again until it worked (4 calls)"            4 "$(count apt.n)"
check "software-ready is written when both are done"         1 "$([ -e "$W/state/software-ready" ] && echo 1 || echo 0)"
check "the script ends (rc 0) when both are done"            0 "$(cat "$W/rc")"
has   "apt's reason is in the journal, in apt's words"       "$W/out" "not valid yet"
check "the catalogue stamp is there before apt succeeded"    1 "$([ -e "$W/state/catalogue-ready" ] && echo 1 || echo 0)"
check "first pause is short, then it doubles to a cap"       "10 20 40" "$(tr '\n' ' ' < "$W/sleeps" | sed 's/ $//')"

# 2. apt works at once; the catalogue fails twice, with a reason
run cat-fails update=2 apt=0 have-flathub=1
check "catalogue tried until it worked (3 calls)"            3 "$(count update.n)"
check "apt's lists fetched once, however often the catalogue fails" 1 "$(count apt.n)"
has   "the catalogue's reason is in the journal"             "$W/out" "No such ref"
has   "...next to the plain 'not yet'"                       "$W/out" "catalogue: not yet"
check "ready at the end"                                     1 "$([ -e "$W/state/software-ready" ] && echo 1 || echo 0)"

# 3. offline for the first three questions: nothing is fetched meanwhile
run offline offline=3 have-flathub=1
check "nothing fetched while offline, then both once"        "1 1" "$(count update.n) $(count apt.n)"
check "it asked again after each pause (4 questions)"        4 "$(count online.n)"

# 4. Flathub is not in the image: added from the build's saved copy
run add-flathub update=0 apt=0 saved=1
has   "Flathub added when the image lacks it"                "$W/out" "Flathub added"
check "...by the one command Flathub's own page gives"       "remote-add --system --if-not-exists flathub $W/saved.flatpakrepo" "$(cat "$W/remote-add.args" 2>/dev/null)"

# 5. the network comes up in the middle of a pause: the pause ends at once
#    (NetworkManager's dispatcher sends USR1), not after the 10 s it asked for
W="$T/wake"; rm -rf "$W"; mkdir -p "$W/state"; echo 1 > "$W/update.failures"; : > "$W/block"; touch "$W/have-flathub"
STANDIN_DIR="$W" PATH="$T/bin:$PATH" AUROS_STATE="$W/state" AUROS_ONLINE_WAIT=1 sh "$S" > "$W/out" 2>&1 &
pid=$!
i=0; while [ ! -s "$W/sleeps" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
kill -USR1 "$pid" 2>/dev/null
i=0; while kill -0 "$pid" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
if kill -0 "$pid" 2>/dev/null; then kill "$pid" 2>/dev/null; bad "a pause ends when the network comes up" "still waiting 5 s after USR1"
else ok "a pause ends when the network comes up (USR1)"; fi
pkill -f "sleep 20" 2>/dev/null || true
check "...and the work was done by then" 1 "$([ -e "$W/state/software-ready" ] && echo 1 || echo 0)"

# 6. Service readiness closes the tiny window before the shell installs its
#    USR1 handler.  NetworkManager must not signal an activating process; once
#    the service has announced readiness, the same event wakes it with USR1.
check "the unit waits for explicit startup readiness" "Type=notify" "$(grep '^Type=' "$UNIT")"
check "the unit only accepts readiness from its main process" "NotifyAccess=main" "$(grep '^NotifyAccess=' "$UNIT")"
trap_line=$(grep -n '^trap : USR1$' "$SCRIPT" | cut -d: -f1)
notify_line=$(grep -n 'systemd-notify --ready --pid="\$\$"' "$SCRIPT" | cut -d: -f1)
if [ -n "$trap_line" ] && [ -n "$notify_line" ] && [ "$trap_line" -lt "$notify_line" ]; then
    ok "USR1 is handled before startup readiness is announced"
else
    bad "USR1 is handled before startup readiness is announced" "trap line: ${trap_line:-missing}" "notify line: ${notify_line:-missing}"
fi

cat > "$T/bin/systemctl" <<'EOF'
#!/bin/sh
W=$DISPATCH_TEST_DIR
echo "$*" >> "$W/calls"
case "$1" in
    show) cat "$W/state" ;;
    kill) echo signal >> "$W/signals" ;;
esac
EOF
chmod +x "$T/bin/systemctl"
W="$T/dispatcher"; mkdir -p "$W"
dispatcher() { DISPATCH_TEST_DIR="$W" PATH="$T/bin:$PATH" sh "$DISPATCHER" test0 "$1"; }
signal_count() { if [ -e "$W/signals" ]; then awk 'END { print NR + 0 }' "$W/signals"; else echo 0; fi; }
echo activating > "$W/state"; dispatcher up
check "a network event before readiness sends no signal" 0 "$(signal_count)"
echo active > "$W/state"; dispatcher connectivity-change
check "a network event after readiness sends USR1" 1 "$(signal_count)"
has "the dispatcher targets only the service main process" "$W/calls" "kill --kill-whom=main -s USR1 auros-software.service"
echo inactive > "$W/state"; dispatcher dhcp4-change
check "a network event after the service finishes sends no signal" 1 "$(signal_count)"
echo active > "$W/state"; dispatcher down
check "a non-up network event sends no signal" 1 "$(signal_count)"

# Every NetworkManager event that can mean "online now" has the same exact,
# main-process-only wake behavior.
: > "$W/calls"; : > "$W/signals"; echo active > "$W/state"
for action in up dhcp4-change dhcp6-change connectivity-change; do dispatcher "$action"; done
check "all four online events wake the ready service" 4 "$(signal_count)"
kill_lines=$(grep '^kill ' "$W/calls" || true)
check "every wake has the exact main-process USR1 command" \
    "$(printf '%s\n' 'kill --kill-whom=main -s USR1 auros-software.service' 'kill --kill-whom=main -s USR1 auros-software.service' 'kill --kill-whom=main -s USR1 auros-software.service' 'kill --kill-whom=main -s USR1 auros-software.service')" \
    "$kill_lines"

# A failed readiness handshake must fail promptly. Otherwise Type=notify stays
# activating until its startup timeout while the dispatcher correctly declines
# to signal it.
run notify-fails notify=1 have-flathub=1
check "a rejected readiness notification fails immediately" 1 "$(cat "$W/rc")"
check "no network work starts when readiness was rejected" "0 0" "$(count update.n) $(count apt.n)"
has "readiness failure is explicit in the journal" "$W/out" "could not report startup readiness"

echo
if [ "$fail" -gt 0 ]; then echo "softwarereadytest: $fail of $checked FAILED"; exit 1; fi
echo "softwarereadytest: $checked checks passed"
