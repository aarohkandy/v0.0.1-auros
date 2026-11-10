#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  stickreporttest.sh -- "Save a report for whoever helps you".
#
#    sh tools/stickreporttest.sh            the checks, on the real script
#    sh tools/stickreporttest.sh mutants    each check shown RED when the thing
#                                           it checks is broken (the script is
#                                           copied, broken, and must fail)
#
#  rootfs/usr/lib/auros/stick-report runs as root. Here it runs in a sandbox:
#  fake journalctl, dmesg, lspci... on PATH print planted private things, a
#  fake findmnt/lsblk say whether the target is a removable stick, and a
#  folder stands in for the mounted stick. Proved:
#    - it refuses a target that is not removable, not a mount point, not
#      absolute, strange, or given with a second argument -- and writes nothing;
#    - a planted psk= line, serial number, UUID, MAC address, host name, file
#      name under /home and network name are NOT in the report;
#    - the NetworkManager connection files are never opened;
#    - the journals are capped at 4000 lines each;
#    - every thing the brief lists is in it, the top says what is and is not.
#  Not proved: the real journalctl/lspci/nmcli on a real PC (homebase VM tests
#  run the script on a loop-mounted exFAT image; see the lane notes).
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.." || exit 1
REPO=$(pwd)
SCRIPT=${AUROS_REPORT_SCRIPT:-$REPO/rootfs/usr/lib/auros/stick-report}
MODE=${1:-checks}

if [ "$MODE" = mutants ]; then
    TMPM=$(mktemp -d "${TMPDIR:-/tmp}/stickreport-mut.XXXXXX") || exit 1
    trap 'rm -rf "$TMPM"' EXIT
    mfail=0
    # name | sed expression applied to a copy of the script
    while IFS='|' read -r name expr; do
        [ -n "$name" ] || continue
        cp "$SCRIPT" "$TMPM/m.sh"
        sed -E "$expr" "$TMPM/m.sh" > "$TMPM/mut.sh"
        if cmp -s "$TMPM/m.sh" "$TMPM/mut.sh"; then
            echo "  MUTANT NOT APPLIED: $name"; mfail=$((mfail+1)); continue
        fi
        if AUROS_REPORT_SCRIPT="$TMPM/mut.sh" sh "$0" checks >"$TMPM/out" 2>&1; then
            echo "  STILL GREEN with: $name"; mfail=$((mfail+1))
        else
            # red for the right reason: some check FAILed (not a script that no longer runs at all)
            grep -q 'FAIL$' "$TMPM/out" || { echo "  red, but for no check: $name"; mfail=$((mfail+1)); continue; }
            echo "  red as it should be, with: $name   [$(grep 'FAIL$' "$TMPM/out" | sed -E 's/ +FAIL$//; s/^ +//' | head -2 | tr '\n' ';')]"
        fi
    done <<'EOF'
removable check dropped|s/^    \|\| refuse "\$src is not a removable USB disk"/    || true/
mount-point check dropped|s/^\[ -n "\$src" \] \|\| refuse.*/src=\/dev\/sdb1/
stick-style file system check dropped|s/^    \*\) refuse "\$T is not a stick-style file system" ;;/    *) : ;;/
absolute path check dropped|s/^case \$T in \/\*\) ;; \*\) refuse.*/:/
second argument allowed|s/^\[ \$# -eq 1 \] \|\| refuse.*/T=${1:-}/
psk label no longer redacted|s/KEYS='psk\|/KEYS='NOPE|/
serial no longer redacted|s/\|serial'$/|NOSERIAL'/
uuid no longer redacted|s/\{8\}-/{9}-/
mac no longer redacted|s/\)\{5\}/){9}/
host name no longer removed|s/length\(host\) > 3/length(host) > 300/
home path no longer removed|s,'s#/home/,'s#/nohome/,
journal not capped|s/-n "\$LINES_MAX" --no-pager/--no-pager/
network name asked for|s/-f DEVICE,TYPE,STATE //
connection files read|s/^sec "AurOS version"/cat "$R"\/etc\/NetworkManager\/system-connections\/*; sec "AurOS version"/
no list of what is left out|s/WHAT IS NOT IN IT, ON PURPOSE/WHAT/
EOF
    [ "$mfail" = 0 ] && { echo "  every check goes red when its thing is broken"; exit 0; }
    echo "  $mfail mutant(s) not caught"; exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/stickreport.XXXXXX") || exit 1
TMP=$(cd "$TMP" && pwd -P)
trap 'rm -rf "$TMP"' EXIT
if [ -n "${KEEP:-}" ]; then trap - EXIT; echo "$TMP"; fi
fail=0
ok()  { printf '    %-62s ok\n' "$1"; }
bad() { fail=$((fail+1)); printf '    %-62s FAIL\n' "$1"; [ -z "${2:-}" ] || printf '        %s\n' "$2"; }

BIN=$TMP/bin; STICK=$TMP/mnt/stick; ROOT=$TMP/root
mkdir -p "$BIN" "$STICK" "$ROOT/etc/auros" "$ROOT/usr/lib" "$ROOT/aurbridge" \
         "$ROOT/etc/NetworkManager/system-connections"

# What the fakes print is the point: every private thing the brief names.
HOSTNAME_PLANTED=grandmas-laptop
cat > "$BIN/hostname" <<EOF
#!/bin/sh
echo $HOSTNAME_PLANTED
EOF
cat > "$BIN/journalctl" <<'EOF'
#!/bin/sh
# honours -n like the real one; prints 6000 lines per boot, a marker per boot
n=; b=0
while [ $# -gt 0 ]; do case "$1" in -n) n=$2; shift ;; -b) b=$2; shift ;; esac; shift; done
{
  i=0; while [ $i -lt 6000 ]; do i=$((i+1)); echo "2026-11-10T10:00:00 grandmas-laptop kernel: JOURNAL-BOOT$b line $i"; done
  echo "2026-11-10T10:00:01 grandmas-laptop NetworkManager[1]: <warn> psk=PLANTED-WIFI-PASSWORD-1 for HomeNet"
  echo "2026-11-10T10:00:02 grandmas-laptop kernel: usb 1-1: SerialNumber: PLANTED-SERIAL-2"
  echo "2026-11-10T10:00:03 grandmas-laptop app[2]: could not open /home/grandma/Documents/PLANTED-TAXES.pdf"
  echo "2026-11-10T10:00:04 grandmas-laptop kernel: mounted 123e4567-e89b-12d3-a456-426614174000"
  echo "2026-11-10T10:00:05 grandmas-laptop kernel: wlan0: aa:bb:cc:dd:ee:ff associated"
} | { if [ -n "$n" ]; then tail -n "$n"; else cat; fi; }
EOF
cat > "$BIN/dmesg" <<'EOF'
#!/bin/sh
echo "[    0.000000] Linux version 6.8 KERNEL-LOG-MARKER"
echo "[    1.000000] ata1: Serial: PLANTED-SERIAL-3"
EOF
cat > "$BIN/lspci" <<'EOF'
#!/bin/sh
echo "00:02.0 VGA compatible controller [0300]: Intel [8086:46a6] LSPCI-MARKER"
EOF
echo '#!/bin/sh
echo "Bus 001 Device 002: ID 8087:0033 Intel LSUSB-MARKER"' > "$BIN/lsusb"
cat > "$BIN/nmcli" <<'EOF'
#!/bin/sh
case "$*" in *-f*) echo "wlan0:wifi:connected" ;; *) echo "wlan0:wifi:connected:PLANTED-SSID-HomeNet" ;; esac
EOF
echo '#!/bin/sh
echo "0: phy0: Wireless LAN RFKILL-MARKER"' > "$BIN/rfkill"
echo '#!/bin/sh
echo "card 0: PCH [HDA Intel PCH] APLAY-MARKER"' > "$BIN/aplay"
echo '#!/bin/sh
echo "Linux grandmas-laptop 6.8.0 UNAME-MARKER x86_64"' > "$BIN/uname"
# the target's disk: FAKE_RM=1 removable USB, 0 an internal disk
cat > "$BIN/lsblk" <<'EOF'
#!/bin/sh
case "$*" in
  *-srno*) if [ "${FAKE_RM:-1}" = 1 ]; then echo "1 usb"; echo "1 "; else echo "0 sata"; echo "0 "; fi ;;
  *) echo "NAME SIZE TYPE FSTYPE MOUNTPOINT"; echo "sda 500G disk"; echo "sdb1 16G part exfat /run/media/grandma/STICK" ;;
esac
EOF
cat > "$BIN/findmnt" <<'EOF'
#!/bin/sh
# findmnt -n -o SOURCE|FSTYPE --mountpoint DIR  : known only for $FAKE_MNT
for a; do d=$a; done
case "$*" in *FSTYPE*) echo "${FAKE_FS:-exfat}"; exit 0 ;; esac   # (a lie for non-mounts: so the SOURCE check is what is tested)
[ "$d" = "$FAKE_MNT" ] || exit 1
echo /dev/sdb1
EOF
chmod 755 "$BIN"/*

cat > "$ROOT/usr/lib/os-release" <<'EOF'
NAME="AurOS"
PRETTY_NAME="AurOS 0.1.0 OSRELEASE-MARKER"
EOF
printf 'language=en_US.UTF-8\nshell=taskbar\nwifi_psk=PLANTED-CONF-PSK\n' > "$ROOT/etc/auros/installer-choices.conf"
printf '{"image_on":"stick","disk_serial":"PLANTED-JOURNAL-SERIAL","profile":"JOURNAL-MARKER"}\n' > "$ROOT/aurbridge/journal.json"
printf '[wifi-security]\npsk=NM-FILE-MUST-NEVER-BE-READ\n' > "$ROOT/etc/NetworkManager/system-connections/HomeNet.nmconnection"

report() { # [FAKE_RM=..] args... -> runs the script
    env AUROS_REPORT_PATH="$BIN:/usr/bin:/bin" AUROS_REPORT_ROOT="$ROOT" FAKE_MNT="$STICK" FAKE_RM="${FAKE_RM:-1}" \
        sh "$SCRIPT" "$@"
}
files_on_stick() { ls -A "$STICK" 2>/dev/null | wc -l | tr -d ' '; }

echo "  refusals (nothing may be written)"
( FAKE_RM=0; report "$STICK" >/dev/null 2>&1 ); rc=$?
[ "$rc" = 3 ] && [ "$(files_on_stick)" = 0 ] && ok "a target on a non-removable disk is refused" || bad "a non-removable target is refused" "rc=$rc files=$(files_on_stick)"
mkdir -p "$TMP/plain"; report "$TMP/plain" >/dev/null 2>&1; rc=$?
[ "$rc" = 3 ] && [ "$(ls -A "$TMP/plain" | wc -l | tr -d ' ')" = 0 ] && ok "a folder that is not a mount point is refused" || bad "not a mount point is refused" "rc=$rc"
(cd "$TMP" && report "mnt/stick" >/dev/null 2>&1); rc=$?
[ "$rc" = 3 ] && ok "a relative path is refused" || bad "a relative path is refused" "rc=$rc"
mkdir -p "$TMP/we ird"; report "$TMP/we ird" >/dev/null 2>&1; rc=$?
[ "$rc" = 3 ] && ok "a path with a space is refused" || bad "a path with a space is refused" "rc=$rc"
report "$STICK" "$TMP/plain" >/dev/null 2>&1; rc=$?
[ "$rc" = 3 ] && [ "$(files_on_stick)" = 0 ] && ok "a second argument is refused" || bad "a second argument is refused" "rc=$rc"
report >/dev/null 2>&1; rc=$?
[ "$rc" = 3 ] && ok "no argument is refused" || bad "no argument is refused" "rc=$rc"
( FAKE_FS=ext4; export FAKE_FS; report "$STICK" >/dev/null 2>&1 ); rc=$?
[ "$rc" = 3 ] && [ "$(files_on_stick)" = 0 ] && ok "a disk formatted for Linux only (not a stick style) is refused" || bad "ext4 target is refused" "rc=$rc"
report /etc >/dev/null 2>&1; rc=$?
[ "$rc" = 3 ] && ok "/etc is refused" || bad "/etc is refused" "rc=$rc"

echo "  a removable stick"
report "$STICK" >"$TMP/out" 2>&1; rc=$?
[ "$rc" = 0 ] && ok "it is written (exit 0)" || bad "it is written" "rc=$rc $(cat "$TMP/out")"
n=$(ls "$STICK" | grep -c '^AurOS-report-.*\.txt$')
[ "$n" = 1 ] && [ "$(files_on_stick)" = 1 ] && ok "exactly one file, AurOS-report-<date>.txt, and no leftover" || bad "exactly one report file" "$(ls -A "$STICK")"
F=$(ls "$STICK"/AurOS-report-*.txt 2>/dev/null | head -1)
[ -f "$F" ] || { bad "report present"; echo "  cannot go on"; exit 1; }
has() { grep -q -- "$1" "$F"; }
for m in OSRELEASE-MARKER UNAME-MARKER KERNEL-LOG-MARKER LSPCI-MARKER LSUSB-MARKER RFKILL-MARKER APLAY-MARKER \
         JOURNAL-MARKER "wlan0:wifi:connected" "language=en_US" "sdb1 16G" "JOURNAL-BOOT0" "JOURNAL-BOOT-1"; do
    has "$m" && ok "it holds: $m" || bad "it holds: $m"
done

echo "  privacy"
for m in PLANTED-WIFI-PASSWORD-1 PLANTED-SERIAL-2 PLANTED-SERIAL-3 PLANTED-TAXES 123e4567 aa:bb:cc:dd:ee:ff \
         PLANTED-CONF-PSK PLANTED-JOURNAL-SERIAL PLANTED-SSID NM-FILE-MUST-NEVER-BE-READ grandmas-laptop; do
    has "$m" && bad "it must NOT hold: $m" || ok "it does not hold: $m"
done
has "psk= \[removed\]" && ok "...the psk line is there, with its value removed" || bad "the psk line is marked removed"
has "WHAT IS NOT IN IT" && has "no Wi-Fi names or passwords" && ok "the top says what is and is not in it" || bad "the top says what is not in it"

echo "  size"
c0=$(grep -c 'JOURNAL-BOOT0 line' "$F"); c1=$(grep -c 'JOURNAL-BOOT-1 line' "$F")
[ "$c0" -le 4000 ] && [ "$c1" -le 4000 ] && [ "$c0" -gt 3000 ] && ok "each journal is capped at 4000 lines (got $c0 and $c1)" || bad "each journal is capped at 4000 lines" "got $c0 and $c1"

echo
[ "$fail" = 0 ] && { echo "  stickreporttest: all green"; exit 0; }
echo "  stickreporttest: $fail FAILED"; exit 1
