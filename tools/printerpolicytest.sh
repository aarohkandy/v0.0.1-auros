#!/bin/sh
# printerpolicytest -- may she set up a printer, whatever the profile says about installing software?
#
# The office profile adds cups and the printer tool because its people print, and says
# allow_user_install="no" because an administrator decides what is INSTALLED. build/forge tied the
# printer permission to that same word, so on the office image the tool's Add asked for a password
# nobody there has, and Settings (which hid its Printers row for the same reason) had no way to it.
#
# This takes the printer block out of build/forge (the lines themselves, not a copy), runs it on a
# stand-in image with the policy file cups-pk-helper ships, once for each way a profile can say it,
# and reads what rule text comes out.
#
#   sh tools/printerpolicytest.sh        (from anywhere)
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
fail=0; n=0
ok()  { n=$((n+1)); printf '  %-74s ok\n' "$1"; }
bad() { n=$((n+1)); fail=$((fail+1)); printf '  %-74s FAIL\n' "$1"; shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

FORGE=${FORGE:-build/forge}
awk '/^        # SETTING UP A PRINTER, WITH THE ACTION IDS READ OUT OF THE FILE\.$/ { f = 1 }
     /^        # FLATHUB, BY THE SAME METHOD/ { exit } f { print }' "$FORGE" > "$T/block.sh"
[ -s "$T/block.sh" ] || { echo "printerpolicytest: no printer block in $FORGE"; exit 2; }

RFS="$T/rfs"; mkdir -p "$RFS/usr/share/polkit-1/actions" "$RFS/usr/bin"
cat > "$RFS/usr/share/polkit-1/actions/org.opensuse.cupspkhelper.mechanism.policy" <<'POL'
<policyconfig>
  <action id="org.opensuse.cupspkhelper.mechanism.all-edit">
  </action>
  <action id="org.opensuse.cupspkhelper.mechanism.printer-add">
  </action>
  <action id="org.opensuse.cupspkhelper.mechanism.job-cancel">
  </action>
</policyconfig>
POL
printf '#!/bin/sh\n' > "$RFS/usr/bin/system-config-printer"; chmod +x "$RFS/usr/bin/system-config-printer"

run() { # allow_user_install -> $T/warn, print_cases on stdout
    : > "$T/warn"
    sh -c '
        WARNF=$1; note() { :; }; warn() { echo "$*" >> "$WARNF"; }
        RFS=$2; allow_user_install=$3; default_user=auros
        . "$4"
        printf "%s" "$print_cases"' sh "$T/warn" "$RFS" "$1" "$T/block.sh"
}

for v in yes no; do
    got=$(run "$v")
    miss=""
    for a in all-edit printer-add job-cancel; do
        printf '%s' "$got" | grep -q "case \"org.opensuse.cupspkhelper.mechanism.$a\":" || miss="$miss $a"
    done
    if [ -z "$miss" ]; then ok "allow_user_install=$v: she may use every printer permission cups-pk-helper has"
    else bad "allow_user_install=$v: she may use every printer permission cups-pk-helper has" "not granted:$miss"; fi
done

# without the tool's own helper on the image, the build says so (a printer she cannot add, and why)
rm "$RFS/usr/share/polkit-1/actions/org.opensuse.cupspkhelper.mechanism.policy"
got=$(run no)
if [ -z "$got" ] && grep -q "cups-pk-helper is not in this image" "$T/warn"; then
    ok "no cups-pk-helper, a printer tool: nothing is granted, and the build warns"
else bad "no cups-pk-helper, a printer tool: nothing is granted, and the build warns" "cases: $got" "$(cat "$T/warn")"; fi
rm "$RFS/usr/bin/system-config-printer"
got=$(run no)
if [ -z "$got" ] && [ ! -s "$T/warn" ]; then ok "no printer tool at all: nothing is granted, and nothing is said"
else bad "no printer tool at all: nothing is granted, and nothing is said" "cases: $got" "$(cat "$T/warn")"; fi

echo
echo "$n checks, $fail failed"
[ "$fail" = 0 ]
