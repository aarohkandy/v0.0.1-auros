#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  flatpakupdatetest — do her Flathub programs update by themselves?
#  (docs/security/UPDATES.md)
#
#  Runs the ExecStart lines of rootfs/etc/systemd/system/
#  auros-flatpak-update.service, as written there, against a system
#  installation with a small runtime installed at its first commit and
#  a second commit published since. A local repository stands in for
#  Flathub (deterministic, offline): what is proven is that the unit's
#  commands update the system installation without a question and
#  remove what nothing uses; that the real Flathub is reachable from
#  here is the image's own software-ready.sh and the store's job.
#
#    --break=unit   the update line is replaced by one that updates
#                   nothing: the test must FAIL.
#
#  Needs root, flatpak, ostree. Use a throwaway container.
# ═══════════════════════════════════════════════════════════════════
set -u
export LC_ALL=C
ROOT=$(cd "$(dirname "$0")/.." && pwd)
UNIT="$ROOT/rootfs/etc/systemd/system/auros-flatpak-update.service"
BREAK=""; [ "${1:-}" = --break=unit ] && BREAK=unit
fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s ok\n' "$1"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s FAIL\n' "$1"; shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
need() { echo "flatpakupdatetest: $*" >&2; exit 2; }
[ "$(id -u)" = 0 ] || need "run as root, in a throwaway container"
[ -f /.dockerenv ] || [ -f /run/.containerenv ] || need "refusing to run outside a container"
command -v flatpak >/dev/null || need "needs flatpak"

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
# By hand: build-init wants the SDK installed, and this runtime is a
# stand-in. build-export wants metadata, files/ and (for a runtime) usr/.
mkdir -p "$T/rt/files" "$T/rt/usr/share"
cat > "$T/rt/metadata" <<'EOM'
[Runtime]
name=org.auros.UpdateTest
runtime=org.auros.UpdateTest/x86_64/1
sdk=org.auros.UpdateTest/x86_64/1
EOM
publish() { # CONTENT
    echo "$1" > "$T/rt/usr/share/version"
    flatpak build-export --runtime --no-update-summary "$T/repo" "$T/rt" 1 >/dev/null 2>"$T/e" ||
        { cat "$T/e"; need "build-export failed"; }
    flatpak build-update-repo "$T/repo" >/dev/null 2>&1 || need "build-update-repo failed"
}
publish one
flatpak remote-add --system --no-gpg-verify auros-test "file://$T/repo" || need "remote-add failed"
flatpak install --system --noninteractive -y auros-test org.auros.UpdateTest//1 >"$T/i" 2>&1 || { cat "$T/i"; need "install failed"; }
F=/var/lib/flatpak/runtime/org.auros.UpdateTest/x86_64/1/active/files/share/version
[ "$(cat $F 2>/dev/null)" = one ] && ok "installed at its first commit" || bad "installed at its first commit" "$(cat $F 2>&1)"
publish two

echo
echo "== the unit's own ExecStart lines"
mapfile -t LINES < <(sed -n 's/^ExecStart=//p' "$UNIT")
[ "${#LINES[@]}" -eq 2 ] && ok "two ExecStart lines (update, then remove what is unused)" || bad "two ExecStart lines" "found ${#LINES[@]}"
[ "$BREAK" = unit ] && LINES[0]="/usr/bin/flatpak list --system"
timeout 300 ${LINES[0]} </dev/null >"$T/u1" 2>&1; rc=$?
[ "$rc" = 0 ] && ok "the update line exits 0 with nothing to answer" || bad "the update line exits 0 with nothing to answer" "exit $rc" "$(tail -5 "$T/u1")"
[ "$(cat $F 2>/dev/null)" = two ] && ok "the runtime moved to the new commit by itself" || bad "the runtime moved to the new commit by itself" "version file says: $(cat $F 2>&1)" "$(tail -5 "$T/u1")"
# A runtime installed by hand is pinned, and --unused rightly keeps it.
# What the timer cleans up is a runtime that came with an app which has
# since been removed: not pinned, used by nothing. Same state, so unpin.
flatpak pin --system | grep -q org.auros.UpdateTest && ok "a runtime installed by hand is pinned (kept)" || bad "a runtime installed by hand is pinned (kept)"
flatpak pin --system --remove runtime/org.auros.UpdateTest/x86_64/1 >/dev/null 2>&1
timeout 300 ${LINES[1]} </dev/null >"$T/u2" 2>&1; rc=$?
[ "$rc" = 0 ] && ok "the clean-up line exits 0" || bad "the clean-up line exits 0" "exit $rc" "$(tail -5 "$T/u2")"
[ ! -e "$F" ] && ok "and removed the runtime nothing uses" || bad "and removed the runtime nothing uses"
echo
if [ "$fail" -eq 0 ]; then echo "flatpakupdatetest: $checked checks, all passed${BREAK:+ (with --break=$BREAK: the test did NOT catch it)}"; exit 0; fi
echo "flatpakupdatetest: $fail of $checked checks FAILED${BREAK:+ (--break=$BREAK, as it should)}"; exit 1
