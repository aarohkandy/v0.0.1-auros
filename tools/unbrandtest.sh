#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  unbrandtest — does the image stop wearing Ubuntu's name and artwork,
#  without breaking Ubuntu's updates or GTK's icons?
#  (docs/legal/COMPLIANCE.md TM-1 and LIC-5; build/forge divert_identity,
#  write_lsb_release, unbrand_icons.)
#
#    container     in a throwaway ubuntu:24.04 container, as root, with
#                  network. Installs adwaita-icon-theme (which pulls in
#                  ubuntu-mono and humanity-icon-theme, as on the image),
#                  then runs build/forge's own code for these steps (read
#                  out of the file, not copied):
#                    - if something else needs the artwork, the build stops
#                    - then the real run: Ubuntu's icon themes are gone,
#                      adwaita is not, apt and dpkg are consistent, apt
#                      will not fetch them back
#                    - the image says AurOS in os-release and lsb-release
#                      (lsb_release itself reads os-release only)
#                    - base-files reinstalled, then moved to the oldest
#                      version Ubuntu has and back (an update whose own
#                      os-release and lsb-release differ) leaves both as
#                      they were, with the image's own dpkg options
#                      (53auros-updates: confdef, confold)
#                    - a second run changes nothing
#    image ROOTFS  on a BUILT image (root, network, chroot; the image is
#                  not modified, an overlay or a copy is): the same
#                  survival check on the real thing -- base-files reinstalled
#                  with the image's own apt policy, the identity and the
#                  icons the same afterwards, apt consistent, the dpkg
#                  records clean.
#
#  FORGE=path reads the functions from another copy of build/forge (the
#  mutants do). Exit 0 all passed, 1 a check failed, 2 it could not run.
# ═══════════════════════════════════════════════════════════════════
set -u
export LC_ALL=C DEBIAN_FRONTEND=noninteractive
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FORGE=${FORGE:-$ROOT/build/forge}

fail=0; checked=0
ok()   { checked=$((checked+1)); printf '    %-70s ok\n' "$1"; }
bad()  { checked=$((checked+1)); fail=$((fail+1)); printf '    %-70s FAIL\n' "$1"
         shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
need() { echo "unbrandtest: $*" >&2; exit 2; }
check() { # "what" cmd...   (cmd runs in the image)
    local w=$1; shift
    if inr "$@" >/dev/null 2>&1; then ok "$w"; else bad "$w"; fi
}

[ "$(id -u)" = 0 ] || need "run as root"
[ -f "$FORGE" ] || need "no $FORGE"

# A function of build/forge, as written there.
fn() { awk -v n="$1" '$0 ~ "^" n "\\(\\) \\{" {f=1} f {print} f && /^}/ {exit}' "$FORGE"; }
# The lines of stage_configure that write the identity (the divert, os-release, lsb-release, the icons).
block() {
    awk '/^    rm -f "\$RFS\/etc\/os-release"$/ {f=1} f {print} f && /^    unbrand_icons$/ {exit}' "$FORGE" | sed 's/^    //'
}

verify() { # both modes: the state afterwards
    echo
    echo "== the icon themes"
    check "ubuntu-mono is not installed" sh -c '! dpkg-query -W -f="\${Status}" ubuntu-mono 2>/dev/null | grep -q "ok installed"'
    check "humanity-icon-theme is not installed" sh -c '! dpkg-query -W -f="\${Status}" humanity-icon-theme 2>/dev/null | grep -q "ok installed"'
    check "...and neither theme is on the disk" sh -c '! ls /usr/share/icons | grep -qi "ubuntu-mono\|humanity"'
    check "GTK's icons stay: adwaita-icon-theme is installed" sh -c 'dpkg-query -W -f="\${Status}" adwaita-icon-theme | grep -q "ok installed"'
    check "...with its dependency answered by aurosicons" sh -c 'dpkg-query -W -f="\${Status} \${Provides}" aurosicons | grep -q "ok installed.*ubuntu-mono"'
    # The built image HAS packages in that namespace on purpose (auros, auros-config,
    # auros-desktop, ...; RC5 run 40040610042, build log "registered with dpkg"): the
    # stand-in must not be one of them, so none of them provides ubuntu-mono.
    check "...and it is not in auros-*, the update channel's namespace (keep-previous, rollback)" sh -c '! dpkg-query -W -f="\${Package} \${Provides}\n" auros "auros-*" 2>/dev/null | grep -q ubuntu-mono'
    check "apt-get check is clean" apt-get check
    check "dpkg --audit has nothing to say" sh -c '[ -z "$(dpkg --audit)" ]'
    check "apt will not put Ubuntu's icon theme back (pinned out)" sh -c '! apt-get -s install ubuntu-mono 2>&1 | grep -q "^Inst ubuntu-mono"'
    check "a full upgrade removes nothing and does not bring it back" sh -c '! apt-get -s full-upgrade 2>&1 | grep -q "^Remv\|^Purg\|^Inst ubuntu-mono\|^Inst humanity"'
    echo
    echo "== who the image says it is"
    check "os-release says NAME=\"AurOS\" and ID=auros" sh -c 'grep -qx "NAME=\"AurOS\"" /usr/lib/os-release && grep -qx "ID=auros" /usr/lib/os-release'
    check "lsb-release says DISTRIB_ID=AurOS" sh -c 'grep -qx "DISTRIB_ID=AurOS" /etc/lsb-release'
    check "...and still names Ubuntu's series (noble, 24.04) for the tools that read it" sh -c 'grep -qx "DISTRIB_CODENAME=noble" /etc/lsb-release && grep -qx "DISTRIB_RELEASE=24.04" /etc/lsb-release'
    check "lsb_release -i -s prints AurOS" sh -c '[ "$(lsb_release -i -s)" = AurOS ]'
    check "both files are diverted, so base-files cannot write over them" sh -c 'dpkg-divert --list /usr/lib/os-release | grep -q distrib && dpkg-divert --list /etc/lsb-release | grep -q distrib'
}

survives() { # base-files reinstalled, downgraded and upgraded the way an update does it
    local old cur o="-o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold"
    [ "$MODE" = image ] && o=""        # the image carries its own (53auros-updates)
    echo
    echo "== base-files reinstalled (what an update of it does)"
    if inr apt-get install -y -q --reinstall $o base-files >"$T/re.log" 2>&1 </dev/null; then
        ok "apt-get install --reinstall base-files succeeds"
    else
        bad "apt-get install --reinstall base-files succeeds" "$(tail -4 "$T/re.log")"
    fi
    still "after a reinstall"
    echo
    echo "== base-files moved to the oldest version Ubuntu has, and back"
    cur=$(inr dpkg-query -W -f='${Version}' base-files)
    old=$(inr apt-cache madison base-files | awk '{print $3}' | tail -1)
    if [ -z "$old" ] || [ "$old" = "$cur" ]; then
        echo "    (Ubuntu has only $cur of base-files: no older version to move through)"
    else
        for v in "$old" "$cur"; do
            if inr apt-get install -y -q --allow-downgrades $o "base-files=$v" >"$T/mv.log" 2>&1 </dev/null; then
                ok "base-files $v installs without a question"
            else
                bad "base-files $v installs without a question" "$(tail -4 "$T/mv.log")"
            fi
            still "at base-files $v"
        done
        check "...and dpkg is left with nothing half-done" sh -c '[ -z "$(dpkg --audit)" ] && apt-get check'
    fi
}
still() {
    check "os-release still says AurOS $1" sh -c 'grep -qx "NAME=\"AurOS\"" /usr/lib/os-release && ! grep -qi "^NAME=.*ubuntu" /usr/lib/os-release'
    check "lsb-release still says AurOS $1" sh -c 'grep -qx "DISTRIB_ID=AurOS" /etc/lsb-release'
    check "Ubuntu's os-release went to os-release.distrib, where nothing reads it $1" sh -c 'grep -q "^NAME=\"Ubuntu\"" /usr/lib/os-release.distrib'
}

T=$(mktemp -d)
trap 'umount "$T/m/dev/pts" "$T/m/dev" "$T/m/proc" "$T/m" 2>/dev/null; rm -rf "$T"' EXIT INT TERM

MODE=${1:-}
case "$MODE" in
container)
    [ -f /.dockerenv ] || [ -f /run/.containerenv ] || need "refusing to run outside a container: it diverts base-files' files"
    inr() { /usr/bin/env -i HOME=/root PATH=/usr/sbin:/usr/bin:/sbin:/bin LC_ALL=C DEBIAN_FRONTEND=noninteractive "$@"; }
    apt-get update -qq >/dev/null 2>&1 || need "apt-get update failed (network?)"
    apt-get install -y -qq --no-install-recommends adwaita-icon-theme lsb-release >/dev/null 2>&1 || need "could not install adwaita-icon-theme"
    dpkg-query -W -f='${Status}' ubuntu-mono 2>/dev/null | grep -q 'ok installed' \
        || need "adwaita-icon-theme did not bring ubuntu-mono: nothing to prove (Ubuntu changed the dependency?)"
    # forge's environment for these steps
    RFS=/; brand_name=AurOS; os_version=0.1.0; brand_short=auros; os_codename=Nocturne; base_suite=noble
    brand_url=https://auros.example
    inchroot() { /usr/bin/env -i HOME=/root PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
                     DEBIAN_FRONTEND=noninteractive LC_ALL=C /bin/bash -c "$*"; }
    die()  { echo "      (the build stops: $*)" | head -2; exit 33; }
    note() { :; }
    for f in pkg_installed divert_identity write_lsb_release unbrand_icons; do
        body=$(fn $f); [ -n "$body" ] || { bad "build/forge has $f" "no such function in $FORGE"; continue; }
        eval "$body"
    done
    STEP=$(block); [ -n "$STEP" ] || { bad "build/forge has the identity step" "could not find it in stage_configure"; echo; echo "unbrandtest: $fail of $checked FAILED"; exit 1; }

    echo
    echo "== if something else needs the artwork, the build stops (and takes nothing with it)"
    cat > "$T/g.control" <<'EOL'
Package: auros-guardtest
Version: 1
Architecture: all
Maintainer: AurOS <noreply@auros.invalid>
Description: needs Ubuntu's icon artwork
Depends: humanity-icon-theme
EOL
    mkdir -p "$T/g/DEBIAN"; cp "$T/g.control" "$T/g/DEBIAN/control"
    dpkg-deb --root-owner-group -b "$T/g" "$T/g.deb" >/dev/null && dpkg -i "$T/g.deb" >/dev/null 2>&1
    ( unbrand_icons ) >"$T/guard.log" 2>&1; rc=$?
    [ $rc -eq 33 ] && ok "unbrand_icons refuses when a third package would go with the artwork" \
                   || bad "unbrand_icons refuses when a third package would go with the artwork" "exit $rc"
    check "...and humanity-icon-theme and ubuntu-mono are still there" sh -c 'dpkg-query -W -f="\${Status}\n" humanity-icon-theme ubuntu-mono | grep -c "ok installed" | grep -qx 2'
    dpkg -r auros-guardtest >/dev/null 2>&1

    echo
    echo "== the real run"
    ( eval "$STEP" ) >"$T/run.log" 2>&1; rc=$?
    [ $rc -eq 0 ] && ok "the identity step runs" || bad "the identity step runs" "exit $rc" "$(tail -4 "$T/run.log")"
    verify
    survives
    echo
    echo "== the update rollback still has its set (rootfs/usr/lib/auros/keep-previous, the real script)"
    # Two parts of the update channel at one version, installed beside the stand-in. keep-previous
    # keeps the set only if every installed auros / auros-* is at the version of `auros`.
    mkdir -p "$T/cache" "$T/debs"
    for pk in auros auros-config; do
        mkdir -p "$T/p/$pk/DEBIAN"
        printf 'Package: %s\nVersion: 5\nArchitecture: all\nMaintainer: AurOS <noreply@auros.invalid>\nDescription: a part\n' "$pk" > "$T/p/$pk/DEBIAN/control"
        dpkg-deb --root-owner-group -b "$T/p/$pk" "$T/cache/${pk}_5_amd64.deb" >/dev/null && dpkg -i "$T/cache/${pk}_5_amd64.deb" >/dev/null 2>&1
    done
    AUROS_DEBS="$T/debs" AUROS_APT_CACHE="$T/cache" sh "$ROOT/rootfs/usr/lib/auros/keep-previous"
    if [ -f "$T/debs/auros_5_amd64.deb" ] && [ -f "$T/debs/auros-config_5_amd64.deb" ]; then ok "an update keeps the set to roll back to, with the stand-in installed"
    else bad "an update keeps the set to roll back to, with the stand-in installed" "keep-previous kept: $(ls "$T/debs" | tr '\n' ' ')" \
            "(it keeps nothing when an installed auros-* package is at another version)"; fi

    echo
    echo "== a second run changes nothing"
    s1=$(md5sum /etc/lsb-release /usr/lib/os-release; dpkg-query -W -f='${Package}\n' | md5sum)
    ( eval "$STEP" ) >"$T/run2.log" 2>&1; rc=$?
    s2=$(md5sum /etc/lsb-release /usr/lib/os-release; dpkg-query -W -f='${Package}\n' | md5sum)
    [ $rc -eq 0 ] && [ "$s1" = "$s2" ] && ok "running the step again is a no-op" || bad "running the step again is a no-op" "exit $rc" "$(tail -3 "$T/run2.log")"
    ;;
image)
    R=$(cd "${2:?ROOTFS}" 2>/dev/null && pwd) || need "no rootfs at ${2:-}"
    mkdir -p "$T/up" "$T/work" "$T/m"
    if ! mount -t overlay overlay -o "lowerdir=$R,upperdir=$T/up,workdir=$T/work" "$T/m" 2>/dev/null; then
        echo "    (no overlay here: working on a copy of $R)"; cp -a "$R/." "$T/m/" || need "could not copy $R"
    fi
    mkdir -p "$T/m/dev" "$T/m/proc" "$T/m/tmp"
    mount --bind /dev "$T/m/dev" && mount -t proc proc "$T/m/proc" || need "could not mount /dev and /proc"
    mount -t devpts devpts "$T/m/dev/pts" 2>/dev/null
    rm -f "$T/m/etc/resolv.conf"; cat /etc/resolv.conf > "$T/m/etc/resolv.conf"
    inr() { chroot "$T/m" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin LC_ALL=C HOME=/root DEBIAN_FRONTEND=noninteractive "$@"; }
    inr apt-get update -q >"$T/update.log" 2>&1 || { tail -5 "$T/update.log"; need "apt-get update failed in the image"; }
    verify
    survives
    ;;
*)  need "usage: unbrandtest.sh container | image ROOTFS" ;;
esac

echo
if [ "$fail" -eq 0 ]; then echo "unbrandtest: $checked checks, all ok"; exit 0; fi
echo "unbrandtest: $fail of $checked checks FAILED"; exit 1
