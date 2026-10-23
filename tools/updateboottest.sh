#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  updateboottest — an AurOS update, on the real image, in a VM:
#  it arrives while she uses the desktop, nothing interrupts her, the
#  next start is updated and healthy, and a broken update undoes
#  itself. (docs/security/UPDATES.md)
#
#  The real image under QEMU/KVM with OVMF and Secure Boot. Its own
#  apt, its own unattended-upgrades, its own apt-daily and
#  apt-daily-upgrade units (with AurOS's drop-ins), its own shell-guard,
#  keep-previous and rollback. What the test changes in its scratch
#  copy: the archive key (a throwaway one; the real key's private half
#  is a secret) and the address (a signed repository on the disk
#  itself, as the VM has no network).
#
#    boot 1  the desktop is up; one day of updates (the real units)
#            brings AurOS V2 -> installed, the desktop neither restarted
#            nor stopped, the set before it kept
#    boot 2  the next start runs V2, nothing failed that had not failed
#            before; then the broken V3 (its aurshell exits at once)
#            arrives the same way, while the desktop runs
#    boot 3  V3's desktop fails, safe mode fails, shell-guard rolls back
#            to V2, the desktop comes up; the daily update does not bring
#            V3 back; the fixed V4 installs
#
#    sudo sh tools/updateboottest.sh [profile]   (out/auros-<profile>.img)
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

PROFILE="${1:-desktop}"
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
BOOT_TIMEOUT="${UPDATEBOOT_TIMEOUT:-2400}"

fail=0; checked=0
ok()  { checked=$((checked+1)); printf '    %-66s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-66s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }

[ "$(id -u)" = 0 ] || { echo "run as root (it mounts the image)"; exit 2; }
for t in qemu-system-x86_64 sgdisk losetup mount gpg dpkg-deb bash; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys"; exit 2; }
[ -f "out/auros-$PROFILE.img" ] || { echo "no out/auros-$PROFILE.img"; exit 2; }
ACCEL=tcg; [ -w /dev/kvm ] && ACCEL=kvm

LOCK="${TMPDIR:-/tmp}/installtest.lock"
mkdir "$LOCK" 2>/dev/null || { echo "another end-to-end test is running"; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/updateboot.XXXXXX")
QP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    rm -rf "$T" "$LOCK"
}
trap cleanup EXIT

echo
echo "An AurOS update while she uses it; the next start; a broken update undone."
echo

part_of() { sgdisk -p "$1" 2>/dev/null | awk -v n="$2" '$NF==n {print $1, $2, $3}'; }
mount_root() {
    set -- $(part_of "$DISK" AUROS-ROOT)
    [ -n "${2:-}" ] || { echo "  no AUROS-ROOT"; return 1; }
    LP=$(losetup --find --show -o $(( $2 * 512 )) \
                 --sizelimit $(( ($3 - $2 + 1) * 512 )) "$DISK") || return 1
    mkdir -p "$T/m"; mount "$LP" "$T/m" || { losetup -d "$LP"; LP=""; return 1; }
}
umount_root() { sync; umount "$T/m" 2>/dev/null; losetup -d "$LP" 2>/dev/null; LP=""; }

DISK="$T/auros.img"
cp --sparse=always "out/auros-$PROFILE.img" "$DISK"
mount_root || exit 2
R="$T/m"
E2E="$R/var/cache/auros-e2e"

[ -s "$R/etc/apt/sources.list.d/auros.sources" ] && [ -s "$R/var/lib/auros/debs/SHA256SUMS" ] ||
    { echo "  this image has no AurOS update channel (built before the archive key?)"; exit 2; }
[ -x "$R/usr/lib/auros/rollback" ] && [ -x "$R/usr/lib/auros/keep-previous" ] ||
    { echo "  this image has no rollback (built before it existed)"; exit 2; }
V1=$(chroot "$R" dpkg-query -W -f '${Version}' auros 2>/dev/null)
[ -n "$V1" ] || { echo "  no auros package in the image"; exit 2; }
# Later versions, as the channel would number them (they only rise).
V2="$V1.1"; V3="$V1.2"; V4="$V1.3"

# ── a throwaway archive key, put where the real one is ─────────────
export GNUPGHOME="$T/gnupg"; mkdir -m 700 "$GNUPGHOME"
gpg --batch --quiet --passphrase '' --quick-gen-key 'AurOS updates (updateboottest)' ed25519 sign 1d 2>/dev/null
gpg --armor --export > "$T/key.asc"
cp "$T/key.asc" "$R/usr/share/keyrings/auros-archive-keyring.asc"

# ── V2, V3 (broken), V4: the image's own packages, renumbered ──────
mk() { # VERSION broken|fine OUTDIR
    mkdir -p "$3"
    for d in "$R"/var/lib/auros/debs/*.deb; do
        rm -rf "$T/x"; dpkg-deb -R "$d" "$T/x" || return 1
        sed -i -e "s/^Version: .*/Version: $1/" -e "s/(= $V1)/(= $1)/g" -e "s/(<< $V1)/(<< $1)/g" "$T/x/DEBIAN/control"
        p=$(sed -n 's/^Package: //p' "$T/x/DEBIAN/control")
        case "$p" in
            auros-desktop)
                mkdir -p "$T/x/usr/share/auros"; echo "$1" > "$T/x/usr/share/auros/updateboottest-marker"
                [ "$2" = broken ] && printf '#!/bin/sh\n# updateboottest: a desktop that does not start\nexit 1\n' > "$T/x/usr/bin/aurshell" ;;
            auros-archive-keyring)
                cp "$T/key.asc" "$T/x/usr/share/keyrings/auros-archive-keyring.asc" ;;
        esac
        dpkg-deb --root-owner-group -Zxz --build "$T/x" "$3/${p}_${1}_amd64.deb" >/dev/null || return 1
    done
}
repo() { # VERSION broken|fine NAME
    mk "$1" "$2" "$T/debs-$3" &&
    bash build/aptchannel repo "$T/debs-$3" "$E2E/repo-$3" >/dev/null &&
    bash build/aptchannel sign "$E2E/repo-$3" 'AurOS updates (updateboottest)' >/dev/null &&
    bash build/aptchannel verify "$E2E/repo-$3" "$T/key.asc" >/dev/null
}
mkdir -p "$E2E/aside"
if repo "$V2" fine v2 && repo "$V3" broken v3 && repo "$V4" fine v4; then
    ok "AurOS $V2, a broken $V3 and a fixed $V4, signed (throwaway key)"
else
    bad "AurOS $V2, a broken $V3 and a fixed $V4, signed (throwaway key)"; exit 1
fi
# The VM has no network: AurOS's channel is on its own disk, and
# Ubuntu's and Mozilla's sources are put aside (they would only fail).
for f in "$R"/etc/apt/sources.list.d/*; do
    case "$f" in */auros.sources) ;; *) [ -e "$f" ] && mv "$f" "$E2E/aside/" ;; esac
done
[ -e "$R/etc/apt/sources.list" ] && mv "$R/etc/apt/sources.list" "$E2E/aside/"
sed -i 's#^URIs: .*#URIs: file:/var/cache/auros-e2e/repo/#' "$R/etc/apt/sources.list.d/auros.sources"
printf 'APT::Periodic::RandomSleep "0";\n' > "$R/etc/apt/apt.conf.d/99updateboottest"
printf 'confirmed by updateboottest\n' > "$R/var/lib/auros/converted.confirmed"

mkdir -p "$R/usr/lib/auros-e2e" "$R/etc/auros-e2e"
cat > "$R/usr/lib/auros-e2e/run.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. tools/updateboottest.sh puts this into a scratch
# copy of the image: one day of updates, by AurOS's own units.
OUT=/var/lib/auros-e2e; mkdir -p "$OUT"
mode=$(cat /etc/auros-e2e/mode); n=$(cat /etc/auros-e2e/boot)
log() { printf '%s %s\n' "$n" "$*" >> "$OUT/log"; sync; }
ver() { dpkg-query -W -f='${Version}' auros-desktop 2>/dev/null; }
prop() { systemctl show -p "$1" --value aurshell.service; }
# The desktop counts as up once aurshell has been running for 20 s.
up() {
    i=0
    while [ "$i" -lt "$1" ]; do
        if systemctl is-active -q aurshell.service; then
            t=$(prop ActiveEnterTimestampMonotonic)
            now=$(awk '{printf "%d", $1 * 1000000}' /proc/uptime)
            [ $((now - t)) -gt 20000000 ] && { log "up after ${i}s"; return 0; }
        fi
        sleep 2; i=$((i + 2))
    done
    log "up NEVER"; return 1
}
daily() { # the repository it would find today
    ln -sfn "/var/cache/auros-e2e/repo-$1" /var/cache/auros-e2e/repo
    # Not this computer's first hour, and nothing done yet today.
    touch -d '2 days ago' /var/lib/auros/firstboot.done 2>/dev/null
    rm -f /var/lib/apt/periodic/*-stamp
    systemctl start apt-daily.service
    systemctl start apt-daily-upgrade.service
    log "day($1) apt-daily=$(systemctl show -p Result --value apt-daily.service) apt-daily-upgrade=$(systemctl show -p Result --value apt-daily-upgrade.service) auros-desktop=$(ver)"
    cat /var/log/unattended-upgrades/unattended-upgrades.log >> "$OUT/uu.log" 2>/dev/null
}
failed() { systemctl --failed --no-legend --plain 2>/dev/null | awk '{print $1}' | sort | tr '\n' ' '; }
log "start mode=$mode auros-desktop=$(ver)"
case "$mode" in
    update)
        up 900
        log "failed-before $(failed)"
        p1=$(prop MainPID); r1=$(prop NRestarts)
        daily v2
        log "desktop pid $p1 -> $(prop MainPID) restarts $r1 -> $(prop NRestarts) $(systemctl is-active aurshell.service)"
        log "kept $(ls /var/lib/auros/debs/ | grep '^auros_') previous $(ls /var/lib/auros/debs-previous/ 2>/dev/null | grep '^auros_')"
        ;;
    check)
        up 900
        log "marker $(cat /usr/share/auros/updateboottest-marker 2>/dev/null)"
        log "failed-after $(failed)"
        p1=$(prop MainPID)
        daily v3
        log "broken-arrived pid $p1 -> $(prop MainPID) $(systemctl is-active aurshell.service)"
        ;;
    broken)
        up 1800
        log "rolled $(ver) level=$(cat /run/auros-guard/level 2>/dev/null)"
        cp /var/lib/auros/rollback.log "$OUT/" 2>/dev/null
        ls /etc/apt/preferences.d/ > "$OUT/prefs" 2>&1
        daily v3
        daily v4
        ;;
esac
sync
systemctl poweroff
EOS
chmod 755 "$R/usr/lib/auros-e2e/run.sh"
cat > "$R/etc/systemd/system/auros-e2e.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: updateboottest's day of updates
After=auros-hold.service aurshell.service
[Service]
Type=oneshot
ExecStart=/usr/lib/auros-e2e/run.sh
TimeoutStartSec=3000
[Install]
WantedBy=multi-user.target
EOU
ln -sf /etc/systemd/system/auros-e2e.service "$R/etc/systemd/system/multi-user.target.wants/auros-e2e.service"
umount_root

NV="$T/vars.fd"; cp "$VARS" "$NV"
BOOTN=0
power_on() { # mode
    BOOTN=$((BOOTN + 1))
    mount_root || return 1
    printf '%s\n' "$1" > "$T/m/etc/auros-e2e/mode"
    printf '%s\n' "$BOOTN" > "$T/m/etc/auros-e2e/boot"
    umount_root
    qemu-system-x86_64 -machine q35,smm=on,accel=$ACCEL -m 3072 -smp 2 -no-reboot \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$NV" \
        -drive file="$DISK",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -device virtio-vga -display none -serial file:"$T/serial.$BOOTN" -net none \
        >/dev/null 2>&1 &
    QP=$!
    i=0
    while kill -0 "$QP" 2>/dev/null && [ "$i" -lt "$BOOT_TIMEOUT" ]; do sleep 2; i=$((i + 2)); done
    kill -0 "$QP" 2>/dev/null && echo "  boot $BOOTN did not power off within ${BOOT_TIMEOUT}s"
    kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
}
L() { mount_root && cat "$T/m/var/lib/auros-e2e/log" 2>/dev/null; umount_root; }
line() { printf '%s\n' "$LOG" | grep "^$1 $2" | tail -1; }

# ── boot 1 ─────────────────────────────────────────────────────────
echo "  boot 1: her desktop, and a day of updates"
power_on update; LOG=$(L)
case "$(line 1 'up ')" in *"up after"*) ok "AurOS $V1: the desktop is up" ;;
    *) bad "AurOS $V1: the desktop is up" "$(printf '%s\n' "$LOG" | tail -5)"; exit 1 ;; esac
case "$(line 1 'day(v2)')" in *"apt-daily-upgrade=success auros-desktop=$V2") ok "the day's update installed AurOS $V2 by itself" ;;
    *) bad "the day's update installed AurOS $V2 by itself" "$(line 1 'day(v2)')"
       mount_root; tail -20 "$T/m/var/lib/auros-e2e/uu.log" 2>/dev/null | sed 's/^/      | /'; umount_root ;; esac
set -- $(line 1 'desktop pid')
# "1 desktop pid P1 -> P2 restarts R1 -> R2 active"
if [ "${4:-x}" = "${6:-y}" ] && [ "${8:-x}" = "${10:-y}" ] && [ "${11:-}" = active ]; then
    ok "the desktop was not restarted or stopped (pid $4, restarts $8)"
else bad "the desktop was not restarted or stopped" "$(line 1 'desktop pid')"; fi
case "$(line 1 kept)" in *"kept auros_${V2}_amd64.deb previous auros_${V1}_amd64.deb") ok "the set before the update was kept" ;;
    *) bad "the set before the update was kept" "$(line 1 kept)" ;; esac

# ── boot 2 ─────────────────────────────────────────────────────────
echo "  boot 2: the next start"
power_on check; LOG=$(L)
case "$(line 2 'up ')" in *"up after"*) ok "the next start: the desktop is up" ;;
    *) bad "the next start: the desktop is up" "$(printf '%s\n' "$LOG" | tail -5)" ;; esac
[ "$(line 2 marker)" = "2 marker $V2" ] && ok "  ... running AurOS $V2" || bad "  ... running AurOS $V2" "$(line 2 marker)"
before=$(line 1 failed-before | sed 's/^1 failed-before //'); after=$(line 2 failed-after | sed 's/^2 failed-after //')
new=""; for u in $after; do case " $before " in *" $u "*) ;; *) new="$new $u" ;; esac; done
[ -z "$new" ] && ok "  ... and nothing failed that had not failed before" || bad "  ... and nothing failed that had not failed before" "now failed:$new"
case "$(line 2 'day(v3)')" in *"auros-desktop=$V3") ok "the broken $V3 arrived the same way" ;;
    *) bad "the broken $V3 arrived the same way" "$(line 2 'day(v3)')" ;; esac
case "$(line 2 broken-arrived)" in *active) ok "  ... and the running desktop went on running" ;;
    *) bad "  ... and the running desktop went on running" "$(line 2 broken-arrived)" ;; esac

# ── boot 3 ─────────────────────────────────────────────────────────
echo "  boot 3: AurOS $V3 does not start"
power_on broken; LOG=$(L)
mount_root
RB=$(cat "$T/m/var/lib/auros-e2e/rollback.log" 2>/dev/null); PR=$(cat "$T/m/var/lib/auros-e2e/prefs" 2>/dev/null)
umount_root
case "$(line 3 'up ')" in *"up after"*) ok "the desktop came back by itself ($(line 3 'up ' | sed 's/^3 //'))" ;;
    *) bad "the desktop came back by itself" "$(printf '%s\n' "$LOG" | tail -5)" "$RB" ;; esac
case "$(line 3 rolled)" in "3 rolled $V2 "*) ok "  ... on AurOS $V2: the broken update was undone" ;;
    *) bad "  ... on AurOS $V2: the broken update was undone" "$(line 3 rolled)" "$RB" ;; esac
case "$RB" in *"went back to $V2"*) ok "  ... and rollback.log says so" ;; *) bad "  ... and rollback.log says so" "$RB" ;; esac
case "$PR" in *00auros-broken-*) ok "  ... and the broken version is pinned out" ;; *) bad "  ... and the broken version is pinned out" "$PR" ;; esac
case "$(line 3 'day(v3)')" in *"auros-desktop=$V2") ok "the next day's update does not bring $V3 back" ;;
    *) bad "the next day's update does not bring $V3 back" "$(line 3 'day(v3)')" ;; esac
case "$(line 3 'day(v4)')" in *"auros-desktop=$V4") ok "the fixed $V4 installs by itself" ;;
    *) bad "the fixed $V4 installs by itself" "$(line 3 'day(v4)')" ;; esac

echo
echo "  $(printf '%s\n' "$LOG" | wc -l) log lines; the log:"
printf '%s\n' "$LOG" | sed 's/^/    | /'
echo
if [ "$fail" -eq 0 ]; then echo "updateboottest: $checked checks, all passed"; exit 0; fi
echo "updateboottest: $fail of $checked checks FAILED"; exit 1
