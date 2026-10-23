#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  timeboottest — is the clock right on a PC where Windows keeps it in
#  local time?
#
#  Windows keeps the PC's hardware clock (RTC) in LOCAL time; Linux reads
#  it as UTC unless told otherwise. On every machine west of Greenwich
#  AurOS therefore started hours BEHIND, and with no time daemon it
#  stayed there: apt refused every Release file ("not valid yet"),
#  unattended-upgrades never installed a fix, certificates failed.
#
#  This boots the real image, three ways, in a VM whose RTC is set to
#  LOCAL time in an American zone (QEMU -rtc base=localtime with TZ set;
#  default America/Los_Angeles), ONLINE, the way a first start is:
#
#    before    the fix taken out (chrony masked, no clock step by the
#              first-boot setup): the clock is hours out, apt refuses
#    ntponly   chrony alone: the clock is out when the network comes
#              up and must be stepped to the right time within a minute
#    after     the image as it is: the first-boot setup (choices.sh,
#              from the installer's answers on the ESP) reads the PC's
#              clock as local time, chrony does the rest; then a
#              RESTART, which must come up with the right time before
#              any network (systemd applies /etc/adjtime LOCAL)
#
#  and on each: the clock against the HOST's, the same on the serial
#  line (the harness prints the guest's time, the host stamps its own on
#  arrival), `apt-get update`, and the RTC after the sync. The RTC must
#  still be in LOCAL time then: this machine must never write UTC into
#  Windows' clock (that is what rtcsync / the kernel's 11-minute mode
#  would do), or Windows shows the wrong time when she goes back to it.
#
#  NOT PART OF AUROS: auros-time-early.service, auros-time-harness.service
#  and /usr/lib/auros-time put into a scratch copy of the image; and, to
#  stand in for AurBridge, EFI/AurOS/choices.conf on that copy's ESP.
#
#    sudo sh tools/timeboottest.sh [image]     (default out/auros-desktop.img)
#    TIME_ZONE=America/New_York  TIME_REPORT=out/timeboot.md
#
#  An image built before the clock files existed (no AurOS chrony.conf in
#  it) gets this tree's files laid over it and chrony installed, and the
#  report says so.
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

IMG="${1:-out/auros-desktop.img}"
ZONE="${TIME_ZONE:-America/Los_Angeles}"
MEM="${TIME_MEM:-4096}"
REPORT="${TIME_REPORT:-out/timeboot.md}"
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd

for t in qemu-system-x86_64 sgdisk losetup mount python3; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys ($CODE)"; exit 2; }
[ -f "$IMG" ] || { echo "no image at $IMG"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run as root (loop devices)"; exit 2; }
[ -f "/usr/share/zoneinfo/$ZONE" ] || { echo "no zone $ZONE on this host"; exit 2; }
if [ -w /dev/kvm ]; then ACCEL="kvm -cpu host"
else ACCEL="tcg -cpu max"; echo "  (no KVM: the checks hold, the waits are long)"; fi
# Seconds the zone is EAST of UTC at this moment (America/Los_Angeles: -25200).
OFF=$(TZ="$ZONE" date +%z | awk '{ s = ($1 ~ /^-/) ? -1 : 1; h = substr($1, 2, 2); m = substr($1, 4, 2); print s * (h * 3600 + m * 60) }')
[ "$OFF" -lt 0 ] || { echo "this test wants a zone west of Greenwich; $ZONE is $OFF s"; exit 2; }

T=$(mktemp -d "${TMPDIR:-/tmp}/timeboot.XXXXXX") || exit 2
QP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    [ "${TIME_KEEP:-0}" = 1 ] && { rm -f "$T/disk.img"; echo "kept $T (without the disk)"; } || rm -rf "$T"
}
trap cleanup EXIT
trap 'exit 130' INT TERM
say() { printf '%s\n' "$*" | tee -a "$T/report"; }

part() { sgdisk -p "$1" 2>/dev/null | awk -v n="$2" '$NF == n { print $2, $3 }'; }
mount_part() { # disk name
    set -- "$1" $(part "$1" "$2")
    [ -n "${3:-}" ] || { echo "  no such partition in $1"; return 1; }
    LP=$(losetup --find --show -o $(( $2 * 512 )) --sizelimit $(( ($3 - $2 + 1) * 512 )) "$1") || return 1
    mkdir -p "$T/m"; mount "$LP" "$T/m" || { losetup -d "$LP"; LP=""; return 1; }
}
umount_part() { sync; umount "$T/m"; losetup -d "$LP"; LP=""; }

# ── what the harness is made of ─────────────────────────────────────
inject() {
    M="$T/m"
    mkdir -p "$M/usr/lib/auros-time" "$M/var/lib/auros-time"
    : > "$M/var/lib/auros-time/boot"; echo 0 > "$M/var/lib/auros-time/boot"
    cat > "$M/usr/lib/auros-time/early.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. The clock as it is when the system comes up, before
# the first-boot setup and before the network: what the PC's clock said,
# and how this system reads it.
n=$(( $(cat /var/lib/auros-time/boot 2>/dev/null || echo 0) + 1 ))
printf 'AUROS-TIME b%s-early %s rtc=%s adj=%s tz=%s\n' "$n" "$(date +%s)" \
    "$(cat /sys/class/rtc/rtc0/since_epoch 2>/dev/null)" "$(sed -n 3p /etc/adjtime 2>/dev/null || echo none)" \
    "$(readlink /etc/localtime 2>/dev/null)" > /dev/ttyS0
EOS
    cat > "$M/usr/lib/auros-time/run.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. Waits for the network, then for the time to be
# right; records the clock, the RTC after the sync, what apt says, and
# whether the time daemon listens; then restarts (first boot) or turns
# the machine off.
D=/var/lib/auros-time
n=$(cat $D/boot); n=$((n + 1)); echo $n > $D/boot
mode=$(cat /etc/auros-time-mode)
R="$D/result.$n"; : > "$R"
put() { printf '%s=%s\n' "$1" "$2" >> "$R"; }
tm() { printf 'AUROS-TIME b%s-%s %s rtc=%s\n' "$n" "$1" "$(date +%s)" "$(cat /sys/class/rtc/rtc0/since_epoch)" > /dev/ttyS0; }
tm up
put chrony_unit "$(systemctl is-enabled chrony.service 2>&1 | head -1)"
put chrony_active "$(systemctl is-active chrony.service 2>&1 | head -1)"
# the network
i=0; while [ -z "$(ip -4 route show default)" ] && [ $i -lt 120 ]; do sleep 1; i=$((i+1)); done
tm net
# the time: chrony says "Normal" once it has a source it trusts (the
# kernel's own flag is not clear without rtcsync, by design)
i=0; synced=no
while [ $i -lt 90 ]; do
    if chronyc -n tracking 2>/dev/null | grep -q '^Leap status *: Normal'; then synced=yes; break; fi
    sleep 1; i=$((i+1))
done
put synced "$synced"; put sync_wait_s "$i"
tm synced
chronyc -n tracking > $D/tracking.$n 2>&1; chronyc -n sources > $D/sources.$n 2>&1
sleep 20            # long enough for the kernel to write the RTC if it were going to
tm rtc-later
put listeners "$(ss -H -lunp 2>/dev/null | grep -c chronyd)"
# apt must accept the Release files
timeout 170 apt-get update -o Acquire::Retries=1 -o Acquire::http::Timeout=20 > $D/apt.$n 2>&1
put apt_rc "$?"
put apt_notyet "$(grep -ci 'not valid yet' $D/apt.$n)"
journalctl -b --no-pager -o short-monotonic -u chrony -u auros-firstboot > $D/journal.$n 2>&1
tm done
sync
if [ "$mode" = reboot ] && [ $n -lt 2 ]; then systemctl reboot; else systemctl poweroff; fi
EOS
    chmod 755 "$M/usr/lib/auros-time/early.sh" "$M/usr/lib/auros-time/run.sh"
    cat > "$M/etc/systemd/system/auros-time-early.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: tools/timeboottest.sh, the clock as it comes up
DefaultDependencies=no
After=local-fs.target
Before=auros-firstboot.service sysinit.target
[Service]
Type=oneshot
ExecStart=/usr/lib/auros-time/early.sh
[Install]
WantedBy=sysinit.target
EOU
    cat > "$M/etc/systemd/system/auros-time-harness.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: tools/timeboottest.sh's harness
After=multi-user.target
[Service]
Type=simple
ExecStart=/usr/lib/auros-time/run.sh
[Install]
WantedBy=multi-user.target
EOU
    ln -sf /etc/systemd/system/auros-time-early.service "$M/etc/systemd/system/sysinit.target.wants/auros-time-early.service"
    ln -sf /etc/systemd/system/auros-time-harness.service "$M/etc/systemd/system/multi-user.target.wants/auros-time-harness.service"
}

# An image built before the clock files existed: this tree's files over
# it, and chrony installed in it the way build/forge would.
overlay() {
    M="$T/m"
    [ -f "$M/usr/lib/systemd/system/chrony.service" ] || [ -f "$M/lib/systemd/system/chrony.service" ] || {
        for d in proc sys dev; do mount --bind "/$d" "$M/$d"; done
        mv "$M/etc/resolv.conf" "$M/etc/resolv.conf.t" 2>/dev/null
        printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$M/etc/resolv.conf"
        printf '#!/bin/sh\nexit 101\n' > "$M/usr/sbin/policy-rc.d"; chmod 755 "$M/usr/sbin/policy-rc.d"
        timeout 600 chroot "$M" sh -c 'apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends chrony' > "$T/overlay.log" 2>&1 \
            || { echo "could not install chrony in the copy"; tail -5 "$T/overlay.log"; }
        chroot "$M" systemctl enable chrony.service > /dev/null 2>&1
        rm -f "$M/usr/sbin/policy-rc.d" "$M/etc/resolv.conf"; mv "$M/etc/resolv.conf.t" "$M/etc/resolv.conf" 2>/dev/null
        rm -rf "$M"/var/lib/apt/lists/*
        for d in dev sys proc; do umount "$M/$d" 2>/dev/null; done
    }
    cp -R rootfs/etc/chrony/. "$M/etc/chrony/"
    install -m 755 rootfs/etc/NetworkManager/dispatcher.d/30-auros-time "$M/etc/NetworkManager/dispatcher.d/30-auros-time"
    install -m 755 rootfs/usr/lib/auros/choices.sh "$M/usr/lib/auros/choices.sh"
}

# The fix, taken out in pieces.
strip_fix() { # before|ntponly
    M="$T/m"
    # no step of the clock by the first-boot setup (and so no adjtime)
    sed -i '/^[[:space:]]*clock_local "\$tz"$/d' "$M/usr/lib/auros/choices.sh"
    # for "before", no time daemon either
    [ "$1" = before ] && ln -sf /dev/null "$M/etc/systemd/system/chrony.service"
}

make_disk() { # scenario
    rm -f "$T/disk.img"
    cp --sparse=always "$IMG" "$T/disk.img" || exit 2
    mount_part "$T/disk.img" AUROS-ROOT || exit 2
    inject
    # An image built before the clock files existed gets this tree's.
    if ! grep -q AurOS "$T/m/etc/chrony/chrony.conf" 2>/dev/null; then overlay; echo yes > "$T/overlaid"; fi
    case "$1" in before) strip_fix before ;; ntponly) strip_fix ntponly ;; esac
    if [ "$1" = after ]; then echo reboot > "$T/m/etc/auros-time-mode"; else echo off > "$T/m/etc/auros-time-mode"; fi
    cp "$T/m/usr/lib/auros/choices.sh" "$T/choices.sh.$1" 2>/dev/null
    umount_part
    # AurBridge's answers, where first boot looks for them
    mount_part "$T/disk.img" AUROS-ESP || exit 2
    mkdir -p "$T/m/EFI/AurOS"
    printf 'language=en_US.UTF-8\ntimezone=iana:%s\n' "$ZONE" > "$T/m/EFI/AurOS/choices.conf"
    umount_part
}

# One VM, however many guest starts it takes (a guest restart keeps the
# RTC and the process).
run_vm() { # scenario
    s=$1; SER="$T/serial.$s"; : > "$SER"; : > "$T/ev.$s"
    cp "$VARS" "$T/vars.$s"
    # shellcheck disable=SC2086
    TZ="$ZONE" qemu-system-x86_64 -machine q35,smm=on -accel $ACCEL -m "$MEM" -smp 2 \
        -rtc base=localtime,clock=host \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$T/vars.$s" \
        -drive file="$T/disk.img",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -device virtio-vga -display none -serial file:"$SER" \
        -netdev user,id=n0 -device virtio-net-pci,netdev=n0 > "$T/qemu.$s" 2>&1 &
    QP=$!; i=0; seen=0; done_marks=1; [ "$s" = after ] && done_marks=2
    while kill -0 "$QP" 2>/dev/null && [ $i -lt "${TIME_TIMEOUT:-1500}" ]; do
        # Stamp each AUROS-TIME line with the host's own clock as it arrives.
        n=$(tr -d '\r' < "$SER" | grep -a -c '^AUROS-TIME ')
        if [ "$n" -gt "$seen" ]; then
            h=$(date +%s)
            tr -d '\r' < "$SER" | grep -a '^AUROS-TIME ' | sed -n "$((seen + 1)),${n}p" | sed "s/^/$h /" >> "$T/ev.$s"
            seen=$n
        fi
        [ "$(grep -c -- '-done ' "$T/ev.$s")" -ge "$done_marks" ] && { sleep 8; break; }
        sleep 1; i=$((i + 1))
    done
    kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
    # what the guest wrote down
    mount_part "$T/disk.img" AUROS-ROOT || return 1
    mkdir -p "$T/rec.$s"; cp -a "$T/m/var/lib/auros-time/." "$T/rec.$s/" 2>/dev/null
    umount_part
    rm -f "$T/disk.img"
}

# ── read what happened ──────────────────────────────────────────────
# ev lines: HOSTEPOCH AUROS-TIME TAG GUESTEPOCH rtc=RAW [...]
guest() { awk -v t="$2" '$3 == t { print $4; exit }' "$T/ev.$1"; }
host()  { awk -v t="$2" '$3 == t { print $1; exit }' "$T/ev.$1"; }
rtc()   { awk -v t="$2" '$3 == t { for (i = 4; i <= NF; i++) if ($i ~ /^rtc=/) { sub(/rtc=/, "", $i); print $i; exit } }' "$T/ev.$1"; }
err()   { g=$(guest "$1" "$2"); h=$(host "$1" "$2"); [ -n "$g" ] && [ -n "$h" ] && echo $((g - h)); }
rtcerr() { r=$(rtc "$1" "$2"); h=$(host "$1" "$2"); [ -n "$r" ] && [ -n "$h" ] && echo $((r - h)); }   # a local-time RTC reads OFF east of UTC
res()   { sed -n "s/^$3=//p" "$T/rec.$1/result.$2" 2>/dev/null | tail -n 1; }
abs()   { echo "${1#-}"; }
within() { [ -n "$1" ] && [ "$(abs $(( $1 - $2 )))" -le "$3" ]; }   # value target tolerance

for s in before ntponly after; do
    echo "  $s: starting the machine (RTC in $ZONE local time, online)"
    make_disk $s
    run_vm $s
done

fail=0; checked=0
say "## The clock, booted (RTC set to $ZONE local time = ${OFF}s east of UTC; online; $(basename "$IMG"))"
say ""
say "| check | result | what the machine said |"
say "|---|---|---|"
row() { # ok|bad label detail
    checked=$((checked + 1))
    if [ "$1" = ok ]; then say "| $2 | ok | $3 |"; else fail=$((fail + 1)); say "| $2 | FAIL | $3 |"; fi
}
chk() { if "$@"; then echo ok; else echo bad; fi; }
S=${TIME_SLOP:-120}

e=$(err before b1-early)
row "$(chk within "$e" "$OFF" 300)" "the premise: this system reads the PC's clock as UTC, so it starts ${OFF#-}s behind" "off by ${e:-?} s"
e=$(err before b1-up)
row "$(chk within "$e" "$OFF" 300)" "without the fix the first start stays hours out" "off by ${e:-?} s"
a=$(res before 1 apt_notyet)
row "$([ "${a:-0}" -ge 1 ] && echo ok || echo bad)" "...and apt refuses every Release file (\"not valid yet\")" "lines saying so: ${a:-?}, apt exit $(res before 1 apt_rc)"

e=$(err ntponly b1-synced)
w=$(res ntponly 1 sync_wait_s)
row "$([ "$(res ntponly 1 synced)" = yes ] && within "$e" 0 5 && echo ok || echo bad)" "chrony alone: the clock is stepped to the right time (within 5 s)" "synced=$(res ntponly 1 synced), off by ${e:-?} s"
row "$([ -n "$w" ] && [ "$w" -le 60 ] && echo ok || echo bad)" "...within a minute of the network coming up" "${w:-?} s after the network"
a=$(res ntponly 1 apt_notyet)
row "$([ "${a:-9}" = 0 ] && [ "$(res ntponly 1 apt_rc)" = 0 ] && echo ok || echo bad)" "...and apt accepts the Release files" "'not valid yet' lines: ${a:-?}, apt exit $(res ntponly 1 apt_rc)"
r=$(rtcerr ntponly b1-rtc-later)
row "$(chk within "$r" "$OFF" "$S")" "...and the PC's own clock is left in local time (never written in UTC)" "reads ${r:-?} s from true UTC; local time is $OFF"

e=$(err after b1-up)
row "$(chk within "$e" 0 "$S")" "after: the first start has the right time before any network (first-boot setup)" "off by ${e:-?} s"
e=$(err after b1-synced)
row "$([ "$(res after 1 synced)" = yes ] && within "$e" 0 5 && echo ok || echo bad)" "...and chrony agrees with the host (within 5 s)" "synced=$(res after 1 synced), off by ${e:-?} s"
a=$(res after 1 apt_notyet)
row "$([ "${a:-9}" = 0 ] && [ "$(res after 1 apt_rc)" = 0 ] && echo ok || echo bad)" "...and apt-get update accepts the Release files" "'not valid yet' lines: ${a:-?}, apt exit $(res after 1 apt_rc)"
r=$(rtcerr after b1-rtc-later)
row "$(chk within "$r" "$OFF" "$S")" "...and Windows' clock is still local time after the sync" "reads ${r:-?} s from true UTC; local time is $OFF"
e=$(err after b2-early)
row "$(chk within "$e" 0 "$S")" "after a RESTART the time is right before any network (adjtime LOCAL)" "off by ${e:-?} s; adjtime line 3 on boot 2: $(grep ' b2-early ' "$T/ev.after" | grep -o 'adj=[A-Za-z]*')"
r=$(rtcerr after b2-rtc-later)
row "$(chk within "$r" "$OFF" "$S")" "...and still local time in the PC's clock after the second sync" "reads ${r:-?} s from true UTC; local time is $OFF"
l="$(res after 1 listeners)$(res after 2 listeners)"
row "$([ "$l" = 00 ] && echo ok || echo bad)" "chrony listens on no port" "sockets: boot 1 $(res after 1 listeners), boot 2 $(res after 2 listeners)"

say ""
[ -f "$T/overlaid" ] && say "(This image predates the clock files: this tree's chrony.conf, NTS sources, dispatcher script and choices.sh were laid over it, and chrony installed in it.)"
say "Details: chrony enabled? before: $(res before 1 chrony_unit), after: $(res after 1 chrony_unit)/$(res after 1 chrony_active)."
say "Sources on the 'after' machine:"
{ echo '```'; sed -n 1,14p "$T/rec.after/sources.1" 2>/dev/null; echo '```'; } | tee -a "$T/report"
say ""
if [ "$fail" -eq 0 ]; then say "timeboottest: $checked checks, all ok"
else say "timeboottest: $fail of $checked checks FAILED"; fi
mkdir -p "$(dirname "$REPORT")"; cp "$T/report" "$REPORT"
tar -C "$T" -czf "${REPORT%.*}-evidence.tar.gz" rec.before rec.ntponly rec.after ev.before ev.ntponly ev.after \
    serial.before serial.ntponly serial.after 2>/dev/null
[ "$fail" -eq 0 ]
