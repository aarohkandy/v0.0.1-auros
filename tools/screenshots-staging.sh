#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  screenshots-staging — render every staging screen to docs/shots/
#
#  Feeds src/aurscreen the lines a real run says (with a made-up clock,
#  so the "about N minutes left" is the one a computer that takes the
#  typical time would show) and paints each state off-screen. Look at
#  every one of them: DESIGN-DIRECTION.md section 4 says what they may
#  say, and none may show a number she cannot use, a device, or a log.
#
#    sh tools/screenshots-staging.sh [OUT-DIR] [W H]
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
OUT="${1:-docs/shots/staging}"
W="${2:-1280}"; H="${3:-720}"
mkdir -p "$OUT"
T=$(mktemp -d "${TMPDIR:-/tmp}/staging-shots.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/fonts"
cp src/aurbridge/fonts/atkinsonnext-400.ttf "$T/fonts/ui.ttf"
cp src/aurbridge/fonts/atkinsonnext-700.ttf "$T/fonts/ui-bold.ttf"
gcc -O2 -std=gnu11 -Wall -Wextra -DFONT_DIR="\"$T/fonts\"" -o "$T/aurscreen" src/aurscreen/screen.c \
    src/aurshell/kms.c src/aurshell/draw.c src/common/font.c src/common/png.c -lm \
    || { echo "aurscreen did not build"; exit 1; }
say() { for l in "$@"; do printf 'aurstage: %s\n' "$l"; done; }
t() { printf 'aurstage-time %s\n' "$1"; }
shot() { # name  mode  (lines on stdin)
    "$T/aurscreen" --mode "$2" --png "$OUT/$1.png" --size "$W" "$H" && echo "  $OUT/$1.png"
}

# 1. the first thing she sees, before aurstage has said a word
true | shot 1-starting install

# 2-5. an install, the way it goes today (the staging shrink)
{ t 1; say "AurOS staging environment"; t 12; say "" "Saving this computer's Windows startup, so it can be put back."
  t 70; say "" "Making room on the Windows drive. This is the only part"
  printf 'aurstage-progress 3\n'; t 130; printf 'aurstage-progress 12\n'; } | shot 2-making-room install
{ t 1; say "AurOS staging environment"; t 12; say "" "Saving this computer's Windows startup, so it can be put back."
  t 70; say "" "Making room on the Windows drive. This is the only part"
  t 600; say "" "Copying AurOS onto this computer."; t 700; printf 'aurstage-progress 10\n'
  t 880; printf 'aurstage-progress 38\n'; } | shot 3-copying install
{ t 1; say "AurOS staging environment" "" "Saving this computer's Windows startup, so it can be put back."
  t 40; say "" "Making room on the Windows drive. This is the only part"
  t 500; say "" "Copying AurOS onto this computer."; t 900
  say "" "Making this computer able to start AurOS."; t 940
  say "" "Checking that this computer works under AurOS."; t 980
  say "" "Writing the new layout."; t 1000
  say "handing over to the system on /dev/vda3, in this same boot"; } | shot 4-installed install

# 6-7. the fast way: Windows made the room before the restart
{ printf 'aurstage-mode gap\n'; t 1; say "AurOS staging environment"; t 6
  say "" "Checking the copy of AurOS that Windows wrote."; t 40; printf 'aurstage-progress 20\n'; } | shot 5-fast-checking install
{ printf 'aurstage-mode gap\n'; t 1; say "" "Checking the copy of AurOS that Windows wrote."; t 150
  say "" "Saving this computer's Windows startup, so it can be put back."; t 190
  say "" "Checking that this computer works under AurOS."; t 215; } | shot 6-fast-checking-works install

# 8-10. it stopped
{ t 1; say "AurOS staging environment" "WARNING: The copy of AurOS on the Windows drive is damaged."
  say "         Run the installer in Windows again."
  say "aurstage-report v1 verdict=image-damaged record=refused -"
  printf 'aurstage-now untouched\n'; printf 'aurstage-countdown 48\n'; } | shot 7-stopped-nothing-changed install
{ t 1; say "" "Making room on the Windows drive. This is the only part"; t 400
  say "WARNING: what was written to this computer's disk did not read back the same, 475 MB in."
  say "aurstage-report v1 verdict=write-failed record=failed -"
  printf 'aurstage-now smaller\n'; printf 'aurstage-countdown 31\n'; } | shot 8-stopped-room-made install

# 11-13. putting Windows back
{ t 1; say "AurOS staging environment" "── putting Windows back ──" "checking the saved copy on /dev/vdb" "restoring /dev/vda"
  printf 'aurstage-progress 55\n'; t 90; } | shot 9-putting-back restore
{ t 1; say "restoring /dev/vda" "aurstage-report v1 verdict=restored record=done -" "" "Windows is back."
  printf 'aurstage-countdown 9\n'; } | shot 10-windows-is-back restore
{ t 1; say "WARNING: there is no saved copy of this computer's Windows startup"
  say "aurstage-report v1 verdict=restore-nothing-saved record=none -"
  printf 'aurstage-countdown 52\n'; } | shot 11-not-put-back restore
echo "done: $OUT"
