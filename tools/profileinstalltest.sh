#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  profileinstalltest -- an installer built for a profile installs THAT
#  profile, and nothing else
#
#  One AurOS-Setup.exe is one OS: the profile is fixed when it is built
#  (build/aurbridge, tools/customize/installer_profile.py). This builds the
#  host-side simulation of the phase engine once per profile -- desktop,
#  office, school-kiosk and a brand-new profile written into a scratch
#  directory, so nothing here knows its name -- and for each asks:
#
#    - does the engine write THAT profile into the install journal, the
#      image's manifest, and the file name the image goes under?
#    - does the look rule hold: the person's look card plus the profile's own
#      theme where the look is offered, and nothing at all in
#      \EFI\AurOS\choices.conf where the image decides (a kiosk)?
#    - does the staging environment's finder (src/aurstage/image.c, the
#      same file the product runs) accept that profile's image and REFUSE it
#      for a journal naming any other?
#    - is a malformed or unknown profile a build error, not desktop?
#
#  No root, no QEMU. Needs sgdisk, cc and python3. The product's whole
#  install, with a staging environment booted, is tools/nosticktest.sh
#  (NOSTICK_PROFILE=baked), and the real image's start is
#  tools/profiletest.sh boot.
#    sh tools/profileinstalltest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-68s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-68s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

command -v sgdisk >/dev/null || { echo "need sgdisk"; exit 2; }
# A short path: staging refuses an image whose path is longer than image.h's
# dev[72], and the guard below checks the real layout fits.
T=$(mktemp -d "${TMPDIR:-/tmp}/pit.XXXXXX")
trap 'rm -rf "$T"' EXIT

echo
echo "Does an installer built for a profile install that profile?"
echo

# The longest profile name must fit the path staging builds after the restart:
# <winvol root>/AurOS/auros-<profile>.img into image_src.dev (src/aurstage).
WROOT=$(sed -n 's|^#define WINVOL_ROOT  *"\(.*\)"|\1|p' src/aurstage/winvol.c)
DEVSZ=$(sed -n 's/^ *char  *dev\[\([0-9]*\)\].*/\1/p' src/aurstage/image.h)
LONGEST=$(( ${#WROOT} + 13 + 32 + 4 ))
if [ -n "$DEVSZ" ] && [ "$LONGEST" -lt "$DEVSZ" ]; then
    ok "a 32-character profile still fits staging's image path ($LONGEST < $DEVSZ)"
else
    bad "a 32-character profile still fits staging's image path" "needs $LONGEST, image_src.dev holds ${DEVSZ:-?}"
fi

S=src/aurbridge
# What build/aurbridge generates besides the profile header; the simulation
# only reads the Secure Boot key names from it.
mkdir -p "$T/inc"; printf '#define AUROS_SHIM_CAS ""\n' > "$T/inc/aurbridge-baked.h"
HWGEN="$T/hw"; python3 -I tools/hw/gen-header.py "$HWGEN" >/dev/null || { echo "hw header"; exit 2; }

# A brand-new profile, in a scratch directory: the owner's way in. No C, no
# list, no workflow is edited for it.
mkdir -p "$T/profiles"
cp profiles/*.profile "$T/profiles/"
cat > "$T/profiles/mahjong-lab.profile" <<'EOP'
inherit="desktop"
profile_id="mahjong-lab"
profile_name="AurOS Mahjong Lab"
theme="moss"
shell_archetype="tiles"
packages_apps="gnome-mahjongg"
EOP

# The staging finder, the real image.c, asked the one question the journal's
# profile exists for.
cat > "$T/find.c" <<'EOC'
#include <stdio.h>
#include "image.h"
int main(int argc, char **argv) {
    image_src s; char why[300] = "";
    int r = image_find_file(argv[1], argv[2], argv[3], &s, why, sizeof why);
    printf("find=%d profile=%s why=%s\n", r, r ? "" : s.profile, why);
    return 0;
}
EOC
FIND_OK=1
cc -O1 -std=gnu11 -I src/aurstage -o "$T/find" "$T/find.c" src/aurstage/image.c \
   src/aurstage/gpt.c src/aurstage/wr.c src/aurstage/sha256.c src/aurstage/boot.c \
   src/aurstage/disks.c src/aurstage/ntfs.c src/aurstage/fde.c 2>"$T/find.log" || FIND_OK=0
[ "$FIND_OK" = 1 ] || bad "the staging finder builds" "$(tail -3 "$T/find.log")"

D="$T/disk.img"
truncate -s 3G "$D"
sgdisk -n 1:2048:206847 -t 1:ef00 -n 2:206848:2303999 -t 2:0700 \
       -n 3:5500000:6291422 -t 3:2700 "$D" >/dev/null
printf 'NTFS    ' | dd of="$D" bs=1 seek=$((206848*512+3)) conv=notrunc status=none
head -c 4096 /dev/urandom > "$T/kern"; head -c 4096 /dev/urandom > "$T/initrd"

journal() {
    tail -c 16384 "$1" | python3 -c '
import sys, zlib
d = sys.stdin.buffer.read()
i = d.rfind(b"\x1f\x8b\x08")
out = zlib.decompress(d[i:], 16 + zlib.MAX_WBITS)
j = out.find(b"{")
sys.stdout.write(out[j:out.rfind(b"}") + 1].decode("ascii", "replace"))'
}

# name  "profile dir"  expect-look-offered  expect-theme-in-choices
check() {
    P="$1"; PD="$2"; OFFERED="$3"; THEME="$4"
    H="$T/h-$P"; mkdir -p "$H"
    if ! python3 tools/customize/installer_profile.py "$P" --profiles "$PD" --output "$H/aurbridge-profile.h" 2>"$T/gen.err"; then
        bad "$P: the profile header is generated" "$(cat "$T/gen.err")"; return
    fi
    if ! cc -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -I "$H" -I "$T/inc" -I "$HWGEN" -I src/aurstage -I $S -I hw/lib \
         -o "$T/sim-$P" $S/format.c $S/phases.c $S/bootargs.c $S/inflate.c $S/sbdb.c $S/recipe.c \
         $S/profile_image.c src/aurstage/gpt.c src/aurstage/plan.c $S/simmain.c $S/plat_sim.c $S/pf_sim.c 2>"$T/cc.err"; then
        bad "$P: the simulation builds with that profile baked in" "$(tail -3 "$T/cc.err")"; return
    fi
    SIM="$T/sim-$P"
    "$SIM" profile > "$T/prof.out" 2>&1
    if [ "$(sed -n 's/^profile=\([^ ]*\) .*/\1/p' "$T/prof.out")" = "$P" ] &&
       grep -q " image=auros-$P.img " "$T/prof.out" && grep -q " look_offered=$OFFERED " "$T/prof.out"; then
        ok "$P: the installer knows it installs $P (auros-$P.img, look offered=$OFFERED)"
    else
        bad "$P: the installer knows it installs $P" "$(cat "$T/prof.out")"; return
    fi

    I="$T/win-$P/AurOS/auros-$P.img"; mkdir -p "$T/win-$P/AurOS"
    truncate -s 64M "$I"
    sgdisk -n 1:2048:+16M -t 1:ef00 -n 2:0:0 -t 2:8304 "$I" >/dev/null
    M="$T/m-$P"; mkdir -p "$M/esp"; : > "$M/efivars.txt"
    printf '0\t%s\t%s\t512\tAUROSTEST\tQEMU\t0\n' "$D" "$(stat -c%s "$D" 2>/dev/null || stat -f%z "$D")" > "$M/disks.txt"
    # What the wizard hands over after she picks the "Like a Mac" card.
    AURBRIDGE_SHELL=dock AURBRIDGE_THEME=sandstone AURBRIDGE_LANGUAGE=en_US.UTF-8 \
        "$SIM" "$M" @baked none "$I" "$T/kern" "$T/initrd" > "$T/out" 2> "$T/err"
    if ! grep -q 'verdict=armed' "$T/out"; then
        bad "$P: a build for $P installs" "$(tail -3 "$T/err")"; return
    fi
    journal "$M/esp/EFI/AurOS/staging.img" > "$T/j.json" 2>/dev/null
    if grep -q "\"profile\":\"$P\"" "$T/j.json"; then
        ok "$P: the install journal says profile $P"
    else
        bad "$P: the install journal says profile $P" "$(head -c 500 "$T/j.json")"
    fi
    if [ "$(dd if="$I.manifest" bs=1 skip=68 count=64 2>/dev/null | tr -d '\0')" = "$P" ]; then
        ok "$P: ...and so does the manifest written beside the image"
    else
        bad "$P: ...and so does the manifest written beside the image"
    fi
    CH="$M/esp/EFI/AurOS/choices.conf"
    if [ "$OFFERED" = 1 ]; then
        if grep -qx 'shell=dock' "$CH" && grep -qx "theme=$THEME" "$CH"; then
            ok "$P: her look (dock) with the profile's theme ($THEME) goes to first boot"
        else bad "$P: her look with the profile's theme goes to first boot" "$(cat "$CH")"; fi
    else
        if grep -q '^shell=\|^theme=' "$CH"; then
            bad "$P: a locked look writes no shell or theme (the image decides)" "$(cat "$CH")"
        else ok "$P: a locked look writes no shell or theme (the image decides)"; fi
    fi
    if [ "$FIND_OK" = 1 ]; then
        R=$("$T/find" "$I" "$I.manifest" "$P")
        case "$R" in find=0*profile=$P\ *) ok "$P: staging accepts a journal for $P" ;;
                     *) bad "$P: staging accepts a journal for $P" "$R" ;; esac
        for other in desktop office school-kiosk mahjong-lab; do
            [ "$other" = "$P" ] && continue
            R=$("$T/find" "$I" "$I.manifest" "$other")
            case "$R" in find=-1*"different version of AurOS"*) ;;
                         *) bad "$P: staging REFUSES a journal for $other" "$R"; return ;; esac
        done
        ok "$P: staging refuses a journal for each of the other three, in her words"
    fi
}

check desktop    profiles        1 sandstone
check office     profiles        1 slate
check school-kiosk profiles      0 ""
check mahjong-lab "$T/profiles"  1 moss

# desktop built the old way (no profile header at all) is the same installer
rm -rf "$T/h-none"; mkdir "$T/h-none"
cc -O2 -std=gnu11 -I "$T/h-none" -I "$T/inc" -I "$HWGEN" -I src/aurstage -I $S -I hw/lib -o "$T/sim-none" \
   $S/format.c $S/phases.c $S/bootargs.c $S/inflate.c $S/sbdb.c $S/recipe.c $S/profile_image.c \
   src/aurstage/gpt.c src/aurstage/plan.c $S/simmain.c $S/plat_sim.c $S/pf_sim.c 2>/dev/null
case "$("$T/sim-none" profile)" in
  "profile=desktop image=auros-desktop.img look_offered=1 look= theme= status=0") ok "no profile named: the installer is desktop, exactly as before" ;;
  *) bad "no profile named: the installer is desktop, exactly as before" "$("$T/sim-none" profile)" ;;
esac

# Fail closed at build time: nothing here falls back to desktop.
for n in Office ../office office/ nosuch "" a--b-; do
    if python3 tools/customize/installer_profile.py "$n" --output "$T/x.h" 2>/dev/null || [ -e "$T/x.h" ]; then
        bad "build for '$n' is refused" "it built"
    else ok "build for '$n' is refused, no header left behind"; fi
done

echo
if [ "$fail" -gt 0 ]; then echo "$fail of $checked wrong."; exit 1; fi
echo "$checked checks: an installer installs the profile it was built for, and staging refuses any other."
