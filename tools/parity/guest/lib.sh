# sourced by run.sh and the checks. NOT PART OF AUROS.
P=/usr/lib/auros-parity
. "$P/config"      # CHECKS OPTS WITHOUT APT APPS (checks run as separate processes: the environment is not inherited)
LOG=/var/lib/auros-parity/log
say()    { printf '\nPARITY %s\n' "$*" > /dev/ttyS0 2>/dev/null; printf 'PARITY %s\n' "$*" >> "$LOG"; }
result() { say "result $1 $2 $3"; }          # name pass|fail detail words
note()   { say "note $*"; }
# act VERB ARGS...: ask the host to do something (snap, key, type, click, hue, near, changed, welcome) and WAIT until it
# has done it (an acknowledgement over the 9p share). Without the share (older runs) it falls back to a pause.
CNT=/var/lib/auros-parity/acts
acked() { [ "$(cat /var/lib/auros-parity/host/acks 2>/dev/null || echo -1)" -ge "$1" ]; }
act() {
    _a=$(cat "$CNT" 2>/dev/null || echo 0); _a=$((_a+1)); echo "$_a" > "$CNT"
    say "do $*"
    if [ -f /var/lib/auros-parity/host/acks ]; then wait_for 180 acked "$_a"; else sleep 3; fi
}
snap()   { act snap "$1"; sleep 1; }                       # the host takes the picture
key()    { act key "$1"; sleep "${2:-1}"; }                # e.g. ctrl+s, ret
typing() { act type "$1"; sleep 2; }
click()  { act click "$1" "$2"; sleep 1; }
opt()    { case " $OPTS " in *" $1 "*) return 0 ;; esac; return 1; }
# wait_for SECONDS CMD...  -> 0 as soon as CMD succeeds
wait_for() { _n=$1; shift; _i=0; while [ "$_i" -lt "$_n" ]; do "$@" >/dev/null 2>&1 && return 0; sleep 1; _i=$((_i+1)); done; return 1; }

# ── a web page the tests serve, and a Firefox to show it ───────────────
OUT=/var/lib/auros-parity/out
serve() {
    [ -n "${SRV:-}" ] && return 0
    python3 "$P/server.py" "$P/www" "$OUT" 8123 &
    SRV=$!
    wait_for 15 curl -sf -o /dev/null http://127.0.0.1:8123/
}
unserve() { [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null; SRV=""; }
fx_start() {   # URL -- a fresh profile, so no check inherits another's state
    rm -rf "$HOME/parity-profile"; mkdir -p "$HOME/parity-profile"
    cp "$P/user.js" "$HOME/parity-profile/user.js"
    firefox --no-remote --new-instance --profile "$HOME/parity-profile" "$1" > /var/lib/auros-parity/firefox.log 2>&1 &
    FX=$!
}
fx_stop() { pkill -f parity-profile 2>/dev/null; sleep 1; pkill -9 -f parity-profile 2>/dev/null; return 0; }
# clear_report NAME BEFORE the browser starts (a page may report before anyone waits), then wait_report NAME SECONDS
clear_report() { rm -f "$OUT/$1.json" "$OUT/$1-stage.json"; }
wait_report() { wait_for "$2" test -s "$OUT/$1.json"; }
# jget FILE KEY  -> the value of a key in a small JSON report
jget() { python3 -c "import json,sys; print(json.load(open('$1')).get('$2',''))" 2>/dev/null; }

# cpu_busy SECONDS -> whole-machine CPU use (percent) over that time, from /proc/stat
cpu_busy() {
    set -- "$1" $(awk '/^cpu /{print $2+$3+$4+$7+$8, $2+$3+$4+$5+$6+$7+$8}' /proc/stat)
    sleep "$1"
    awk -v b0="$2" -v t0="$3" '/^cpu /{b=$2+$3+$4+$7+$8; t=$2+$3+$4+$5+$6+$7+$8; printf "%d", 100*(b-b0)/(t-t0)}' /proc/stat
}
