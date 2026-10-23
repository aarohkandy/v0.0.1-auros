#!/bin/sh
# wishe2e.sh — the potato, end to end: the shell's panel, the real aurwish (stream W1's
# engine, test build) and a stand-in for the Wishes service. See tools/wishe2e.c.
#
#   sh tools/wishe2e.sh                # from the repository root; needs cc, python3, curl,
#                                      # libdbus-1-dev (pkg-config dbus-1), about 1-2 minutes
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=${TMPDIR:-/tmp}/wishe2e.$$
mkdir -p "$T/run" "$T/home/.local/share/auros" "$T/shells"
cleanup() { kill $(jobs -p) 2>/dev/null || true; rm -rf "$T"; }
trap cleanup EXIT
for need in cc python3 curl pkg-config; do
    command -v $need >/dev/null 2>&1 || { echo "needs $need"; exit 2; }
done
pkg-config --exists dbus-1 || { echo "needs libdbus-1-dev"; exit 2; }

echo "== building the engine (test build) and the client"
TEST=1 sh "$ROOT/src/aurwish/build.sh" "$T/aurwish" >/dev/null 2>"$T/build.err" || { cat "$T/build.err"; exit 1; }
cc -O1 -g -std=gnu11 -w -DAUROS_WISHES=1 -o "$T/wishe2e" "$ROOT/tools/wishe2e.c" \
   "$ROOT/src/aurshell/foot.c" "$ROOT/src/aurshell/run.c" "$ROOT/src/aurshell/draw.c" \
   "$ROOT/src/aurshell/shellcommon.c" "$ROOT/src/aurshell/anim.c" \
   "$ROOT"/src/aurshell/layouts/[a-z]*.c "$ROOT/src/common/theme.c" "$ROOT/src/common/font.c" \
   "$ROOT/src/common/png.c" -I "$ROOT/src/aurshell" -I "$ROOT/src/common" -lm

PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
: > "$T/service.log"
python3 "$ROOT/tools/wishe2e-cloud.py" "$PORT" "$T/service.log" &
printf 'url=http://127.0.0.1:%s\n' "$PORT" > "$T/wishes.conf"
printf 'wishes = on\n' > "$T/policy.conf"
printf 'wishes=on\n' > "$T/features"

export XDG_RUNTIME_DIR="$T/run" HOME="$T/home" XDG_DATA_HOME="$T/home/.local/share"
export AURWISH_FEATURES="$T/features" AURWISH_POLICY="$T/policy.conf" AURWISH_CONF="$T/wishes.conf" AURWISH_STORE="$T/home/.local/share/auros/wishes.json"
export AURWISH_SOUNDS="$T/none" AURWISH_HELP="$T/none"
"$T/aurwish" >"$T/engine.log" 2>&1 &
i=0; while [ ! -S "$T/run/aurwish.sock" ] && [ $i -lt 50 ]; do sleep 0.2; i=$((i + 1)); done
[ -S "$T/run/aurwish.sock" ] || { echo "the engine did not start"; cat "$T/engine.log"; exit 1; }
echo "== running"
if "$T/wishe2e" "$T/service.log"; then rc=0; else rc=1; echo "--- the engine said:"; tail -20 "$T/engine.log"; echo "--- the service was asked:"; cat "$T/service.log"; fi
exit $rc
