#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  hardenboottest — the hardening, on the real image, booted
#
#  tools/hardentest.sh reads the controls out of a rootfs. This boots
#  the published image -- out/auros-desktop.img, byte for byte what
#  AurBridge writes -- under OVMF with Microsoft's keys and Secure Boot
#  on, in a 4 GB, two-core VM (the 2013 laptop), and asks the running
#  machine what is actually in force:
#
#    network   nothing listens; a stranger's TCP connection and UDP 631
#              packet (the cups-browsed CVE-2024-47176 door) reach a
#              listener we put there on purpose -- and must get nothing;
#              her own traffic (DHCP, a page from outside) still works;
#              avahi does not announce the computer
#    kernel    /proc/cmdline, every sysctl in hardentest's list, an
#              ordinary program's socket() cannot pull in dccp, and
#              Flatpak's sandbox (bubblewrap) still starts
#    memory    `tail /dev/zero` as her: zram takes the first GBs,
#              earlyoom stops it, the desktop is the same process after
#    desktop   aurshell killed with SIGSEGV three times: the fourth
#              start is safe mode (the plain home, the machine's settings)
#    updates   a package left half-configured (a power cut mid-update)
#              is finished at the next start
#    logs      the journal's cap is the one AurOS sets
#    boot menu the keys a person at the keyboard would press at GRUB:
#              `e` (edit a start line, add init=/bin/bash), `c` (GRUB's
#              command line), and the recovery-shell entry -- each must
#              ask for a password (stream BT's lock)
#    scams     (stream S2's controls, asked for by the lead) AnyDesk from
#              the store and a TeamViewer .deb are refused by the booted
#              polkitd, the desktop's card is asked for, her "yes" on it
#              (with the keyboard) lets the next try through; with
#              Firefox full screen on a page that eats every key, one
#              Escape does nothing, three Escapes and Ctrl+Alt+Delete
#              bring up the card and its "Close this page" closes it
#
#  Keys and screenshots go through QEMU's monitor (sendkey, screendump)
#  when the harness asks for them on the serial line.
#
#  THEN THE SAME IMAGE WITH EVERY CONTROL TAKEN OUT (sysctl.d files,
#  modprobe.d, cmdline options, nftables/zram/earlyoom/dpkg-repair
#  enable links, the safe-mode drop-in, the journald cap, avahi
#  publishing back on, GRUB's superusers/password lines, S2's polkit
#  rule) is booted the same way, and every check tied to
#  a control must FAIL there. A check that passes without its control
#  proves nothing. (One at a time, statically: hardentest-mutants.)
#
#  And the cost: both disks are started HARDEN_RUNS more times, in
#  turn, and boot time, time to desktop and idle RAM (MemTotal minus
#  MemAvailable, 60 s after the boot finished) are the medians.
#
#  WHAT IS NOT REAL. One unit in each booted system is not AurOS:
#  auros-harden.service, the harness that runs the checks, writes what
#  it saw into /var/lib/auros-harden and powers off. The network is
#  QEMU's (restrict=on: no internet, so nothing else downloads during
#  the measurement); the probes come from the host through it. The
#  "before" image is the S1 image with its controls switched off, not
#  an older build: cups-browsed is not in it either (it was never
#  installed), so the RAM it would have cost is not in "before".
#
#    sudo sh tools/hardenboottest.sh [image]   (default out/auros-desktop.img)
#    HARDEN_RUNS=3  HARDEN_MEM=4096  HARDEN_REPORT=out/hardenboot.md
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."

IMG="${1:-out/auros-desktop.img}"
CODE=/usr/share/OVMF/OVMF_CODE_4M.ms.fd
VARS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
MEM="${HARDEN_MEM:-4096}"
RUNS="${HARDEN_RUNS:-3}"
REPORT="${HARDEN_REPORT:-out/hardenboot.md}"
FULL_TIMEOUT="${HARDEN_TIMEOUT:-1500}"
TP=17777 UP=10631 HP=18080   # host ports: inbound TCP, inbound UDP, the page "from outside"
KOPTS="init_on_alloc=1 slab_nomerge randomize_kstack_offset=on vsyscall=none debugfs=off panic=10 fsck.repair=yes lockdown=integrity"

for t in qemu-system-x86_64 sgdisk losetup mount python3; do
    command -v "$t" >/dev/null 2>&1 || { echo "need $t"; exit 2; }
done
[ -f "$CODE" ] && [ -f "$VARS" ] || { echo "need OVMF with Microsoft's keys ($CODE)"; exit 2; }
[ -f "$IMG" ] || { echo "no image at $IMG"; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run as root (loop devices)"; exit 2; }
if [ -w /dev/kvm ]; then ACCEL="kvm -cpu host"
else ACCEL="tcg -cpu max"; echo "  (no KVM: the checks hold, the timings mean nothing)"; fi

T=$(mktemp -d "${TMPDIR:-/tmp}/hardenboot.XXXXXX") || exit 2
QP=""; WP=""; LP=""
cleanup() {
    [ -n "$QP" ] && kill -9 "$QP" 2>/dev/null
    [ -n "$WP" ] && kill "$WP" 2>/dev/null
    mountpoint -q "$T/m" 2>/dev/null && umount "$T/m"
    [ -n "$LP" ] && losetup -d "$LP" 2>/dev/null
    [ "${HARDEN_KEEP:-0}" = 1 ] && { rm -f "$T/disk.img"; echo "kept $T (without the disk)"; } || rm -rf "$T"
}
# EXIT, INT and TERM: a killed run (a timeout, a cancelled job) must not
# leave a multi-GB disk copy behind on a shared machine.
trap cleanup EXIT
trap 'exit 130' INT TERM

fail=0; checked=0
say() { printf '%s\n' "$*" | tee -a "$T/report"; }

# ── the root partition of a disk, by name ───────────────────────────
mount_root() { # disk
    set -- "$1" $(sgdisk -p "$1" 2>/dev/null | awk '$NF=="AUROS-ROOT" {print $2, $3}')
    [ -n "${3:-}" ] || { echo "  $1 has no AUROS-ROOT"; return 1; }
    LP=$(losetup --find --show -o $(( $2 * 512 )) --sizelimit $(( ($3 - $2 + 1) * 512 )) "$1") || return 1
    mkdir -p "$T/m"; mount "$LP" "$T/m" || { losetup -d "$LP"; LP=""; return 1; }
}
umount_root() { sync; umount "$T/m"; losetup -d "$LP"; LP=""; }

# ── the harness: NOT PART OF AUROS ──────────────────────────────────
inject() {
    M="$T/m"
    mkdir -p "$M/usr/lib/auros-harden" "$M/etc/auros-harden"
    awk '{print $1}' "$T/keys" > "$M/etc/auros-harden/keys"
    cat > "$M/usr/lib/auros-harden/listen.py" <<'EOP'
# NOT PART OF AUROS: a listener put there on purpose, so that "nothing
# reaches it" is about the firewall and not about an empty port.
import select, socket, sys, time
out = open(sys.argv[1], 'a', buffering=1)
t = socket.socket(); t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
t.bind(('0.0.0.0', 7777)); t.listen(5)
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.bind(('0.0.0.0', 631))
out.write('listening\n')
end = time.time() + float(sys.argv[2])
while time.time() < end:
    for s in select.select([t, u], [], [], 1)[0]:
        if s is t:
            c, a = t.accept(); out.write('tcp from %s\n' % a[0])
            c.sendall(b'AUROS-PROBE-BANNER\n'); c.close()
        else:
            d, a = u.recvfrom(512); out.write('udp from %s\n' % a[0])
EOP
    cat > "$M/usr/lib/auros-harden/trap.html" <<'EOH'
<!doctype html><meta charset="utf-8"><title>WARNING</title>
<!-- NOT PART OF AUROS: a stand-in for a scam page that will not let go -->
<body style="margin:0;background:#c00;color:#fff;font:bold 48px sans-serif">
<p style="padding:2em">YOUR COMPUTER IS INFECTED. DO NOT TURN IT OFF. CALL 0800 000 000.</p>
<script>
addEventListener('keydown', function (e) { e.preventDefault(); e.stopPropagation(); }, true);
addEventListener('beforeunload', function (e) { e.preventDefault(); e.returnValue = ''; });
</script>
EOH
    cat > "$M/usr/lib/auros-harden/run.sh" <<'EOS'
#!/bin/sh
# NOT PART OF AUROS. tools/hardenboottest.sh puts this into a scratch
# copy of the image; it checks what is in force, writes it down, and
# turns the machine off.
OUT=/var/lib/auros-harden; C=/etc/auros-harden
mkdir -p "$OUT"; R="$OUT/result"; : > "$R"
mode=$(cat "$C/mode")
put() { printf '%s=%s\n' "$1" "$2" >> "$R"; }
con() { printf 'AUROS-HARDEN %s\n' "$*" > /dev/ttyS0 2>/dev/null; }
mono() { systemctl show -p "$1" --value ${2:-}; }
shellpid() { pgrep -o -x aurshell; }
# The host presses keys on the machine's own keyboard (QEMU's sendkey)
# and takes screenshots when asked on the serial line; it polls once a
# second, so each request is given time.
nd=0
host() { nd=$((nd+1)); con "do $nd $*"; sleep 4; }
con "up mode=$mode"

i=0
while [ $i -lt 400 ]; do
    s=$(systemctl is-system-running 2>/dev/null)
    case "$s" in running|degraded) break ;; esac
    sleep 1; i=$((i+1))
done
put state "$s"
i=0; while [ -z "$(shellpid)" ] && [ $i -lt 120 ]; do sleep 1; i=$((i+1)); done
p=$(shellpid); put desktop "${p:-none}"
u=$(ps -o user= -p "${p:-1}" | tr -d ' '); put desktop_user "$u"
# The desktop and what it starts hold NO capability: bubblewrap (Flatpak) refuses to run
# with any (PARITY found CAP_WAKE_ALARM in all four sets). Inh, Prm, Eff, Amb.
put desktop_caps "$(grep -E '^Cap(Inh|Prm|Eff|Amb):' /proc/"${p:-1}"/status | awk '{print $2}' | tr '\n' ' ')"
put failed_units "$(systemctl --failed --plain --no-legend | awk '{print $1}' | tr '\n' ' ')"

# ── the cost, before anything below disturbs it ──
sleep 60
put finish_us "$(mono FinishTimestampMonotonic)"
put userspace_us "$(mono UserspaceTimestampMonotonic)"
put desktop_us "$(mono ExecMainStartTimestampMonotonic aurshell.service)"
awk '/^MemTotal:/ {t=$2} /^MemAvailable:/ {a=$2} END {print "used_kb=" t-a; print "memtotal_kb=" t}' /proc/meminfo >> "$R"
put analyze "$(systemd-analyze 2>/dev/null | head -1)"
# CPU time the guest spent (not wall time: a busy host stretches that
# tenfold). user+nice+system+irq+softirq, in ms, summed over both cores.
awk '/^cpu / { print "cpu_busy_ms=" ($2 + $3 + $4 + $7 + $8) * 10; print "cpu_steal_ms=" $9 * 10 }' /proc/stat >> "$R"
systemd-analyze blame 2>/dev/null | head -15 > "$OUT/blame.txt"
systemd-analyze critical-chain 2>/dev/null > "$OUT/chain.txt"
cp /proc/meminfo "$OUT/meminfo.txt"; ps -eo rss,comm --sort=-rss | head -25 > "$OUT/ps.txt"
systemctl list-units --plain --no-legend --state=active --type=service | awk '{print $1}' > "$OUT/services.txt"
if [ "$mode" = measure ]; then sync; systemctl poweroff; exit 0; fi

# ── kernel ──
put cmdline "$(cat /proc/cmdline)"
put secureboot "$(mokutil --sb-state 2>&1 | head -1)"
put lockdown "$(cat /sys/kernel/security/lockdown 2>&1)"
# Login prompts a person at the machine could reach (reported, not judged
# here: the account's password is another stream's).
put gettys "$(systemctl list-units --plain --no-legend --state=active 'getty@*' 'serial-getty@*' 'autovt@*' | awk '{print $1}' | tr '\n' ' ')"
put nautovts "$(busctl get-property org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager NAutoVTs 2>/dev/null | awk '{print $2}')"
for k in $(cat "$C/keys"); do put "sysctl.$k" "$(sysctl -n "$k" 2>/dev/null)"; done
# An ordinary program asking for a DCCP socket is how CVE-2017-6074
# got its module loaded.
setpriv --reuid="$u" --regid="$(id -g "$u")" --init-groups python3 -c \
    'import socket; socket.socket(2, 6, 33)' > "$OUT/dccp.txt" 2>&1   # AF_INET, SOCK_DCCP, IPPROTO_DCCP
put dccp_socket "$?"
grep -q '^dccp ' /proc/modules && put dccp_loaded yes || put dccp_loaded no
setpriv --reuid="$u" --regid="$(id -g "$u")" --init-groups \
    aa-exec -p flatpak -- bwrap --unshare-user --unshare-pid --ro-bind / / /bin/true > "$OUT/bwrap.txt" 2>&1
put bwrap "$?"

# ── network ──
i=0; while [ -z "$(ip -4 -o addr show scope global)" ] && [ $i -lt 90 ]; do sleep 1; i=$((i+1)); done
put ipv4 "$(ip -4 -o addr show scope global | awk '{print $4}' | head -1)"
python3 -c 'import urllib.request; print(urllib.request.urlopen("http://10.0.2.100:8080/ok", timeout=15).read().decode().strip())' \
    > "$OUT/outbound.txt" 2>&1
put outbound "$(head -1 "$OUT/outbound.txt")"
ss -H -lntup > "$OUT/listen.txt" 2>&1
python3 /usr/lib/auros-harden/listen.py "$OUT/listener.txt" 40 &
i=0; while ! grep -q listening "$OUT/listener.txt" 2>/dev/null && [ $i -lt 20 ]; do sleep 1; i=$((i+1)); done
ss -H -lntup > "$OUT/listen-probe.txt" 2>&1
con "probe-ready"
wait
grep -q '^tcp from' "$OUT/listener.txt" && put probe_tcp_reached yes || put probe_tcp_reached no
grep -q '^udp from' "$OUT/listener.txt" && put probe_udp_reached yes || put probe_udp_reached no
r=$(timeout 15 avahi-resolve -4 -n "$(hostname).local" 2>&1 | head -1)
case "$r" in *"$(hostname).local"*[0-9]*) put avahi_self answered ;; *) put avahi_self silent ;; esac
put avahi_said "$r"
put journal_max "$(journalctl -b -u systemd-journald -o cat | grep -E '^System Journal .* max ' | tail -1 | sed 's/.* max \([0-9.]*[KMGT]\).*/\1/')"

# ── an interrupted update ──
put dpkg_less "$(dpkg-query -W -f='${Status}' less)"
put dpkg_repair "$(journalctl -b -u auros-dpkg-repair -o cat | tr '\n' ' ' | cut -c1-200)"

# ── S2: a remote-control program, the first time ──
# As polkit sees it from her desktop: the subject is aurshell, in her
# session, and polkitd is the booted one, in its systemd sandbox.
sp=$(shellpid); g=$(id -g "$u")
dbus-monitor --system "type='signal',interface='org.auros.Guard'" > "$OUT/guard-signal.txt" 2>&1 &
mp=$!; sleep 2
FPREF=app/com.anydesk.Anydesk/x86_64/stable
pkcheck --action-id org.freedesktop.Flatpak.app-install --process "$sp" --detail ref "$FPREF" > "$OUT/pk1.txt" 2>&1
put remote_first "$?"
sleep 2
# Keys only when a card was asked for: on the home screen, Tab Tab
# Return would start a program and spoil what follows.
if grep -q '"anydesk"' "$OUT/guard-signal.txt"; then
    put remote_signal yes
    host shot remote-card
    host keys tab tab ret      # "I called someone I know", from the keyboard
    put remote_consent "$(stat -c %U /run/auros-remote-access/anydesk 2>/dev/null || echo none)"
    pkcheck --action-id org.freedesktop.Flatpak.app-install --process "$sp" --detail ref "$FPREF" > "$OUT/pk2.txt" 2>&1
    put remote_after_yes "$?"
    host keys esc              # "Now try installing it again" -> OK
else put remote_signal no; fi
# (gdebi is not in the image; PackageKit's install is what a .deb or the
# store's system packages go through.)
pkcheck --action-id org.freedesktop.packagekit.package-install --process "$sp" --detail package_ids \
    "teamviewer;15.58.4;amd64;local" > "$OUT/pk3.txt" 2>&1
put remote_deb "$?"
# Why a PackageKit install of a remote-control program might be let
# through: every rule polkit reads, what the action's own defaults are,
# and the same question asked three more ways.
{
    echo "== rules"; ls -l /etc/polkit-1/rules.d /usr/share/polkit-1/rules.d 2>&1
    echo "== pkaction"; pkaction --action-id org.freedesktop.packagekit.package-install --verbose 2>&1 | head -20
    for v in "anydesk;6.3.2;amd64;local" "teamviewer;15.58.4;amd64;local" "teamviewer-host;15.58.4;amd64;local"; do
        pkcheck --action-id org.freedesktop.packagekit.package-install --process "$sp" --detail package_ids "$v" > /dev/null 2>&1
        echo "package_ids=$v -> pkcheck exit $?"
    done
    echo "== polkitd"; journalctl -b --no-pager -o cat _COMM=polkitd 2>&1 | tail -30
} > "$OUT/polkit.txt" 2>&1
sleep 2
if grep -q '"teamviewer"' "$OUT/guard-signal.txt"; then
    put remote_deb_signal yes; host keys esc   # "Don't install it"
else put remote_deb_signal no; fi
kill "$mp" 2>/dev/null

# ── S2: the way out of a page that will not let her go ──
# aurshell's own runtime directory has its Wayland socket (wayland-0).
xrd=$(tr '\0' '\n' < /proc/"$sp"/environ | sed -n 's/^XDG_RUNTIME_DIR=//p')
wd=$(ls "$xrd" 2>/dev/null | grep -m1 '^wayland-[0-9][0-9]*$')
put wayland "$wd $xrd"
# /usr/lib/firefox/firefox is a launcher that execs firefox-bin: the
# process to look for is called firefox-bin (a first version looked for
# "firefox", found nothing, and called every page "closed").
page() { # start Firefox full screen on the trap page, as her
    setpriv --reuid="$u" --regid="$g" --init-groups env -i HOME="/home/$u" USER="$u" \
        PATH=/usr/bin:/bin XDG_RUNTIME_DIR="$xrd" WAYLAND_DISPLAY="$wd" MOZ_ENABLE_WAYLAND=1 \
        firefox --kiosk file:///usr/lib/auros-harden/trap.html >> "$OUT/firefox.txt" 2>&1 &
    i=0; while ! pgrep -u "$u" -x firefox-bin >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i+1)); done
    # A new profile, software drawing, a busy host: the page is not there
    # for a good while (a first version looked after 25 s and saw the
    # window's logo).
    sleep 70
}
gone() { # firefox ended within 15 s?
    i=0; while pgrep -u "$u" -x firefox-bin >/dev/null && [ $i -lt 15 ]; do sleep 1; i=$((i+1)); done
    pgrep -u "$u" -x firefox-bin >/dev/null && echo still-open || echo closed
}
alive() { pgrep -u "$u" -x firefox-bin >/dev/null && echo yes || echo no; }
page
put page_up "$(alive)"
ls -l /usr/bin/firefox /usr/lib/firefox/firefox >> "$OUT/firefox.txt" 2>&1
host shot page                 # the trap page, red
host keys esc tab ret          # ONE Escape: no card, so the page keeps her keys
host shot after-esc
put alive_after_esc "$(alive)"
host keys ctrl-alt-delete      # Ctrl+Alt+Delete: the card, not a restart
host shot cad
host keys esc                  # the card's own Escape: back to the page
host shot back
put alive_after_back "$(alive)"
host keys esc esc esc          # three: the card, over the page
host shot esc3
host keys tab ret              # "Close this page"
put out_three_esc "$(gone)"
# (A second Firefox started after this one was killed died of a Wayland
# protocol error from aurwl: "xdg_surface: no configure with serial N
# was sent" -- see docs/handoff/streams/S1.md. Hence one launch, not two.)
pkill -u "$u" -x firefox-bin 2>/dev/null

# ── the desktop crashing, three times ──
for c in 1 2 3; do
    p=$(shellpid); t0=$(date +%s)
    kill -SEGV "$p"
    i=0
    while [ $i -lt 150 ]; do
        q=$(shellpid); [ -n "$q" ] && [ "$q" != "$p" ] && break
        sleep 1; i=$((i+1))
    done
    put "crash${c}_back_after" "$(( $(date +%s) - t0 ))s"
done
sleep 5
q=$(shellpid)
put safe_cmdline "$(tr '\0' ' ' < /proc/"${q:-1}"/cmdline 2>/dev/null)"
put safe_level "$(cat /run/auros-guard/level 2>/dev/null || echo none)"

# ── a runaway program ──
# LAST, because without zram and earlyoom the machine may never answer
# again: everything above is on the disk first, and the host watches
# for hog-end on the serial line (it gives up after 300 s and says so).
grep -q '^/dev/zram' /proc/swaps && put zram yes || put zram no
sync; con "hog-start"
p0=$(shellpid); n0=$(mono NRestarts aurshell.service)
since=$(date '+%Y-%m-%d %H:%M:%S'); t0=$(date +%s)
rm -f "$OUT/hog.rc"
( setpriv --reuid="$u" --regid="$(id -g "$u")" --init-groups tail /dev/zero; echo $? > "$OUT/hog.rc" ) &
peak=0; i=0
while [ ! -f "$OUT/hog.rc" ] && [ $i -lt 240 ]; do
    s=$(awk '/^SwapTotal:/ {t=$2} /^SwapFree:/ {f=$2} END {print t-f}' /proc/meminfo)
    [ "$s" -gt "$peak" ] && peak=$s
    sleep 1; i=$((i+1))
done
if [ -f "$OUT/hog.rc" ]; then put hog_secs "$(( $(date +%s) - t0 ))"; put hog_rc "$(cat "$OUT/hog.rc")"
else pkill -9 -x tail; put hog_secs never; fi
put swap_peak_kb "$peak"
con "hog-end"
sleep 3
journalctl -b -u earlyoom --since "$since" -o cat > "$OUT/earlyoom.txt" 2>&1
journalctl -k -b --since "$since" -o cat > "$OUT/kernel.txt" 2>&1
if grep -Eq 'sending SIG(TERM|KILL) to process [0-9]+ .*"tail"' "$OUT/earlyoom.txt"; then put hog_killer earlyoom
elif grep -Eq 'Out of memory: Killed process [0-9]+ \(tail\)' "$OUT/kernel.txt"; then put hog_killer kernel
else put hog_killer none; fi
[ "$(shellpid)" = "$p0" ] && [ "$(mono NRestarts aurshell.service)" = "$n0" ] \
    && put desktop_after_hog same || put desktop_after_hog "changed ($p0 -> $(shellpid))"

journalctl -b --no-pager -o short-monotonic -u aurshell -u earlyoom -u zramswap -u nftables \
    -u auros-dpkg-repair -u auros-harden > "$OUT/journal.txt" 2>&1
con "done"
sync
systemctl poweroff
EOS
    chmod 755 "$M/usr/lib/auros-harden/run.sh"
    cat > "$M/etc/systemd/system/auros-harden.service" <<'EOU'
[Unit]
Description=NOT PART OF AUROS: tools/hardenboottest.sh's harness
After=multi-user.target
[Service]
Type=simple
ExecStart=/usr/lib/auros-harden/run.sh
[Install]
WantedBy=multi-user.target
EOU
    ln -sf /etc/systemd/system/auros-harden.service \
        "$M/etc/systemd/system/multi-user.target.wants/auros-harden.service"
    # A power cut in the middle of an update: one package unpacked
    # and never configured.
    sed -i '/^Package: less$/,/^$/ s/^Status: install ok installed$/Status: install ok half-configured/' \
        "$M/var/lib/dpkg/status"
}

# ── the same image, every S1 control taken out ──────────────────────
strip_controls() {
    M="$T/m"
    rm -f "$M/etc/sysctl.d/60-auros-hardening.conf" "$M/etc/sysctl.d/61-auros-reliability.conf" \
          "$M/etc/sysctl.d/99-protect-links.conf" "$M/etc/modprobe.d/auros-hardening.conf" \
          "$M/etc/systemd/system/sysinit.target.wants/nftables.service" \
          "$M/etc/systemd/system/multi-user.target.wants/zramswap.service" \
          "$M/etc/systemd/system/multi-user.target.wants/earlyoom.service" \
          "$M/etc/systemd/system/multi-user.target.wants/auros-dpkg-repair.service" \
          "$M/etc/systemd/system/aurshell.service.d/50-auros-safe-mode.conf" \
          "$M/etc/systemd/journald.conf.d/50-auros.conf" \
          "$M/etc/polkit-1/rules.d/40-auros-remote-access.rules"   # S2's
    sed -i 's/^disable-publishing=yes/disable-publishing=no/' "$M/etc/avahi/avahi-daemon.conf"
    for o in $KOPTS; do sed -i "s/ $o\( \|\$\)/\1/" "$M/boot/grub/grub.cfg"; done
    # The boot menu's lock (stream BT): no superusers, no password.
    sed -i '/^[[:space:]]*set[[:space:]]\{1,\}superusers=/d; /^[[:space:]]*password\(_pbkdf2\)\{0,1\}[[:space:]]/d' \
        "$M/boot/grub/grub.cfg"
}

# ── the boot menu, from the keyboard ────────────────────────────────
#
# Anyone at the keyboard gets GRUB's menu. If it lets them edit a start
# line (add init=/bin/bash), open GRUB's own command line, or pick an
# entry that starts a root shell, without a password, then everything
# else in this file is decoration. grub.cfg.in puts the menu on the
# serial line too, so the keys a person would press are pressed here.
shell_entry() { # rootfs -> "ID INDEX" of the first menu entry whose kernel starts a shell
    awk '/^[[:space:]]*menuentry[[:space:]]/ { n++; id = ""; if (match($0, /--id[ =][^ {]+/)) id = substr($0, RSTART + 5, RLENGTH - 5) }
         /^[[:space:]]*linux[[:space:]]/ && / init=\/(usr\/)?bin\/(ba|da)?sh( |$)/ && id != "" { print id, n - 1; exit }' \
        "$1/boot/grub/grub.cfg"
}
menu_probe() { # disk label edit|console|shell [index] -> asks | opens | nomenu | nothing
    NV="$T/vars.$2"; cp "$VARS" "$NV"; S="$T/sock.$2"; rm -f "$S"
    # shellcheck disable=SC2086
    qemu-system-x86_64 -machine q35,smm=on -accel $ACCEL -m "$MEM" -smp 2 \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$NV" \
        -drive file="$1",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -device virtio-vga -display none -nic none \
        -chardev "socket,id=s0,path=$S,server=on,wait=on" -serial chardev:s0 > "$T/qemu.$2" 2>&1 &
    QP=$!
    python3 - "$S" "$3" "$T/serial.$2" "${4:-0}" <<'EOPY'
import socket, sys, time
path, action, logf, idx = sys.argv[1:5]
s = socket.socket(socket.AF_UNIX)
for _ in range(200):
    try: s.connect(path); break
    except OSError: time.sleep(0.1)
s.settimeout(0.5); log = open(logf, 'wb'); buf = b''
def wait(marks, secs):
    global buf
    end = time.time() + secs
    while time.time() < end:
        try: d = s.recv(4096)
        except socket.timeout: d = b''
        if d: buf += d; log.write(d); log.flush()
        for i, m in enumerate(marks):
            if m in buf: return i
    return -1
if action == 'shell':
    # Down to the entry that starts a shell, Return. Not "Run /init as
    # init process": that is the initramfs, which every start passes
    # through. bash says the others on the console.
    if wait([b'automatically in'], 120) < 0: print('nomenu'); sys.exit()
    buf = b''; time.sleep(0.3)
    for _ in range(int(idx)):
        s.send(b'\x1b[B'); time.sleep(0.3)
    s.send(b'\r')
    r = wait([b'Enter username', b'no job control', b'bash-5', b'root@'], 150)
else:
    if wait([b'automatically in'], 120) < 0: print('nomenu'); sys.exit()
    buf = b''; time.sleep(0.3)
    s.send(b'e' if action == 'edit' else b'c')
    r = wait([b'Enter username', b'Emacs-like', b'BASH-like', b'grub>', b'setparams'], 30)
print('asks' if r == 0 else 'opens' if r > 0 else 'nothing')
EOPY
    kill -9 "$QP" 2>/dev/null; wait "$QP" 2>/dev/null; QP=""
}

# ── one power-on ────────────────────────────────────────────────────
python3 - "$T/www" <<'EOPY'
import os, sys
os.makedirs(sys.argv[1]); open(os.path.join(sys.argv[1], 'ok'), 'w').write('AUROS-OUTBOUND-OK\n')
EOPY
python3 -m http.server "$HP" --bind 127.0.0.1 --directory "$T/www" > "$T/www.log" 2>&1 &
WP=$!

probe() { # what a stranger on her network gets
    python3 - "$TP" "$UP" <<'EOPY'
import socket, sys
got = b''
try:
    s = socket.create_connection(('127.0.0.1', int(sys.argv[1])), timeout=5)
    s.settimeout(8); got = s.recv(64)
except Exception:
    pass
print('tcp_banner=' + ('yes' if b'AUROS-PROBE' in got else 'no'))
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for _ in range(3):
    u.sendto(b'AUROS-UDP-PROBE', ('127.0.0.1', int(sys.argv[2])))
EOPY
}

# The machine's own keyboard and screen, through QEMU's monitor: what
# the harness asks for on the serial line ("do N keys esc esc esc",
# "do N shot NAME"), carried out once each.
hmp() { # label "cmd;cmd;..."
    python3 - "$T/mon.$1" "$2" <<'EOPY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(3)
def drain():
    b = b''
    try:
        while not b.endswith(b'(qemu) '):
            d = s.recv(4096)
            if not d: break
            b += d
    except socket.timeout:
        pass
drain()
for c in sys.argv[2].split(';'):
    s.send(c.encode() + b'\n'); drain(); time.sleep(0.15)
EOPY
}
host_do() { # label
    f="$T/done.$1"; d=$(cat "$f" 2>/dev/null || echo 0)
    tr -d '\r' < "$SER" | grep -ao 'AUROS-HARDEN do [0-9][0-9]* [a-z]* .*' | while read -r _ _ n verb args; do
        [ "$n" -gt "$d" ] || continue
        case "$verb" in
            keys) c=""; for k in $args; do c="$c${c:+;}sendkey $k"; done; hmp "$1" "$c" ;;
            shot) hmp "$1" "screendump $T/shot.$1.$args.ppm" ;;
        esac
        echo "$n" > "$f"; d=$n
    done
}

power_on() { # disk label mode timeout
    mount_root "$1" || return 1
    printf '%s\n' "$3" > "$T/m/etc/auros-harden/mode"
    umount_root
    SER="$T/serial.$2"; : > "$SER"; NV="$T/vars.$2"; cp "$VARS" "$NV"
    # shellcheck disable=SC2086
    qemu-system-x86_64 -machine q35,smm=on -accel $ACCEL -m "$MEM" -smp 2 -no-reboot \
        -global driver=cfi.pflash01,property=secure,value=on \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
        -drive if=pflash,format=raw,unit=1,file="$NV" \
        -drive file="$1",format=raw,if=none,id=d0 -device virtio-blk-pci,drive=d0 \
        -device virtio-vga -display none -serial file:"$SER" \
        -netdev "user,id=n0,restrict=on,hostfwd=tcp:127.0.0.1:$TP-:7777,hostfwd=udp:127.0.0.1:$UP-:631,guestfwd=tcp:10.0.2.100:8080-tcp:127.0.0.1:$HP" \
        -device virtio-net-pci,netdev=n0 -monitor "unix:$T/mon.$2,server=on,wait=off" > "$T/qemu.$2" 2>&1 &
    QP=$!; i=0; probed=0; hog=""; FROZE=no
    while kill -0 "$QP" 2>/dev/null && [ $i -lt "$4" ]; do
        if [ $probed = 0 ] && grep -aq 'AUROS-HARDEN probe-ready' "$SER"; then
            probe > "$T/probe.$2"; probed=1
        fi
        host_do "$2"
        # The memory hog: a machine that has not said hog-end 300 s
        # after hog-start has stopped answering. That is the finding.
        if [ -z "$hog" ] && grep -aq 'AUROS-HARDEN hog-start' "$SER"; then hog=$i; fi
        if [ -n "$hog" ] && [ $((i - hog)) -ge 300 ] && ! grep -aq 'AUROS-HARDEN hog-end' "$SER"; then
            FROZE=yes; break
        fi
        sleep 1; i=$((i+1))
    done
    OFF=yes; kill -0 "$QP" 2>/dev/null && { kill -9 "$QP"; OFF="no (killed after ${i}s)"; }
    wait "$QP" 2>/dev/null; QP=""
    mount_root "$1" || return 1
    mkdir -p "$T/rec.$2"; cp -a "$T/m/var/lib/auros-harden/." "$T/rec.$2/" 2>/dev/null
    printf 'powered_off=%s\nwall_s=%s\nhog_froze=%s\n' "$OFF" "$i" "$FROZE" >> "$T/rec.$2/result"
    k=$(tr -d '\r' < "$SER" | grep -ao 'Out of memory: Killed process [0-9]* ([a-z]*)' | head -1)
    [ -n "$k" ] && printf 'kernel_oom=%s\n' "$k" >> "$T/rec.$2/result"
    [ -f "$T/probe.$2" ] && cat "$T/probe.$2" >> "$T/rec.$2/result"
    umount_root
}

# What a screenshot shows: the trap page is dark red, the desktop's
# cards are cream. Appended to that boot's record as
# shot_<name>=page|card|other.
cat > "$T/shotkind.py" <<'EOPY'
import sys
d = open(sys.argv[1], 'rb').read().split(b'\n', 3)
w, h = map(int, d[1].split()); px = d[3]
def at(x, y): i = (y * w + x) * 3; return px[i], px[i + 1], px[i + 2]
# The margins, not the middle: a first-run dialog may sit over the page.
pts = [at(x, y) for x, y in ((60, 100), (1220, 100), (60, 650), (1220, 650))]
red = sum(1 for r, g, b in pts if r > 30 and g < 20 and b < 20)
# cream paper, not the white of a window still starting
cream = sum(1 for r, g, b in pts if r > 200 and g > 200 and b > 190 and r - b >= 6)
print('page' if red >= 3 else 'card' if cream >= 3 else 'other')
EOPY
shotkinds() { # label
    for f in "$T"/shot."$1".*.ppm; do
        [ -f "$f" ] || continue
        n=${f#"$T"/shot."$1".}; n=${n%.ppm}
        k=$(python3 "$T/shotkind.py" "$f" 2>&1 | tail -1)
        echo "shot_$n=$k" >> "$T/rec.$1/result"
        echo "    $1 screenshot $n: $k"
    done
}

# ── the two disks ───────────────────────────────────────────────────
# The sysctl list (key, value) is hardentest's, so the two never disagree.
awk "/^done <<'EOF'\$/ {f=1; next} f && /^EOF\$/ {exit} f {print \$1, \$2}" tools/hardentest.sh > "$T/keys"
[ -s "$T/keys" ] || { echo "could not read the sysctl list out of tools/hardentest.sh"; exit 2; }
echo
say "## Hardening, booted ($(basename "$IMG"), ${MEM} MB, 2 cores, Secure Boot, $(echo "$ACCEL" | cut -d' ' -f1))"
say ""
# ONE working copy of the disk at a time (the image is 2.6 GB of real
# data; two copies at once are 5 GB on a machine other people share). It
# is made from the original, the harness put in, the controls taken out
# for "base", and deleted when its boots are done.
make_disk() { # hard|base  [static]
    rm -f "$T/disk.img"
    cp --sparse=always "$IMG" "$T/disk.img" || exit 2
    mount_root "$T/disk.img" || exit 2
    inject
    [ "$1" = base ] && strip_controls
    if [ "${2:-}" = static ]; then
        RFS="$T/m" HARDEN_QUIET=1 sh tools/hardentest.sh > "$T/static.$1" 2>&1; echo $? > "$T/static.$1.rc"
        # The static checks, each shown to fail when its control is taken
        # away, on this very image (a few MB of it are copied out).
        [ "$1" = hard ] && { RFS="$T/m" sh tools/hardentest-mutants.sh > "$T/mutants.out" 2>&1; echo $? > "$T/mutants.rc"; }
    fi
    umount_root
}
free_gb=$(df -Pk "$T" | awk 'NR == 2 { printf "%d", $4 / 1048576 }')
need=$(( $(du -k "$IMG" | cut -f1) / 1048576 + ${HARDEN_MINFREE_GB:-8} ))
[ "$free_gb" -ge "$need" ] || { echo "only ${free_gb} GB free where the disk copy goes; need $need (HARDEN_MINFREE_GB)"; exit 2; }

for d in hard base; do
    make_disk $d static
    echo "  $d: the boot menu (edit, command line, recovery shell)"
    mount_root "$T/disk.img" || exit 2
    se=$(shell_entry "$T/m"); [ $d = hard ] && SHELL_ID=${se% *}
    umount_root
    { echo "grub_edit=$(menu_probe "$T/disk.img" "$d.edit" edit)"
      echo "grub_console=$(menu_probe "$T/disk.img" "$d.console" console)"
      if [ -n "$se" ]; then
          echo "grub_shell=$(menu_probe "$T/disk.img" "$d.shell" shell "${se#* }")"
      else echo "grub_shell=none"; fi
    } > "$T/menu.$d"
    echo "  $d: a full boot with every check (about 8 minutes under KVM)"
    power_on "$T/disk.img" $d full "$FULL_TIMEOUT"
    rm -f "$T/disk.img"
done
RH="$T/rec.hard/result"; RB="$T/rec.base/result"
cat "$T/menu.hard" >> "$RH"; cat "$T/menu.base" >> "$RB"
{ echo "mutants_rc=$(cat "$T/mutants.rc" 2>/dev/null)"; echo "mutants=$(tail -1 "$T/mutants.out" 2>/dev/null)"; } >> "$RH"
# (not inside the loop above: a first version classified there and its
# lines never reached the record; here the files are known to be there)
ls "$T"/shot.*.ppm 2>&1 | sed 's/^/    screenshot file: /'
shotkinds hard; shotkinds base

# ── the verdicts ────────────────────────────────────────────────────
val() { awk -v k="$1" 'index($0, k "=") == 1 { v = substr($0, length(k) + 2) } END { print v }' "$2"; }
# row KIND LABEL PREDICATE [ARGS]: "PREDICATE FILE [ARGS]" is asked of
# both boots' records. KIND
#   control  the hardened boot passes, and the boot without the S1
#            controls FAILS (else the check proves nothing)
#   both     both pass: a precondition (so a control check cannot
#            "fail without its control" because that machine never
#            came up), or something hardening must not break
#   keep     the hardened boot passes; the other column is shown
#   pinned   a sysctl whose Ubuntu default is already ours: only the
#            static test can see our line go
row() {
    kind=$1; label=$2; pred=$3; shift 3
    if "$pred" "$RH" "$@" 2>/dev/null; then h=ok; else h=FAIL; fi
    if "$pred" "$RB" "$@" 2>/dev/null; then b=passes; else b=fails; fi
    checked=$((checked+1)); v=ok
    if [ $h = FAIL ]; then v=FAIL; fail=$((fail+1))
    elif [ "$kind" = control ] && [ $b = passes ]; then v="FAIL: passes without its control"; fail=$((fail+1))
    elif [ "$kind" = both ] && [ $b = fails ]; then v="FAIL: the other boot needed it too"; fail=$((fail+1))
    elif [ "$kind" = pinned ]; then v="ok (Ubuntu's default is the same)"; fi
    say "| $label | $h | $b | $v |"
}
is()       { [ "$(val "$2" "$1")" = "$3" ]; }
not_is()   { [ "$(val "$2" "$1")" != "$3" ] && [ -n "$(val "$2" "$1")" ]; }
has_opt()  { case " $(val cmdline "$1") " in *" $2 "*) return 0 ;; esac; return 1; }
up()       { case "$(val desktop "$1")" in ''|none) return 1 ;; esac; [ -n "$(val safe_level "$1")" ]; }
locked()   { case "$(val lockdown "$1")" in *"[integrity]"*|*"[confidentiality]"*) return 0 ;; esac; return 1; }
sb_on()    { case "$(val secureboot "$1")" in *enabled*) return 0 ;; esac; return 1; }
static_ok() { [ "$(cat "$T/static.$(basename "$(dirname "$1")" | sed 's/^rec\.//').rc")" = 0 ]; }
num() { v=$(val "$1" "$2"); echo "${v:-0}"; }
outbound() { is "$1" outbound AUROS-OUTBOUND-OK && [ -n "$(val ipv4 "$1")" ]; }
# Listening sockets an outsider could talk to: not loopback, and not
# the ones a desktop needs -- avahi's (printer search; it also keeps two
# random UDP ports for its own unicast questions), DHCP and DHCPv6. The
# firewall lets in only the answers to her own questions on those.
strangers() { # listen-file
    awk '{ a = $5; p = a; sub(/.*:/, "", p)
           if (a ~ /^(127\.|\[::1\]|\[::ffff:127\.)/ || a ~ /%lo:/) next
           if ($1 == "udp" && ($7 ~ /"avahi-daemon"/ || p == 68 || p == 546)) next
           print $1, a, $7 }' "$1" 2>/dev/null
}
no_listeners() { f="$(dirname "$1")/listen.txt"; [ -s "$f" ] && [ -z "$(strangers "$f")" ]; }
sees_listener() { strangers "$(dirname "$1")/listen-probe.txt" | grep -q ':7777 '; }
tcp_refused() { is "$1" tcp_banner no && is "$1" probe_tcp_reached no && grep -q '^listening' "$(dirname "$1")/listener.txt"; }
udp_refused() { is "$1" probe_udp_reached no && grep -q '^listening' "$(dirname "$1")/listener.txt"; }
mb() { # 200.0M -> 200
    echo "$1" | awk '{ n = $0 + 0; u = substr($0, length($0))
        if (u == "K") n /= 1024; if (u == "G") n *= 1024; if (u == "T") n *= 1048576; printf "%d", n }'
}
journal_capped() { m=$(val journal_max "$1"); [ -n "$m" ] && [ "$(mb "$m")" -le 200 ]; }
dccp_refused() { not_is "$1" dccp_socket 0 && is "$1" dccp_loaded no; }
hog_by_earlyoom() { is "$1" hog_killer earlyoom && [ "$(val hog_secs "$1")" != never ]; }
zram_used() { is "$1" zram yes && [ "$(val swap_peak_kb "$1")" -gt 102400 ]; }
safe_mode() { case "$(val safe_cmdline "$1")" in *"--shell /usr/share/auros/shells/rail.shell --conf /etc/auros/shell.conf"*) is "$1" safe_level 1 ;; *) return 1 ;; esac; }
page_shown()    { is "$1" page_up yes && is "$1" shot_page page; }
esc_one_stays() { page_shown "$1" && is "$1" alive_after_esc yes && is "$1" shot_after-esc page; }
cad_card()      { page_shown "$1" && is "$1" shot_cad card; }
back_ok()       { cad_card "$1" && is "$1" alive_after_back yes && is "$1" shot_back page; }
esc3_closes()   { back_ok "$1" && is "$1" shot_esc3 card && is "$1" out_three_esc closed; }
remote_gate() { not_is "$1" remote_first 0 && is "$1" remote_signal yes; }
remote_yes() { is "$1" remote_consent "$(val desktop_user "$1")" && is "$1" remote_after_yes 0; }
deb_gate() { not_is "$1" remote_deb 0 && is "$1" remote_deb_signal yes; }
dpkg_fixed() { is "$1" dpkg_less "install ok installed"; }
no_caps() { [ "$(val desktop_caps "$1")" = "0000000000000000 0000000000000000 0000000000000000 0000000000000000 " ]; }

say "| check | hardened | controls removed | verdict |"
say "|---|---|---|---|"
row both    "the machine starts, the desktop comes up, every check runs" up
row both    "Secure Boot is on" sb_on
row control "tools/hardentest.sh passes on the disk" static_ok
row keep    "tools/hardentest-mutants.sh: every static check fails without its control" is mutants_rc 0
row control "GRUB: editing a start line asks for a password" is grub_edit asks
row control "GRUB: its command line asks for a password" is grub_console asks
if [ -n "$SHELL_ID" ]; then
    row control "GRUB: the shell entry ($SHELL_ID) asks for a password" is grub_shell asks
else
    row keep "GRUB: no menu entry starts a shell" is grub_shell none
fi
row both    "the kernel is locked down (Secure Boot does it here too)" locked
for o in $KOPTS; do row control "kernel line: $o" has_opt "$o"; done
while read -r key want; do
    if [ "$(val "sysctl.$key" "$RB")" = "$want" ]; then kind=pinned; else kind=control; fi
    row "$kind" "$key = $want" is "sysctl.$key" "$want"
done < "$T/keys"
row control "an ordinary program cannot load dccp (socket refused)" dccp_refused
row both    "Flatpak's sandbox (bubblewrap, userns) still starts" is bwrap 0
row both    "the desktop holds no capability (bubblewrap refuses any: no Flatpak program would open)" no_caps
row both    "her own traffic works (DHCP address, a page from outside)" outbound
row both    "nothing listens on the network (cups-browsed is gone)" no_listeners
row both    "...and that check sees a listener when there is one" sees_listener
row control "a stranger's TCP connection reaches nothing (firewall)" tcp_refused
row control "a stranger's UDP 631 packet reaches nothing (firewall)" udp_refused
row control "the computer does not announce itself (avahi)" is avahi_self silent
row control "the system journal is capped at 200 MB" journal_capped
row control "an update cut off half-way is finished at the next start" dpkg_fixed
row control "a runaway program fills zram swap first" zram_used
row control "...and earlyoom stops it, not the kernel's last resort" hog_by_earlyoom
row keep    "...and the desktop is the same process afterwards" is desktop_after_hog same
row control "...and the machine keeps answering (hog-end within 300 s)" is hog_froze no
row control "three desktop crashes: the 4th start is safe mode" safe_mode
row control "S2: AnyDesk from the store, first time: refused, card asked for" remote_gate
row keep    "S2: ...she answers on the card with the keyboard; the next try works" remote_yes
row control "S2: TeamViewer through PackageKit: refused, card asked for" deb_gate
row both    "S2: Firefox full screen on a trap page" page_shown
row both    "S2: one Escape does not take the page away, and shows no card" esc_one_stays
row both    "S2: Ctrl+Alt+Delete over the page: the card, not a restart" cad_card
row both    "S2: the card's Escape leaves her page as it was" back_ok
row both    "S2: Escape x3 over the page: the card, and Close this page closes it" esc3_closes

say ""
say "Details: hog stopped after $(val hog_secs "$RH") s (exit $(val hog_rc "$RH")), zram took $(( $(num swap_peak_kb "$RH") / 1024 )) MB;" \
    "Without the controls: killer [$(val hog_killer "$RB")], kernel [$(val kernel_oom "$RB")], stopped answering: $(val hog_froze "$RB")." \
    "Crashes came back after $(val crash1_back_after "$RH") $(val crash2_back_after "$RH") $(val crash3_back_after "$RH")." \
    "Journal max $(val journal_max "$RH") (without: $(val journal_max "$RB"))." \
    "Failed units: hardened [$(val failed_units "$RH")] without [$(val failed_units "$RB")]." \
    "Login prompts running: [$(val gettys "$RH")], Ctrl-Alt-Fn consoles on demand: $(val nautovts "$RH")."

# ── the cost ────────────────────────────────────────────────────────
echo "  and $RUNS more starts of each, in turn, for the cost"
r=1
while [ $r -le "$RUNS" ]; do
    for d in hard base; do make_disk $d; power_on "$T/disk.img" "$d.m$r" measure 600; rm -f "$T/disk.img"; done
    r=$((r+1))
done
median() { sort -n | awk '{ v[NR] = $1 } END { if (NR) print v[int((NR + 1) / 2)]; else print "?" }'; }
best()   { sort -n | head -1; }
vals_of() { # disk key divisor -> one number per start, one per line
    r=1; while [ $r -le "$RUNS" ]; do
        v=$(val "$2" "$T/rec.$1.m$r/result"); [ -n "$v" ] && echo $(( v / $3 ))
        r=$((r+1)); done
}
say ""
say "| of $RUNS starts each | before (controls off): median / best | after (AurOS): median / best | every start, before / after |"
say "|---|---|---|---|"
for m in "boot finished (ms since the kernel started):finish_us:1000" \
         "desktop started (ms):desktop_us:1000" \
         "CPU the guest used until then, 2 cores (ms):cpu_busy_ms:1" \
         "RAM in use 60 s after the boot (MB):used_kb:1024"; do
    l=${m%%:*}; k=${m#*:}; dv=${k#*:}; k=${k%%:*}
    b=$(vals_of base "$k" "$dv"); h=$(vals_of hard "$k" "$dv")
    say "| $l | $(echo "$b" | median) / $(echo "$b" | best) | $(echo "$h" | median) / $(echo "$h" | best) | $(echo $b) / $(echo $h) |"
done
say "(Wall times are stretched by whatever else the host is doing: read the best of N, and the CPU row, which is not.)"

say ""
if [ "$fail" -eq 0 ]; then
    say "hardenboottest: $checked checks, all ok; every control-tied check fails without its control"
else
    say "hardenboottest: $fail of $checked checks FAILED"
fi
mkdir -p "$(dirname "$REPORT")"; cp "$T/report" "$REPORT"
# What a person debugging a FAIL needs, next to the report.
# Screenshots as PNG (the monitor writes PPM: 3 MB each).
python3 - "$T" <<'EOPY'
import glob, struct, sys, zlib
for f in glob.glob(sys.argv[1] + '/shot.*.ppm'):
    d = open(f, 'rb').read().split(b'\n', 3)
    w, h = map(int, d[1].split()); px = d[3]
    raw = b''.join(b'\x00' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def ch(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    open(f[:-4] + '.png', 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                                    + ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))
EOPY
tar -C "$T" -czf "${REPORT%.*}-evidence.tar.gz" rec.hard rec.base $(cd "$T" && ls -d rec.*.m* shot.*.png 2>/dev/null) \
    serial.hard serial.base static.hard static.base mutants.out menu.hard menu.base \
    serial.hard.edit serial.hard.console serial.base.edit serial.base.console 2>/dev/null
[ "$fail" -eq 0 ]
