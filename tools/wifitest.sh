#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  wifitest — her Wi-Fi comes across (decision D10), and nothing else does
#
#  Two halves of one promise, each checked where it happens:
#
#   the engine (out/aurbridge-sim, a simulated computer made of files)
#     with the box ticked it writes \EFI\AurOS\wifi.conf only when Restart
#     now is pressed: the one network, three lines. Restart later and taking
#     an armed restart back leave no password there. Unticked it writes
#     nothing and takes away one an earlier attempt left. A name or key it
#     cannot write down without doubt is not carried (and the install carries
#     on). The key appears NOWHERE else:
#     not in a log, not in the firmware's variables, not in choices.conf.
#     Closing the installer takes the file away.
#
#   first boot (rootfs/usr/lib/auros/wifi-across.sh)
#     turns the file into a NetworkManager keyfile, mode 0600, and deletes
#     the file from the EFI partition whether or not it could be used. The
#     file is input from a FAT partition anybody with Windows could have
#     written: every hostile shape is refused and writes nothing.
#
#  No root, no KVM.   sh tools/wifitest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

[ -x out/aurbridge-sim ] || { echo "run ./build/aurbridge first"; exit 2; }
command -v sgdisk >/dev/null || { echo "need sgdisk"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/wifitest.XXXXXX")
_clean() { kill $(jobs -p) 2>/dev/null; rm -rf "$T"; }
trap _clean EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo
echo "Does her Wi-Fi come across, and only hers, and only when she agreed?"
echo

D="$T/disk.img"
truncate -s 3G "$D"
sgdisk -n 1:2048:206847 -t 1:ef00 -n 2:206848:2303999 -t 2:0700 \
       -n 3:5500000:6291422 -t 3:2700 "$D" >/dev/null
printf 'NTFS    ' | dd of="$D" bs=1 seek=$((206848*512+3)) conv=notrunc status=none
mkdir -p "$T/win/AurOS"
I="$T/win/AurOS/auros-desktop.img"
truncate -s 64M "$I"
sgdisk -n 1:2048:+16M -t 1:ef00 -n 2:0:0 -t 2:8304 "$I" >/dev/null
head -c 4096 /dev/urandom > "$T/kern"; head -c 4096 /dev/urandom > "$T/initrd"
KEY=hunter2-hunter2-key

# engine NAME [env...]: a fresh simulated machine with $T/wifi.in as the
# Wi-Fi the computer is on (when it exists), the engine run against it.
engine() {
    S="$T/$1"; shift
    rm -rf "$S"; mkdir -p "$S/esp/EFI/AurOS"
    : > "$S/efivars.txt"
    printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D" 2>/dev/null || stat -f%z "$D")" > "$S/disks.txt"
    [ -f "$T/wifi.in" ] && cp "$T/wifi.in" "$S/wifi.txt"
    env "$@" out/aurbridge-sim "$S" desktop none "$I" "$T/kern" "$T/initrd" > "$S/out" 2> "$S/err"
}
wifi_in() { printf '%s\n%s\n%s\n' "$1" "$2" "$3" > "$T/wifi.in"; }
conf() { cat "$S/esp/EFI/AurOS/wifi.conf" 2>/dev/null; }

echo "  the engine"
wifi_in "Home Network" wpa-psk "$KEY"
engine later AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 AURBRIDGE_RESTART_LATER=1
grep -q 'step=prepared bootnext=0' "$S/out" && [ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] \
    && ok "Restart later: no Wi-Fi password is left on the EFI partition" \
    || bad "Restart later: no Wi-Fi password is left on the EFI partition" "$(conf)" "$(cat "$S/out")"

engine on AURBRIDGE_BRING_WIFI=1
grep -q 'verdict=armed' "$S/out" && ok "ticked: the install arms" || bad "ticked: the install arms" "$(tail -2 "$S/err")"
[ "$(conf)" = "ssid=Home Network
security=wpa-psk
psk=$KEY" ] && ok "...and wifi.conf is exactly three lines: name, how it signs in, the key" \
    || bad "...and wifi.conf is exactly three lines" "$(conf)"
# (wifi.txt is the simulated computer's own network profile, the SOURCE.)
leaks=$(grep -rl -- "$KEY" "$S" 2>/dev/null | grep -v '/EFI/AurOS/wifi.conf$' | grep -v '/wifi.txt$')
[ -z "$leaks" ] && ok "...and the key is nowhere else: no log, no firmware variable, no other file" \
    || bad "...and the key is nowhere else" "$leaks"
grep -q 'goes across' "$S/err" && ok "...and says so, without the key" || bad "...and says so"

engine off
[ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] && ok "unticked: nothing is written" || bad "unticked: nothing is written" "$(conf)"
# An earlier attempt's file is taken away by an unticked one.
rm -rf "$T/stale"; mkdir -p "$T/stale"
S="$T/stale"; mkdir -p "$S/esp/EFI/AurOS"; : > "$S/efivars.txt"
printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D" 2>/dev/null || stat -f%z "$D")" > "$S/disks.txt"
printf 'ssid=Old\nsecurity=open\n' > "$S/esp/EFI/AurOS/wifi.conf"
out/aurbridge-sim "$S" desktop none "$I" "$T/kern" "$T/initrd" > "$S/out" 2> "$S/err"
[ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] && ok "...and a file an earlier try left is taken away" || bad "...a file an earlier try left is taken away"

wifi_in "Cafe" open ""
engine open AURBRIDGE_BRING_WIFI=1
[ "$(conf)" = "ssid=Cafe
security=open" ] && ok "an open network: name and 'open', no key line" || bad "an open network" "$(conf)"
wifi_in "Home" sae "$KEY"
engine sae AURBRIDGE_BRING_WIFI=1
[ "$(conf)" = "ssid=Home
security=sae
psk=$KEY" ] && ok "a WPA3 network: sae" || bad "a WPA3 network" "$(conf)"

# What cannot be written down without doubt is not carried -- and the
# install carries on, because AurOS can ask for it on its own screen.
hostile() { # label ssid security key
    wifi_in "$2" "$3" "$4"
    engine hostile AURBRIDGE_BRING_WIFI=1
    if grep -q 'verdict=armed' "$S/out" && [ ! -f "$S/esp/EFI/AurOS/wifi.conf" ]; then
        ok "not carried, install carries on: $1"
    else bad "not carried, install carries on: $1" "$(conf)" "$(tail -2 "$S/err")"; fi
}
hostile "a name of 33 characters" "123456789012345678901234567890123" wpa-psk "$KEY"
hostile "a key of 7 characters" "Home" wpa-psk "short77"
hostile "a key with a letter outside ASCII" "Home" wpa-psk "$(printf 'passw\303\251rd-long')"
hostile "a work or school sign-in" "Corp" wpa2-enterprise "$KEY"
hostile "an open network that has a key" "Home" open "$KEY"
hostile "a 64-character key that is not hex" "Home" wpa-psk "$(printf 'z%.0s' $(seq 1 64))"
if wifi_in "No Wi-Fi" wpa-psk "$KEY" && rm -f "$T/wifi.in" && engine nowifi AURBRIDGE_BRING_WIFI=1 &&
   grep -q 'verdict=armed' "$S/out" && [ ! -f "$S/esp/EFI/AurOS/wifi.conf" ]; then
    ok "not on Wi-Fi at all: nothing is written, the install carries on"
else bad "not on Wi-Fi at all"; fi

wifi_in "Home" wpa-psk "$KEY"
engine aborted AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ABORT=1
[ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] && grep -q 'verdict=aborted' "$S/out" \
    && ok "closing the installer takes the file away" || bad "closing the installer takes the file away" "$(conf)"

engine disarmed AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 AURBRIDGE_DISARM=1
[ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] && grep -q 'step=disarmed bootnext=0' "$S/out" \
    && ok "a restart failure rollback takes the Wi-Fi password away" \
    || bad "a restart failure rollback takes the Wi-Fi password away" "$(conf)" "$(cat "$S/out")"

engine rearmed AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 AURBRIDGE_DISARM=1 AURBRIDGE_REARM=1
[ -f "$S/esp/EFI/AurOS/wifi.conf" ] && grep -q 'step=rearmed rc=0 bootnext=1' "$S/out" \
    && ok "the same prepared install can retry after arming was taken back" \
    || bad "the same prepared install can retry after arming was taken back" "$(conf)" "$(cat "$S/out")"

engine stalefail AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 AURBRIDGE_SIM_FAIL_WIFI_DELETE_AT=2
grep -q 'step=armed rc=-1 bootnext=0' "$S/out" && grep -q 'old Wi-Fi password copy could not be removed' "$S/err" \
    && ok "if an old Wi-Fi copy cannot be removed, Restart now refuses" \
    || bad "if an old Wi-Fi copy cannot be removed, Restart now refuses" "$(cat "$S/out")" "$(tail -4 "$S/err")"

for failkind in partial full; do
    engine "write-$failkind" AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 \
        AURBRIDGE_SIM_FAIL_WIFI_PUT="$failkind"
    [ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] && grep -q 'step=armed rc=0 bootnext=1' "$S/out" \
        && ok "a $failkind failed Wi-Fi write is deleted before boot is armed" \
        || bad "a $failkind failed Wi-Fi write is deleted before boot is armed" "$(conf)" "$(cat "$S/out")"
done

engine unsafeput AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 \
    AURBRIDGE_SIM_FAIL_WIFI_PUT=partial AURBRIDGE_SIM_FAIL_WIFI_DELETE_AT=3
grep -q 'step=armed rc=-1 bootnext=0' "$S/out" && grep -q 'incomplete Wi-Fi password copy could not be removed' "$S/err" \
    && [ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] \
    && ok "a failed write refuses to arm and outer rollback retries cleanup" \
    || bad "a failed write refuses to arm and outer rollback retries cleanup" "$(conf)" "$(cat "$S/out")" "$(tail -4 "$S/err")"

engine noesp AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 AURBRIDGE_SIM_FAIL_ESP_OPEN_AT=2
[ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] && grep -q 'step=armed rc=-1 bootnext=0' "$S/out" \
    && grep -q 'could not be checked for an old Wi-Fi password copy' "$S/err" \
    && ok "if the EFI partition cannot reopen, Restart now refuses and cleanup retries" \
    || bad "if the EFI partition cannot reopen, Restart now refuses and cleanup retries" "$(conf)" "$(cat "$S/out")" "$(tail -4 "$S/err")"

engine bootfail AURBRIDGE_BRING_WIFI=1 AURBRIDGE_ARM_LATER=1 AURBRIDGE_SIM_FAIL_BOOTNEXT=1
[ ! -f "$S/esp/EFI/AurOS/wifi.conf" ] && grep -q 'step=armed rc=-1 bootnext=0' "$S/out" \
    && ok "if BootNext fails after the copy, the Wi-Fi password is removed" \
    || bad "if BootNext fails after the copy, the Wi-Fi password is removed" "$(conf)" "$(cat "$S/out")"

wifi_in "Second Network" wpa-psk "second-password"
cp "$T/wifi.in" "$S/wifi.txt"
env AURBRIDGE_BRING_WIFI=1 out/aurbridge-sim "$S" desktop none "$I" "$T/kern" "$T/initrd" \
    > "$S/retry.out" 2> "$S/retry.err"
[ "$(conf)" = "ssid=Second Network
security=wpa-psk
psk=second-password" ] && ok "retry reads and carries the current Wi-Fi password" \
    || bad "retry reads and carries the current Wi-Fi password" "$(conf)" "$(tail -3 "$S/retry.err")"

echo
echo "  first boot"
SCRIPT=rootfs/usr/lib/auros/wifi-across.sh
run() { # label  file-content   -> $R (a root), file $T/w.conf
    R="$T/root-$1"; rm -rf "$R"; mkdir -p "$R"
    printf '%s' "$2" > "$T/w.conf"
    AUROS_WIFI_FILE="$T/w.conf" AUROS_WIFI_ROOT="$R" sh "$SCRIPT" > "$T/run.out" 2>&1
    KF="$R/etc/NetworkManager/system-connections/auros-wifi.nmconnection"
}
run good "ssid=Home Network
security=wpa-psk
psk=$KEY
"
[ -f "$KF" ] && ok "a keyfile is written" || bad "a keyfile is written" "$(cat "$T/run.out")"
[ "$(stat -c %a "$KF" 2>/dev/null || stat -f %Lp "$KF")" = 600 ] && ok "...readable by root alone (0600)" || bad "...readable by root alone (0600)"
grep -qx 'key-mgmt=wpa-psk' "$KF" && grep -qx "psk=$KEY" "$KF" && grep -qx 'ssid=Home Network' "$KF" && grep -qx 'type=wifi' "$KF" \
    && ok "...with the name, the key and how it signs in" || bad "...with the name, the key and how it signs in" "$(sed 's/^psk=.*/psk=<hidden>/' "$KF")"
[ ! -e "$T/w.conf" ] && ok "...and the copy on the EFI partition is deleted" || bad "...and the copy on the EFI partition is deleted"
[ "$(find "$R" -type f | wc -l)" = 1 ] && ok "...and nothing else was written" || bad "...and nothing else was written" "$(find "$R" -type f)"
grep -q -- "$KEY" "$T/run.out" && bad "...and the key is never printed" "$(cat "$T/run.out")" || ok "...and the key is never printed"

run open "ssid=Cafe
security=open
"
[ -f "$KF" ] && ! grep -q 'wifi-security' "$KF" && ok "an open network: no security section" || bad "an open network: no security section"
run sae "ssid=Home
security=sae
psk=$KEY
"
grep -qx 'key-mgmt=sae' "$KF" 2>/dev/null && ok "WPA3: key-mgmt=sae" || bad "WPA3: key-mgmt=sae"
run hex "ssid=Home
security=wpa-psk
psk=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
"
[ -f "$KF" ] && ok "a 64-digit hex key is accepted" || bad "a 64-digit hex key is accepted"
run bs "ssid=Home
security=wpa-psk
psk=pass\\word-with-a-backslash
"
grep -qx 'psk=pass\\\\word-with-a-backslash' "$KF" 2>/dev/null && ok "a backslash is escaped, not trusted" || bad "a backslash is escaped, not trusted" "$(sed 's/^psk=.*/psk=<hidden>/' "$KF" 2>/dev/null)"

refused() { # label content
    run refused "$2"
    if [ ! -e "$KF" ] && [ ! -e "$T/w.conf" ] && [ "$(find "$R" -type f | wc -l)" = 0 ]; then
        ok "refused, nothing written, the file still deleted: $1"
    else bad "refused: $1" "$(find "$R" -type f) $(ls "$T/w.conf" 2>/dev/null)"; fi
}
refused "a fourth line" "ssid=Home
security=wpa-psk
psk=$KEY
autoconnect-priority=999
"
refused "a key NetworkManager would take as its own" "ssid=Home
security=wpa-psk
psk=$KEY
[connection]
"
refused "an unknown key" "ssid=Home
security=wpa-psk
foo=bar
"
refused "two names" "ssid=A
ssid=B
security=open
"
refused "a key of 7 characters" "ssid=Home
security=wpa-psk
psk=short77
"
refused "an open network with a key" "ssid=Home
security=open
psk=$KEY
"
refused "a security kind it does not know" "ssid=Home
security=wep
psk=$KEY
"
refused "a name of 33 characters" "ssid=123456789012345678901234567890123
security=open
"
refused "no name" "security=open
"
refused "nothing sensible at all" "hello world
"

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: her Wi-Fi comes across only when she agreed, as three lines that"
echo "become a root-only keyfile, and the key is written nowhere else."
