#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  AurOS first boot: the Wi-Fi she was on in Windows (decision D10)
#
#  When she left "Bring my Wi-Fi across" ticked on the installer's last
#  page, AurBridge wrote \EFI\AurOS\wifi.conf on the machine's EFI
#  partition: the one network she was connected to, its name, how it signs
#  in, and its key. This turns that into a NetworkManager connection (a
#  keyfile, mode 0600, root's) so AurOS is online the moment it starts,
#  and then DELETES the file from the EFI partition, wherever it came from
#  and whether or not it could be used: the key sits in the clear on a FAT
#  partition anyone can read until this runs, so it does not stay a second
#  longer than it has to.
#
#  THE FILE IS INPUT, NOT CODE. It is never sourced. Three lines, each
#  matched against a pattern before it is used:
#
#      ssid=<1..32 printable ASCII characters>
#      security=wpa-psk | sae | open
#      psk=<8..63 printable ASCII characters, or 64 hex digits>   (none for open)
#
#  Anything else -- another key, a fourth line, a newline inside a value --
#  and nothing is written; the file is still deleted. The key is never
#  printed.
#
#  For tests: AUROS_WIFI_FILE names the file (skipping the search of the EFI
#  partitions; it is still deleted), and AUROS_WIFI_ROOT is a directory
#  standing in for / -- in which case nothing is run, only files are written.
# ═══════════════════════════════════════════════════════════════════
set -u
umask 077
ROOT="${AUROS_WIFI_ROOT:-}"
ESP_TYPE="c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
say() { printf 'wifi: %s\n' "$*"; }

# ── find it, read it, delete it ────────────────────────────────────
#
# Every EFI System partition on every disk, mounted READ-WRITE one at a time
# (the delete needs it), until one holds \EFI\AurOS\wifi.conf. Read first,
# bounded and printable, then removed and the partition let go.
TEXT=""
if [ -n "${AUROS_WIFI_FILE:-}" ]; then
    if [ -f "$AUROS_WIFI_FILE" ]; then
        TEXT=$(head -c 1024 "$AUROS_WIFI_FILE" | tr -cd '\n\040-\176')
        rm -f "$AUROS_WIFI_FILE"
    fi
else
    command -v lsblk >/dev/null 2>&1 || exit 0
    mnt=$(mktemp -d /run/auros-wifi.XXXXXX 2>/dev/null) || exit 0
    for dev in $(lsblk -rpno NAME,PARTTYPE 2>/dev/null | awk -v t="$ESP_TYPE" '$2 == t { print $1 }'); do
        mount -o rw,nodev,nosuid,noexec -t vfat "$dev" "$mnt" 2>/dev/null || continue
        f="$mnt/EFI/AurOS/wifi.conf"
        if [ -f "$f" ]; then
            TEXT=$(head -c 1024 "$f" | tr -cd '\n\040-\176')
            rm -f "$f"
            sync
        fi
        umount "$mnt" 2>/dev/null
        [ -n "$TEXT" ] && break
    done
    rmdir "$mnt" 2>/dev/null
fi
[ -n "$TEXT" ] || exit 0

# ── check it ───────────────────────────────────────────────────────
n=$(printf '%s\n' "$TEXT" | grep -c .)
get() { printf '%s\n' "$TEXT" | sed -n "s/^$1=//p" | tail -n 1; }
matches() { printf '%s' "$1" | grep -Eq "^($2)\$"; }
bad=0
[ "$n" -le 3 ] || bad=1
# Nothing but the three keys, each at most once.
if printf '%s\n' "$TEXT" | grep -v '^$' | grep -Evq '^(ssid|security|psk)='; then bad=1; fi
for k in ssid security psk; do
    [ "$(printf '%s\n' "$TEXT" | grep -c "^$k=")" -le 1 ] || bad=1
done
SSID=$(get ssid); SEC=$(get security); PSK=$(get psk)
[ -n "$SSID" ] && [ "${#SSID}" -le 32 ] || bad=1
matches "$SEC" 'wpa-psk|sae|open' || bad=1
if [ "$SEC" = open ]; then
    [ -z "$PSK" ] || bad=1
else
    if ! { [ "${#PSK}" -ge 8 ] && [ "${#PSK}" -le 63 ]; } && ! matches "$PSK" '[0-9A-Fa-f]{64}'; then bad=1; fi
fi
if [ "$bad" = 1 ]; then
    say "the Wi-Fi file was not in a shape AurOS trusts; nothing was written (and the file is gone)"
    exit 0
fi

# ── write it ───────────────────────────────────────────────────────
DIR="$ROOT/etc/NetworkManager/system-connections"
mkdir -p "$DIR" || exit 0
chmod 700 "$DIR" 2>/dev/null
OUT="$DIR/auros-wifi.nmconnection"
UUID=$(cat /proc/sys/kernel/random/uuid 2>/dev/null || echo 00000000-0000-4000-8000-000000000000)
# keyfile values: backslash and a leading/trailing space are the only
# things that need care, and the printable-ASCII rule above has let both
# through -- so they are escaped, not trusted.
esc() { printf '%s' "$1" | sed 's/\\/\\\\/g'; }
{
    echo '[connection]'
    printf 'id=%s\n' "$(esc "$SSID")"
    echo "uuid=$UUID"
    echo 'type=wifi'
    echo 'autoconnect=true'
    echo
    echo '[wifi]'
    echo 'mode=infrastructure'
    printf 'ssid=%s\n' "$(esc "$SSID")"
    if [ "$SEC" != open ]; then
        echo
        echo '[wifi-security]'
        if [ "$SEC" = sae ]; then echo 'key-mgmt=sae'; else echo 'key-mgmt=wpa-psk'; fi
        printf 'psk=%s\n' "$(esc "$PSK")"
    fi
    echo
    echo '[ipv4]'
    echo 'method=auto'
    echo
    echo '[ipv6]'
    echo 'addr-gen-mode=default'
    echo 'method=auto'
} > "$OUT.new" && chmod 600 "$OUT.new" && mv "$OUT.new" "$OUT" || { rm -f "$OUT.new"; exit 0; }
say "your Wi-Fi ($SSID) came across"
if [ -z "$ROOT" ] && command -v nmcli >/dev/null 2>&1; then
    nmcli connection reload >/dev/null 2>&1 || true
fi
exit 0
