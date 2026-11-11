#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  healthtest — the two refusals that come before the irreversible step
#
#  R6: "Refuse on battery <50% or not on AC."
#  R5: "Refuse on any pending/uncorrectable sectors, no override."
#
#  NEITHER OF THESE CAN BE TESTED BY WAITING FOR ONE TO HAPPEN. Nobody
#  has a failing drive on the day they write the code that refuses one,
#  and this container has no battery at all. So both are driven from
#  constructed inputs: a synthetic /sys/class/power_supply, and raw ATA
#  SMART data pages built byte by byte from the specification.
#
#  A refusal that has never once fired is a refusal nobody should
#  trust.
#
#    sh tools/healthtest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-56s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-56s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/healthtest.XXXXXX")
# A kill (timeout, ^C) must not leak the images in $TMP: the EXIT trap alone
# does not run on a signal in dash, and a leaked 3 GB disk image per run
# once filled homebase. INT and TERM exit, which runs the EXIT trap, and
# the background QEMU goes with it.
_a2_clean() { kill $(jobs -p) 2>/dev/null; rm -rf "$TMP"; }
trap _a2_clean EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo
echo "Would it refuse to start the one step that cannot be stopped?"
echo

cat > "$TMP/h.c" <<'EOC'
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "health.h"

/* Build an ATA SMART data page with the attributes named on the
 * command line, laid out the way a drive lays them out: a two-byte
 * revision, then thirty twelve-byte entries of id, flags, current,
 * worst, and a 48-bit little-endian raw value. */
static void put_attr(unsigned char *p, int slot, int id, unsigned long long raw)
{
    unsigned char *a = p + 2 + slot * 12;
    a[0] = (unsigned char)id; a[1] = 0x0B; a[2] = 0x00;
    a[3] = 100; a[4] = 100;
    for (int i = 0; i < 6; i++) a[5 + i] = (unsigned char)(raw >> (i * 8));
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "power")) {
        power_state ps; char why[200];
        power_read(argv[2], &ps);
        int go = power_ok(&ps, why, sizeof why);
        /* One field per line. They were on one line, and the
         * extractor below anchors at the start of a line, so every
         * field but the first came back empty and eleven correct
         * answers read as failures. */
        printf("go=%d\n", go);
        printf("mains=%d\n", ps.on_mains);
        printf("battery=%d\n", ps.has_battery);
        printf("percent=%d\n", ps.percent);
        printf("why=%s\n", why);
        return go ? 0 : 1;
    }
    /* The second look: power again <first-root> <second-root>. */
    if (argc >= 4 && !strcmp(argv[1], "again")) {
        power_state first, now; char why[200];
        power_read(argv[2], &first);
        power_read(argv[3], &now);
        int go = power_still_ok(&first, &now, why, sizeof why);
        printf("go=%d\n", go);
        printf("why=%s\n", why);
        return go ? 0 : 1;
    }
    if (argc >= 2 && !strcmp(argv[1], "smart")) {
        unsigned char page[512];
        memset(page, 0, sizeof page);
        int slot = 0;
        for (int i = 2; i + 1 < argc; i += 2)
            put_attr(page, slot++, atoi(argv[i]), strtoull(argv[i+1], NULL, 10));
        smart_state st;
        smart_parse_ata(page, &st);
        printf("verdict=%s\n", smart_verdict_name(st.verdict));
        printf("realloc=%llu\n", (unsigned long long)st.reallocated);
        printf("pending=%llu\n", (unsigned long long)st.pending);
        printf("uncorr=%llu\n", (unsigned long long)st.uncorrectable);
        printf("hours=%llu\n", (unsigned long long)st.power_on_hours);
        printf("why=%s\n", st.why);
        printf("remedy=%s\n", st.remedy);
        return st.verdict == SMART_FAILING ? 1 : 0;
    }
    fputs("usage: h power <sysfs-root> | h again <first> <second> | h smart <id> <raw> ...\n", stderr);
    return 2;
}
EOC
gcc -O1 -g -std=gnu11 -Wall -Wextra -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I src/aurstage -o "$TMP/h" \
    "$TMP/h.c" src/aurstage/health.c || { echo "  did not build"; exit 2; }

field() { "$TMP/h" "$@" 2>/dev/null | sed -n "s/^$FIELD=//p"; }

# ── a synthetic machine's power supplies ────────────────────────────
mkps() { # dir type [online] [capacity] [status]
    mkdir -p "$1"; printf '%s\n' "$2" > "$1/type"
    [ $# -ge 3 ] && printf '%s\n' "$3" > "$1/online"
    [ $# -ge 4 ] && printf '%s\n' "$4" > "$1/capacity"
    [ $# -ge 5 ] && printf '%s\n' "$5" > "$1/status"
    return 0
}

echo "  the power"

R="$TMP/ps-desktop"; mkdir -p "$R"
# A desktop reports nothing at all. It cannot run out of charge because
# it was never on any, and refusing those would refuse most of the
# machines this product is for.
FIELD=go; got=$(field power "$R")
[ "$got" = "1" ] && ok "a desktop with no power supplies is allowed" \
                 || bad "a desktop with no power supplies is allowed" "go=$got"

R="$TMP/ps-plugged"; mkps "$R/AC" Mains 1; mkps "$R/BAT0" Battery "" 42 Charging
FIELD=go; got=$(field power "$R")
[ "$got" = "1" ] && ok "a laptop plugged in at 42% is allowed" \
                 || bad "a laptop plugged in at 42% is allowed" "go=$got"

R="$TMP/ps-unplugged"; mkps "$R/AC" Mains 0; mkps "$R/BAT0" Battery "" 95 Discharging
FIELD=go; got=$(field power "$R")
[ "$got" = "0" ] && ok "a laptop on battery is refused even at 95%" \
                 || bad "a laptop on battery is refused even at 95%" "go=$got"
FIELD=why; case "$(field power "$R")" in
  *battery*95*) ok "...and is told how much is left" ;;
  *) bad "...and is told how much is left" "it said: $(field power "$R")" ;;
esac

R="$TMP/ps-two"; mkps "$R/AC" Mains 0
mkps "$R/BAT0" Battery "" 3 Discharging; mkps "$R/BAT1" Battery "" 88 Discharging
FIELD=percent; got=$(field power "$R")
[ "$got" = "88" ] && ok "with two batteries it reads the fuller one" \
                  || bad "with two batteries it reads the fuller one" "percent=$got"

R="$TMP/ps-usbc"; mkps "$R/ucsi" USB_PD 1; mkps "$R/BAT0" Battery "" 20 Charging
FIELD=go; got=$(field power "$R")
[ "$got" = "1" ] && ok "a USB-C charger counts as being plugged in" \
                 || bad "a USB-C charger counts as being plugged in" "go=$got"

# THE SECOND LOOK, just before the line. A machine that showed a battery at
# the first look and shows none now is a laptop that could not be read, not
# a desktop, and the second look must not wave it through.
R="$TMP/ps-gone"; mkdir -p "$R"      # a sysfs that has lost everything
FIELD=go
got=$(field again "$TMP/ps-plugged" "$TMP/ps-plugged")
[ "$got" = "1" ] && ok "again: battery and mains, then battery and mains, is allowed" \
                 || bad "again: battery and mains, then battery and mains, is allowed" "go=$got"
got=$(field again "$TMP/ps-plugged" "$TMP/ps-unplugged")
[ "$got" = "0" ] && ok "again: unplugged since the first look is refused" \
                 || bad "again: unplugged since the first look is refused" "go=$got"
got=$(field again "$TMP/ps-unplugged" "$TMP/ps-unplugged")
[ "$got" = "0" ] && ok "again: a battery with no mains at both looks is refused" \
                 || bad "again: a battery with no mains at both looks is refused" "go=$got"
got=$(field again "$TMP/ps-plugged" "$R")
[ "$got" = "0" ] && ok "again: a battery at the first look and nothing now is refused" \
                 || bad "again: a battery at the first look and nothing now is refused" "go=$got"
got=$(field again "$TMP/ps-plugged" "$TMP/does-not-exist")
[ "$got" = "0" ] && ok "again: a battery at the first look and an unreadable sysfs is refused" \
                 || bad "again: a battery at the first look and an unreadable sysfs is refused" "go=$got"
got=$(field again "$TMP/ps-desktop" "$TMP/ps-desktop")
[ "$got" = "1" ] && ok "again: a desktop that showed no battery at either look is allowed" \
                 || bad "again: a desktop that showed no battery at either look is allowed" "go=$got"
FIELD=why; case "$(field again "$TMP/ps-plugged" "$R")" in
  *"no longer see"*"battery"*) ok "...and she is told AurOS can no longer see the battery" ;;
  *) bad "...and she is told AurOS can no longer see the battery" "it said: $(field again "$TMP/ps-plugged" "$R")" ;;
esac

echo
echo "  the drive"

FIELD=verdict; got=$(field smart 5 0 197 0 198 0 9 12000)
[ "$got" = "good" ] && ok "a healthy drive is good" || bad "a healthy drive is good" "$got"

# 197 is Current_Pending_Sector: sectors the drive can no longer read
# and has not yet been able to move. R5 makes this an unconditional
# refusal, and it is the single most important number in this file.
FIELD=verdict; got=$(field smart 5 0 197 1 198 0 9 40000)
[ "$got" = "failing" ] && ok "ONE pending sector is a refusal" \
                       || bad "ONE pending sector is a refusal" "$got"
FIELD=remedy; case "$(field smart 5 0 197 1 198 0 9 40000)" in
  *"USB stick"*|*"external drive"*) ok "...and she is told to copy her files off TODAY" ;;
  *) bad "...and she is told to copy her files off TODAY" \
         "it said: $(field smart 5 0 197 1 198 0 9 40000)" ;;
esac

FIELD=verdict; got=$(field smart 5 0 197 0 198 3 9 40000)
[ "$got" = "failing" ] && ok "an uncorrectable sector is a refusal" \
                       || bad "an uncorrectable sector is a refusal" "$got"

# Reallocated sectors alone are a drive that had trouble and dealt with
# it. Refusing those refuses a great many working computers.
FIELD=verdict; got=$(field smart 5 24 197 0 198 0 9 50000)
[ "$got" = "worn" ] && ok "reallocated sectors alone are a warning, not a refusal" \
                    || bad "reallocated sectors alone are a warning, not a refusal" "$got"

# A 48-bit raw value, at the top of its range: the reader must not
# sign-extend it or truncate it to 32 bits.
FIELD=pending; got=$(field smart 197 281474976710655)
[ "$got" = "281474976710655" ] && ok "a 48-bit raw value is read whole" \
                              || bad "a 48-bit raw value is read whole" "$got"

# A page of zeroes is a drive that answered with nothing. It must not
# come out as "good": nothing was measured.
FIELD=verdict; got=$(field smart)
[ "$got" = "good" ] && ok "an empty page reads as no bad sectors" \
                    || bad "an empty page reads as no bad sectors" "$got"

echo
echo "  and the real drive in this machine"
D=$(lsblk -dno NAME,TYPE 2>/dev/null | awk '$2=="disk" && $1 !~ /^(zram|loop)/ {print $1; exit}')
if [ -n "$D" ] && [ -r "/dev/$D" ]; then
    gcc -O1 -std=gnu11 -I src/aurstage -o "$TMP/real" -x c - src/aurstage/health.c <<'EOC'
#include <stdio.h>
#include "health.h"
int main(int c, char **v){ smart_state s; smart_read(v[1], &s);
  printf("      %s: %s\n", smart_verdict_name(s.verdict), s.why); return 0; }
EOC
    "$TMP/real" "/dev/$D"
    ok "reading a real drive does not crash"
else
    echo "    (no readable whole disk here)"
fi

# THE POWER IS LOOKED AT AGAIN BEFORE THE LINE, not only at the start. Both
# gates (the Windows-side fast path and the no-stick install) hash the whole
# copy of AurOS, read every sector about to be freed and save the Windows
# start-up between the first power look and the shrink; a charger knocked out
# in those minutes must still stop the install while nothing has changed. The
# staging environment cannot be driven from here, so the order is read from the
# source: every step(REC_GATE_OK) is preceded by power_still_on(), and that
# function refuses with "on-battery" when power_ok says no.
echo
echo "  and again at the line, not only at the start"
gates=$(grep -c 'step(REC_GATE_OK' src/aurstage/install.c)
guarded=$(awk '/power_still_on\(\);/ {p=NR} /step\(REC_GATE_OK/ {if (p && NR-p<=1) n++} END {print n+0}' src/aurstage/install.c)
[ "$gates" -ge 2 ] && [ "$gates" = "$guarded" ] \
  && ok "every gate looks at the power just before it is passed" \
  || bad "every gate looks at the power just before it is passed" "$guarded of $gates gates are preceded by power_still_on()"
sed -n '/^static void power_still_on/,/^}/p' src/aurstage/install.c > "$TMP/psl.txt"
# On the fast path Windows has already made room, so "nothing has been changed" would be untrue there.
grep -q 'Nothing has been changed' "$TMP/psl.txt" \
  && bad "the power refusal does not claim nothing was changed (untrue on the fast path)" "$(cat "$TMP/psl.txt")" \
  || ok "the power refusal does not claim nothing was changed (untrue on the fast path)"
grep -q 'power_read(NULL' "$TMP/psl.txt" && grep -q '!power_still_ok(&g_first_power' "$TMP/psl.txt" && grep -q '"on-battery"' "$TMP/psl.txt" \
  && ok "...and that look refuses, with nothing changed, when it is not on the mains" \
  || bad "...and that look refuses, with nothing changed, when it is not on the mains" "$(cat "$TMP/psl.txt")"
# ...and it is given the FIRST look to compare with: both first looks (the
# gap path and the whole install) keep what they saw.
kept=$(grep -c 'g_first_power = pw;' src/aurstage/install.c)
firsts=$(grep -c 'power_read(NULL, &pw);' src/aurstage/install.c)
# one of the power_read calls is the second look itself (in power_still_on)
[ "$kept" -ge 2 ] && [ "$((firsts - 1))" = "$kept" ] \
  && ok "...and each first look is kept for the second to compare with" \
  || bad "...and each first look is kept for the second to compare with" "$kept kept of $((firsts - 1)) first looks"

echo
if [ "$fail" -gt 0 ]; then
    echo "$fail of $checked wrong."
    echo "These two refusals stand between a person and the one step in this"
    echo "product that cannot be undone."
    exit 1
fi
echo "$checked checks: it refuses a machine on battery, and it refuses a"
echo "drive with a sector it can no longer read."
exit 0
