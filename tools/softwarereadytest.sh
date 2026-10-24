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
#
#  No root, no network. POSIX sh.
# ═══════════════════════════════════════════════════════════════════
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$ROOT/rootfs/usr/lib/auros/software-ready.sh"
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
cat > "$T/bin/sleep" <<'EOF'
#!/bin/sh
W=$STANDIN_DIR
echo "$1" >> "$W/sleeps"
[ "$(wc -l < "$W/sleeps")" -le 30 ] || kill -TERM "$PPID"      # a loop that never ends
exit 0
EOF
chmod +x "$T/bin/flatpak" "$T/bin/apt-get" "$T/bin/nm-online" "$T/bin/sleep"

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

echo
if [ "$fail" -gt 0 ]; then echo "softwarereadytest: $fail of $checked FAILED"; exit 1; fi
echo "softwarereadytest: $checked checks passed"
