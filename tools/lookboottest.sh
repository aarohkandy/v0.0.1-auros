#!/bin/sh
# lookboottest.sh — does the look she picked in Settings survive a restart?
#
# It did not. Settings writes her choice to ~/.config/auros/shell.conf
# and the running desktop reloads it, so the change showed at once --
# and the next time the computer started, aurshell read the theme
# BEFORE deciding whose copy to read, drew the machine's look, and
# Settings went on saying she had picked hers.
#
# A restart is a fresh aurshell with nothing on its command line, so
# that is what this runs: once with only her saved file in a home
# directory, once told the same file outright. The two pictures must
# be the same picture.
#
#   AURSHELL=<built aurshell> sh tools/lookboottest.sh   (repository root)
set -u
cd "$(dirname "$0")/.." || exit 2
A=${AURSHELL:-}
[ -n "$A" ] && [ -x "$A" ] || { echo "set AURSHELL to a built aurshell"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/lookboot.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/home/.config/auros" "$T/st/c" "$T/apps"

# Her pick: Sandstone, resolved the way Settings resolves it.
printf 'sandstone\n' > "$T/st/theme"
tail -n +2 themes/templates/shell.conf.tpl | \
    AURORA_THEMES=themes AURORA_TEMPLATES=themes/templates \
    AURORA_STATE="$T/st" AURORA_CACHE="$T/st/c" ./src/aurora/aurora render \
    > "$T/home/.config/auros/shell.conf" 2>/dev/null
grep -q '^col_bg' "$T/home/.config/auros/shell.conf" || { echo "could not render Sandstone"; exit 2; }

run() {
    HOME="$T/home" AURSHELL_CLOCK=1761206400 AUROS_APP_DIRS="$T/apps" \
        "$A" --shell shells/rail.shell --policy /nonexistent --size 640 400 "$@" \
        >> "$T/log" 2>&1
}
run --png "$T/restart.png"
run --conf "$T/home/.config/auros/shell.conf" --png "$T/hers.png"

if cmp -s "$T/restart.png" "$T/hers.png"; then
    echo "    ok    after a restart the desktop wears the look she picked"
    exit 0
fi
echo "    FAIL  after a restart the desktop wears the machine's look, not hers"
sed 's/^/          /' "$T/log"
exit 1
