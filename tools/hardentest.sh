#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  hardentest — is every hardening and recovery control in this rootfs?
#
#  Static: reads a built AurOS root filesystem and checks each control
#  in docs/security/HARDENING.md is present AND says what it should --
#  the effective sysctl value after every sysctl.d file is read in
#  systemd's order, every kernel line in the boot menu, the firewall's
#  accept rules against an allow-list, the units actually enabled, the
#  printing listener actually gone. It also runs the two recovery
#  scripts the image ships (shell-guard, dpkg-repair) against scratch
#  state, so what is tested is the shipped file, not a copy.
#
#  build/harden runs this at the seal and stops the build on a FAIL.
#  tools/hardentest-mutants.sh proves every check fails when the thing
#  it checks is removed or weakened. tools/hardenboottest.sh is the
#  same controls on a booted image.
#
#    RFS=work/forge/desktop/rootfs sh tools/hardentest.sh
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.."
RFS="${RFS:-work/forge/desktop/rootfs}"
[ -d "$RFS/etc" ] || { echo "no rootfs at $RFS (set RFS=)"; exit 2; }

fail=0; checked=0
ok()  { checked=$((checked+1)); [ -n "${HARDEN_QUIET:-}" ] || printf '    %-62s %s\n' "$1" "ok"; }
bad() { checked=$((checked+1)); fail=$((fail+1)); printf '    %-62s %s\n' "$1" "FAIL"
        shift; for m in "$@"; do printf '      %s\n' "$m"; done; }
say() { [ -n "${HARDEN_QUIET:-}" ] || printf '%s\n' "$*"; }

T=$(mktemp -d "${TMPDIR:-/tmp}/hardentest.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT

# A path inside the rootfs, following symlinks (absolute ones are
# relative to the rootfs, not to this machine).
inroot() {
    p="$1"; n=0
    while [ -L "$RFS$p" ] && [ $n -lt 20 ]; do
        l=$(readlink "$RFS$p")
        case "$l" in /*) p="$l" ;; *) p="$(dirname "$p")/$l" ;; esac
        n=$((n+1))
    done
    printf '%s%s' "$RFS" "$p"
}
enabled() { # target unit -> 0 if the enable link exists
    [ -L "$RFS/etc/systemd/system/$1.wants/$2" ] || [ -e "$RFS/etc/systemd/system/$1.wants/$2" ] \
    || [ -L "$RFS/etc/systemd/system/$1.requires/$2" ]
}
installed() { # package -> 0 if dpkg says it is installed
    awk -v p="$1" '
        /^Package: / { pk = $2 }
        /^Status: / && pk == p && $0 ~ / installed$/ { found = 1 }
        END { exit !found }' "$RFS/var/lib/dpkg/status" 2>/dev/null
}

# ── 1. the kernel's settings, as systemd-sysctl will leave them ──────
#
# systemd-sysctl reads every *.conf in these directories, a name in an
# earlier directory hiding the same name in a later one, all in order
# of file NAME; the last assignment of a key wins.
say ""
say "Kernel settings (sysctl), as the machine will have them"
for d in /etc/sysctl.d /run/sysctl.d /usr/local/lib/sysctl.d /usr/lib/sysctl.d; do
    [ -d "$RFS$d" ] || continue
    for f in "$RFS$d"/*.conf; do
        [ -e "$f" ] || continue
        b=$(basename "$f")
        grep -qxF "$b" "$T/seen" 2>/dev/null && continue
        echo "$b" >> "$T/seen"
        printf '%s\t%s\n' "$b" "$d/$b" >> "$T/files"
    done
done
sort "$T/files" 2>/dev/null | while IFS="$(printf '\t')" read -r b p; do
    sed -e 's/#.*//' -e 's/;.*//' "$(inroot "$p")" 2>/dev/null | awk -F= '
        NF >= 2 { k = $1; sub(/^[ \t-]+/, "", k); sub(/[ \t]+$/, "", k); gsub("/", ".", k)
                  v = substr($0, index($0, "=") + 1); sub(/^[ \t]+/, "", v); sub(/[ \t]+$/, "", v)
                  if (k != "") print k "=" v }'
done > "$T/assign"
eff() { awk -F= -v k="$1" '$1 == k { v = substr($0, length(k) + 2) } END { print v }' "$T/assign"; }

while read -r key want why; do
    [ -n "$key" ] || continue
    got=$(eff "$key")
    if [ "$got" = "$want" ]; then ok "$key = $want"
    else bad "$key = $want" "is '${got:-unset}': $why"; fi
done <<'EOF'
kernel.kptr_restrict 2 kernel addresses would be visible
kernel.dmesg_restrict 1 the kernel log would be readable by programs
kernel.unprivileged_bpf_disabled 1 programs could load BPF into the kernel
net.core.bpf_jit_harden 2 the BPF compiler would be unhardened
kernel.yama.ptrace_scope 1 programs could read other programs' memory
kernel.kexec_load_disabled 1 the running kernel could be replaced
fs.protected_symlinks 1 /tmp symlink tricks
fs.protected_hardlinks 1 hardlink tricks
fs.protected_fifos 2 FIFO tricks in shared directories
fs.protected_regular 2 file tricks in shared directories
fs.suid_dumpable 0 privileged programs could dump memory
kernel.sysrq 176 SysRq keys could read memory or kill programs
dev.tty.ldisc_autoload 0 line-discipline modules load on demand
vm.unprivileged_userfaultfd 0 userfaultfd open to programs
kernel.io_uring_disabled 2 io_uring open to programs
net.ipv4.conf.all.accept_redirects 0 ICMP redirects accepted
net.ipv6.conf.all.accept_redirects 0 ICMPv6 redirects accepted
net.ipv4.conf.all.send_redirects 0 the machine could act as a router
net.ipv4.conf.all.accept_source_route 0 source routing accepted
net.ipv4.tcp_syncookies 1 SYN floods
kernel.panic 10 a kernel panic would freeze the screen for ever
kernel.panic_on_oops 0 one driver's bug would take the whole machine down
vm.swappiness 150 zram swap would be used like a slow disk
vm.page-cluster 0 zram would read ahead for nothing
EOF

# What must NOT be turned off: Flatpak's sandbox (bubblewrap) needs
# user namespaces, which AppArmor already restricts to the programs
# allowed them (/etc/apparmor.d/flatpak has `userns,`).
for kv in kernel.unprivileged_userns_clone=0 user.max_user_namespaces=0; do
    k=${kv%=*}
    if [ "$(eff "$k")" = "${kv#*=}" ]; then
        bad "$k is not ${kv#*=} (Flatpak's sandbox needs it)" "it is ${kv#*=}: every Flathub program would fail to start"
    else ok "$k is not ${kv#*=} (Flatpak's sandbox needs it)"; fi
done
if [ -e "$RFS/usr/bin/flatpak" ]; then
    grep -q '^[[:space:]]*userns,' "$RFS/etc/apparmor.d/flatpak" 2>/dev/null \
        && ok "AppArmor lets Flatpak make its sandbox (userns)" \
        || bad "AppArmor lets Flatpak make its sandbox (userns)" "etc/apparmor.d/flatpak has no userns rule"
fi

# ── 2. the kernel command line, on every way AurOS can start ─────────
say ""
say "Kernel command line"
KOPTS="init_on_alloc=1 slab_nomerge randomize_kstack_offset=on vsyscall=none debugfs=off panic=10 fsck.repair=yes lockdown=integrity"
# EVERY kernel line, one at a time, and the entry it belongs to: an
# option dropped from just one entry (a rewrite of the menu, a new
# fallback entry) is named, not averaged away. A kernel line is any
# `linux`/`linuxefi` line but the restore's own (staging.efi).
check_lines() { # label file
    res=$(awk -v kopts="$KOPTS" '
        BEGIN { n = split(kopts, K, " ") }
        /^[[:space:]]*menuentry[[:space:]]/ { id = "?"; if (match($0, /--id[ =][^ {]+/)) id = substr($0, RSTART + 5, RLENGTH - 5) }
        /^[[:space:]]*linux(efi)?[[:space:]]/ && !/staging\.efi/ {
            lines++; l = " " $0 " "; miss = ""
            for (i = 1; i <= n; i++) if (index(l, " " K[i] " ") == 0) miss = miss " " K[i]
            if (index(l, " init_on_free=1 ") > 0) miss = miss " (init_on_free=1 is there: a measured cost for little)"
            if (miss != "") print "entry " id " lacks" miss
        }
        END { if (!lines) print "no kernel line" }' "$2" 2>/dev/null)
    if [ -z "$res" ]; then ok "$1"; else bad "$1" "$res"; fi
}
check_lines "every AurOS entry in grub.cfg.in" "$RFS/usr/lib/auros/grub.cfg.in"
check_lines "the previous-kernel entry auros-bootchain writes" "$RFS/usr/lib/auros/bootchain"
if [ -f "$RFS/boot/grub/grub.cfg" ]; then
    check_lines "every AurOS entry in the written grub.cfg" "$RFS/boot/grub/grub.cfg"
fi

# ── 3. the firewall ──────────────────────────────────────────────────
say ""
say "Firewall"
NFT="$RFS/etc/nftables.conf"
if [ -x "$RFS/usr/sbin/nft" ]; then ok "nftables is installed"; else bad "nftables is installed" "no usr/sbin/nft"; fi
if enabled sysinit.target nftables.service; then ok "nftables.service starts at boot"
else bad "nftables.service starts at boot" "no sysinit.target.wants/nftables.service link"; fi
sed 's/#.*//' "$NFT" 2>/dev/null > "$T/nft"
chain_policy() { # chain -> its policy
    awk -v c="$1" '$1 == "chain" && $2 == c { inside = 1 }
                   inside && /policy/ { s = $0; sub(/.*policy[ \t]+/, "", s); sub(/[ ;].*/, "", s); print s; exit }' "$T/nft"
}
[ "$(chain_policy input)" = drop ] && ok "inbound: everything not answered is dropped" \
    || bad "inbound: everything not answered is dropped" "input chain policy is '$(chain_policy input)'"
[ "$(chain_policy forward)" = drop ] && ok "forwarding: dropped (this is not a router)" \
    || bad "forwarding: dropped (this is not a router)" "forward chain policy is '$(chain_policy forward)'"
grep -q '^flush ruleset' "$T/nft" && ok "the ruleset replaces whatever was there" \
    || bad "the ruleset replaces whatever was there" "no 'flush ruleset'"
# Every accept rule must be one of these. A new one -- an open port --
# is a decision to write down here and in HARDENING.md, not a line to
# slip into the file.
cat > "$T/allowed" <<'EOF'
iif "lo" accept
ct state established,related accept
nd-neighbor-advert, mld-listener-query } accept
udp sport 67 udp dport 68 accept
ip6 saddr fe80::/10 udp sport 547 udp dport 546 accept
udp dport 5353 @th,80,1 1 accept
EOF
extra=$(grep -E '(^|[[:space:]])accept([[:space:]]|;|$)' "$T/nft" | grep -v 'policy accept' \
        | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' | grep -vxF -f "$T/allowed")
[ -z "$extra" ] && ok "nothing is let in but answers, DHCP, IPv6 basics, printer search" \
    || bad "nothing is let in but answers, DHCP, IPv6 basics, printer search" "unexpected: $extra"
grep -q 'ct state established,related accept' "$T/nft" \
    && ok "answers to her own traffic come back" \
    || bad "answers to her own traffic come back" "no established,related accept: nothing would work"
if [ "$(id -u)" = 0 ] && command -v nft >/dev/null 2>&1; then
    e=$(nft -c -f "$NFT" 2>&1) && ok "the ruleset loads (nft -c)" || bad "the ruleset loads (nft -c)" "$e"
fi

# ── 4. printing without a network listener ───────────────────────────
say ""
say "Printing and discovery"
if installed cups-browsed; then bad "cups-browsed is not installed (CVE-2024-47176)" "dpkg says it is"
else ok "cups-browsed is not installed (CVE-2024-47176)"; fi
if enabled multi-user.target cups-browsed.service; then bad "cups-browsed is not started" "multi-user.target.wants/cups-browsed.service"
else ok "cups-browsed is not started"; fi
if installed cups-daemon; then
    l=$(grep -E '^[[:space:]]*(Listen|Port)[[:space:]]' "$RFS/etc/cups/cupsd.conf" | grep -vE 'Listen[[:space:]]+(localhost:631|/run/cups/cups.sock)$')
    [ -z "$l" ] && ok "cups listens on this computer only" || bad "cups listens on this computer only" "$l"
    [ -x "$RFS/usr/lib/cups/backend/usb" ] && ok "cups can still print to USB" \
        || bad "cups can still print to USB" "no usr/lib/cups/backend/usb"
fi
if [ -f "$RFS/etc/avahi/avahi-daemon.conf" ]; then
    pub() { awk -v k="$1" '/^\[/ { s = $0 } s == "[publish]" && $0 ~ "^" k "[[:space:]]*=" { sub(/.*=[[:space:]]*/, ""); v = $0 } END { print v }' \
        "$RFS/etc/avahi/avahi-daemon.conf"; }
    if installed ipp-usb; then
        # A printer on a USB cable: ipp-usb registers it with avahi, and
        # avahi must accept that; but this computer says nothing about
        # itself, and ipp-usb registers on loopback only.
        [ "$(pub disable-publishing)" != yes ] \
            && ok "avahi lets ipp-usb register a USB printer (publishing is not off)" \
            || bad "avahi lets ipp-usb register a USB printer (publishing is not off)" "disable-publishing=yes: a printer on a USB cable would never be found"
        miss=""
        for k in publish-hinfo publish-workstation publish-domain; do
            [ "$(pub $k)" = no ] || miss="$miss $k"
        done
        [ -z "$miss" ] && ok "avahi publishes no hardware, workstation or domain record" \
            || bad "avahi publishes no hardware, workstation or domain record" "not 'no' in [publish]:$miss"
        # (publish-addresses stays on: the printer's record names this
        # computer, and a client that cannot resolve the name finds no
        # printer. The firewall rule below is what keeps it off the network.)
        [ "$(pub publish-addresses)" != no ] \
            && ok "...and the computer's own address record stays (a USB printer's DNS-SD record has to resolve)" \
            || bad "...and the computer's own address record stays (a USB printer's DNS-SD record has to resolve)" "publish-addresses=no: CUPS and ippfind could not resolve the printer's host name"
        IU="$RFS/etc/ipp-usb/ipp-usb.conf"
        if sed -e 's/[#;].*//' "$IU" 2>/dev/null | grep -Eq '^[[:space:]]*interface[[:space:]]*=[[:space:]]*loopback[[:space:]]*$'; then
            ok "ipp-usb serves and registers its printer on loopback only (interface = loopback)"
        else bad "ipp-usb serves and registers its printer on loopback only (interface = loopback)" "etc/ipp-usb/ipp-usb.conf has no 'interface = loopback'"; fi
        if grep -qE '^[[:space:]]*oifname != "lo" udp dport 5353 @th,80,1 1 drop' "$NFT" 2>/dev/null; then
            ok "an mDNS announcement never leaves on a real interface (firewall output rule)"
        else bad "an mDNS announcement never leaves on a real interface (firewall output rule)" "no 'oifname != \"lo\" udp dport 5353 @th,80,1 1 drop' in etc/nftables.conf"; fi
    else
        [ "$(pub disable-publishing)" = yes ] && ok "avahi announces nothing about this computer" \
            || bad "avahi announces nothing about this computer" "[publish] disable-publishing is '$(pub disable-publishing)'"
    fi
fi

# ── 5. kernel modules an ordinary program could load ─────────────────
say ""
say "Kernel modules that stay unloaded"
MP="$RFS/etc/modprobe.d/auros-hardening.conf"
miss=""
for m in dccp sctp rds tipc n_hdlc ax25 netrom x25 rose decnet econet af_802154 ipx \
         appletalk psnap p8023 p8022 can can-bcm can-raw can-gw can-isotp can-j1939 atm \
         ksmbd vivid cramfs freevxfs jffs2 hfs firewire-core firewire-ohci firewire-sbp2; do
    grep -qx "install $m /bin/false" "$MP" 2>/dev/null || miss="$miss $m"
done
[ -z "$miss" ] && ok "32 rarely used, often exploited modules cannot load" \
    || bad "32 rarely used, often exploited modules cannot load" "loadable:$miss"

# ── 6. memory, logs, recovery ────────────────────────────────────────
say ""
say "Memory, logs and recovery"
J="$RFS/etc/systemd/journald.conf.d/50-auros.conf"
mx=$(sed -n 's/^SystemMaxUse=\([0-9]*\)M$/\1/p' "$J" 2>/dev/null)
[ -n "$mx" ] && [ "$mx" -le 500 ] && ok "the system log is capped (${mx} MB)" \
    || bad "the system log is capped" "SystemMaxUse in ${J#"$RFS"/} is '${mx:-unset}'"
grep -q '^SystemKeepFree=' "$J" 2>/dev/null && ok "the log stops before the disk is full" \
    || bad "the log stops before the disk is full" "no SystemKeepFree="
if enabled multi-user.target zramswap.service && grep -q '^PERCENT=[1-9]' "$RFS/etc/default/zramswap" 2>/dev/null; then
    ok "compressed swap in RAM (zram, $(sed -n 's/^PERCENT=//p' "$RFS/etc/default/zramswap")%)"
else bad "compressed swap in RAM (zram)" "zramswap.service not enabled or etc/default/zramswap has no PERCENT"; fi
if enabled multi-user.target earlyoom.service &&
   grep -q -- "--avoid '[^']*aurshell" "$RFS/etc/default/earlyoom" 2>/dev/null; then
    ok "earlyoom stops a runaway program, never the desktop"
else bad "earlyoom stops a runaway program, never the desktop" "earlyoom.service not enabled, or no --avoid aurshell"; fi

# The desktop's safe mode.
U="$RFS/etc/systemd/system/aurshell.service"
DI="$RFS/etc/systemd/system/aurshell.service.d/50-auros-safe-mode.conf"
grep -q '^ExecStart=.*/usr/bin/aurshell \$AURSHELL_SAFE$' "$U" 2>/dev/null \
    && ok "the desktop's command line takes the safe-mode words" \
    || bad "the desktop's command line takes the safe-mode words" "aurshell.service ExecStart has no \$AURSHELL_SAFE"
if grep -qx 'EnvironmentFile=-/run/auros-guard/aurshell.env' "$DI" 2>/dev/null &&
   grep -qx 'ExecStartPre=+/usr/lib/auros/shell-guard start' "$DI" &&
   grep -qx 'ExecStopPost=+/usr/lib/auros/shell-guard stopped' "$DI"; then
    ok "the safe-mode guard runs around every desktop start"
else bad "the safe-mode guard runs around every desktop start" "${DI#"$RFS"/} is missing or incomplete"; fi
G="$RFS/usr/lib/auros/shell-guard"
if [ -x "$G" ]; then
    gd="$T/guard"; mkdir -p "$gd"; printf 'kiosk_mode = no\n' > "$T/policy"
    guard() { AUROS_GUARD_DIR="$gd" AUROS_GUARD_POLICY="$T/policy" AUROS_GUARD_NOW="$1" sh "$G" "$2" 2>/dev/null; }
    envw() { sed -n 's/^AURSHELL_SAFE=//p' "$gd/aurshell.env" 2>/dev/null; }
    guard 100 start
    [ -z "$(envw)" ] && ok "a normal start is a normal start" || bad "a normal start is a normal start" "env: $(envw)"
    SERVICE_RESULT=success guard 110 stopped
    SERVICE_RESULT=success guard 120 stopped
    SERVICE_RESULT=success guard 130 stopped
    guard 140 start
    [ -z "$(envw)" ] && ok "stopping it on purpose is not a crash" || bad "stopping it on purpose is not a crash" "env: $(envw)"
    SERVICE_RESULT=signal EXIT_CODE=dumped EXIT_STATUS=SEGV guard 150 stopped
    SERVICE_RESULT=signal guard 200 stopped
    guard 210 start
    [ -z "$(envw)" ] && ok "two crashes: still her own desktop" || bad "two crashes: still her own desktop" "env: $(envw)"
    SERVICE_RESULT=exit-code guard 260 stopped
    guard 270 start
    [ "$(envw)" = "--shell /usr/share/auros/shells/rail.shell --conf /etc/auros/shell.conf" ] \
        && ok "three crashes in ten minutes: the plain home, the machine's settings" \
        || bad "three crashes in ten minutes: the plain home, the machine's settings" "env: '$(envw)'"
    [ -f "$RFS/usr/share/auros/shells/rail.shell" ] && ok "...and that home is in the image" \
        || bad "...and that home is in the image" "no usr/share/auros/shells/rail.shell"
    for t in 300 340 380; do SERVICE_RESULT=signal guard $t stopped; done
    guard 390 start
    case "$(envw)" in "--shell /usr/share/auros/shells/rail.shell --conf "*)
        [ "$(envw)" != "--shell /usr/share/auros/shells/rail.shell --conf /etc/auros/shell.conf" ] \
            && ok "three more: the built-in look as well" || bad "three more: the built-in look as well" "env: $(envw)" ;;
        *) bad "three more: the built-in look as well" "env: '$(envw)'" ;; esac
    rm -rf "$gd"; mkdir -p "$gd"
    for t in 10 20 30; do SERVICE_RESULT=signal guard $t stopped; done
    guard 900 start
    [ -z "$(envw)" ] && ok "crashes long ago do not count" || bad "crashes long ago do not count" "env: $(envw)"
    rm -rf "$gd"; mkdir -p "$gd"; printf 'kiosk_mode = yes\n' > "$T/policy"
    for t in 10 20 30; do SERVICE_RESULT=signal guard $t stopped; done
    guard 40 start
    case "$(envw)" in *locked.shell*) ok "a kiosk's safe mode is still locked" ;;
        *) bad "a kiosk's safe mode is still locked" "env: '$(envw)'" ;; esac
else
    bad "the safe-mode guard is in the image" "no executable usr/lib/auros/shell-guard"
fi

# dpkg repaired at boot.
if enabled multi-user.target auros-dpkg-repair.service; then ok "an interrupted update is finished at the next start"
else bad "an interrupted update is finished at the next start" "auros-dpkg-repair.service not enabled"; fi
DR="$RFS/usr/lib/auros/dpkg-repair"
if [ -x "$DR" ]; then
    mkdir -p "$T/bin" "$T/adm/updates"
    printf '#!/bin/sh\necho "$0 $*" >> "%s/calls"\n' "$T" > "$T/bin/dpkg"
    cp "$T/bin/dpkg" "$T/bin/apt-get"; chmod 755 "$T/bin/dpkg" "$T/bin/apt-get"
    printf 'Package: less\nStatus: install ok installed\n\nPackage: nano\nStatus: install ok installed\n' > "$T/adm/status"
    rep() { : > "$T/calls"; PATH="$T/bin:$PATH" DPKG_ADMINDIR="$T/adm" sh "$DR" >/dev/null 2>&1; }
    rep
    [ ! -s "$T/calls" ] && ok "...and a clean machine costs nothing (dpkg not run)" \
        || bad "...and a clean machine costs nothing (dpkg not run)" "$(cat "$T/calls")"
    printf 'Package: less\nStatus: install ok half-configured\n' > "$T/adm/status"
    rep
    grep -q 'dpkg --force-confdef --force-confold --configure -a' "$T/calls" \
        && ok "...a half-configured package is configured" \
        || bad "...a half-configured package is configured" "calls: $(cat "$T/calls")"
    printf 'Package: less\nStatus: install ok installed\n' > "$T/adm/status"; : > "$T/adm/updates/0001"
    rep
    grep -q -- '--configure -a' "$T/calls" && ok "...and so is an update dpkg had only begun" \
        || bad "...and so is an update dpkg had only begun" "calls: $(cat "$T/calls")"
else
    bad "the dpkg repair is in the image" "no executable usr/lib/auros/dpkg-repair"
fi

# ── 7. the clock ─────────────────────────────────────────────────────
#
# Windows keeps the PC's hardware clock in local time. Without a time
# daemon, and with the clock read as UTC, a machine in the Americas was
# hours behind and apt refused every update ("not valid yet"). And a time
# daemon that lets the kernel write the hardware clock back would leave
# Windows' clock hours wrong after she went back to it.
say ""
say "The clock"
CH="$RFS/etc/chrony/chrony.conf"
if installed chrony; then ok "chrony is installed (the time daemon)"; else bad "chrony is installed (the time daemon)" "dpkg says it is not"; fi
if installed systemd-timesyncd; then bad "there is no second time daemon" "systemd-timesyncd is installed beside chrony"
else ok "there is no second time daemon"; fi
if enabled multi-user.target chrony.service; then ok "chrony starts at boot"
else bad "chrony starts at boot" "no multi-user.target.wants/chrony.service link"; fi
conf() { sed -e 's/#.*//' "$CH" 2>/dev/null; }
if conf | grep -Eq '^[[:space:]]*rtcsync([[:space:]]|$)'; then
    bad "chrony never writes the PC's hardware clock (no rtcsync)" "etc/chrony/chrony.conf has rtcsync: Windows' clock would be set to UTC every 11 minutes"
else ok "chrony never writes the PC's hardware clock (no rtcsync)"; fi
if conf | grep -Eq '^[[:space:]]*makestep[[:space:]]+[0-9.]+[[:space:]]+-1[[:space:]]*$'; then ok "a clock that is hours out is stepped, whenever it is found"
else bad "a clock that is hours out is stepped, whenever it is found" "no 'makestep <s> -1' in etc/chrony/chrony.conf (the default steps only in the first three updates)"; fi
if conf | grep -Eq '^[[:space:]]*port[[:space:]]+0[[:space:]]*$' && conf | grep -Eq '^[[:space:]]*cmdport[[:space:]]+0[[:space:]]*$'; then
    ok "chrony asks for the time and answers nobody (no listening port)"
else bad "chrony asks for the time and answers nobody (no listening port)" "port 0 / cmdport 0 missing in etc/chrony/chrony.conf"; fi
if sed -e 's/#.*//' "$RFS"/etc/chrony/sources.d/*.sources 2>/dev/null | grep -Eq '^[[:space:]]*server[[:space:]].*[[:space:]]nts([[:space:]]|$)'; then
    ok "time over NTS (authenticated) is asked for first"
else bad "time over NTS (authenticated) is asked for first" "no 'server ... nts' line under etc/chrony/sources.d"; fi
if conf | grep -Eq '^[[:space:]]*pool[[:space:]]'; then ok "...and ordinary pool servers are the way in on a network that blocks NTS"
else bad "...and ordinary pool servers are the way in on a network that blocks NTS" "no pool line in etc/chrony/chrony.conf"; fi
[ -x "$RFS/etc/NetworkManager/dispatcher.d/30-auros-time" ] && ok "a network coming up makes chrony look for its servers now" \
    || bad "a network coming up makes chrony look for its servers now" "no executable etc/NetworkManager/dispatcher.d/30-auros-time"
# What the installer's answers do to the clock: the shipped choices.sh,
# run against a scratch root.
CS="$RFS/usr/lib/auros/choices.sh"
if [ -f "$CS" ]; then
    cr="$T/croot"; mkdir -p "$cr/usr/share/zoneinfo/America"; : > "$cr/usr/share/zoneinfo/America/Los_Angeles"
    printf 'timezone=iana:America/Los_Angeles\n' > "$T/c1.conf"
    AUROS_CHOICES_ROOT="$cr" AUROS_CHOICES_FILE="$T/c1.conf" sh "$CS" >/dev/null 2>&1
    if [ "$(sed -n 3p "$cr/etc/adjtime" 2>/dev/null)" = LOCAL ]; then ok "an install beside Windows reads the PC's clock as local time (adjtime LOCAL)"
    else bad "an install beside Windows reads the PC's clock as local time (adjtime LOCAL)" "choices.sh with a time zone left no 'LOCAL' in etc/adjtime"; fi
    rm -rf "$cr"; mkdir -p "$cr/usr/share/zoneinfo/America"; : > "$cr/usr/share/zoneinfo/America/Los_Angeles"
    printf 'timezone=iana:America/Los_Angeles\nrtc=utc\n' > "$T/c2.conf"
    AUROS_CHOICES_ROOT="$cr" AUROS_CHOICES_FILE="$T/c2.conf" sh "$CS" >/dev/null 2>&1
    if [ ! -e "$cr/etc/adjtime" ]; then ok "...unless Windows keeps its clock in UTC (rtc=utc)"
    else bad "...unless Windows keeps its clock in UTC (rtc=utc)" "etc/adjtime was written"; fi
    rm -rf "$cr"; mkdir -p "$cr"
    printf 'theme=moss\n' > "$T/c3.conf"
    AUROS_CHOICES_ROOT="$cr" AUROS_CHOICES_FILE="$T/c3.conf" sh "$CS" >/dev/null 2>&1
    if [ ! -e "$cr/etc/adjtime" ]; then ok "...and no time zone chosen, no change to the clock"
    else bad "...and no time zone chosen, no change to the clock" "etc/adjtime was written without a zone"; fi
    rm -rf "$cr"
else bad "choices.sh is in the image" "no usr/lib/auros/choices.sh"; fi

# ── every computer is its own computer ─────────────────────────────
#
# One machine id for every AurOS PC is visible to networks (the DHCP client
# id) and seeds other per-machine names. systemd makes a new one at a first
# start when the file is empty -- and a first start also asks questions on
# the console and applies preset policy, so those are held still.
say ""
say "Machine identity"
if [ -e "$RFS/etc/machine-id" ] && [ ! -s "$RFS/etc/machine-id" ]; then ok "/etc/machine-id is empty: each computer makes its own at its first start"
else bad "/etc/machine-id is empty: each computer makes its own at its first start" \
        "etc/machine-id has the build's id in it (or is missing): every PC would share one"; fi
if [ "$(readlink "$RFS/var/lib/dbus/machine-id" 2>/dev/null)" = /etc/machine-id ]; then ok "/var/lib/dbus/machine-id is the same file"
else bad "/var/lib/dbus/machine-id is the same file" "var/lib/dbus/machine-id is not a link to /etc/machine-id: D-Bus would keep another id"; fi
if [ "$(readlink "$RFS/etc/systemd/system/systemd-firstboot.service" 2>/dev/null)" = /dev/null ]; then ok "the first start asks nothing on the console (systemd-firstboot masked)"
else bad "the first start asks nothing on the console (systemd-firstboot masked)" "systemd-firstboot.service is not masked: an empty machine-id makes it prompt"; fi
PRE="$RFS/etc/systemd/system-preset/00-auros.preset"
if [ -f "$PRE" ] && [ "$(grep -v '^[[:space:]]*#' "$PRE" | grep -v '^[[:space:]]*$' | tail -1)" = "disable *" ]; then
    ok "the first start changes no unit (the preset is what the build enabled, then disable *)"
else bad "the first start changes no unit (the preset is what the build enabled, then disable *)" \
        "no etc/systemd/system-preset/00-auros.preset ending in 'disable *': Ubuntu's preset would enable systemd-networkd and more"; fi
if [ -f "$PRE" ]; then
    for u in aurshell.service auros-firstboot.service; do
        if grep -qx "enable $u" "$PRE"; then ok "...and $u is still enabled by it"
        else bad "...and $u is still enabled by it" "$PRE has no 'enable $u'"; fi
    done
fi

# ── names that are not ours to wear ───────────────────────────────────
#
# The image does not say it is Ubuntu, and does not carry Ubuntu's icon
# artwork (docs/legal/COMPLIANCE.md TM-1, LIC-5; tools/legal/check.py image
# reads the same). base-files writes Ubuntu's os-release and lsb-release over
# ours on every update unless both are diverted; ubuntu-mono cannot simply be
# purged because GTK's adwaita-icon-theme depends on it, so a package with
# no files answers that dependency instead. tools/unbrandtest.sh proves the
# build's code; this reads the image it made.
say ""
say "Names that are not ours (Ubuntu's)"
OSR=$(inroot /usr/lib/os-release); LSB=$(inroot /etc/lsb-release)
if [ ! -f "$OSR" ]; then bad "os-release does not name the image Ubuntu" "no usr/lib/os-release"
elif grep -E '^(NAME|PRETTY_NAME|ID)=' "$OSR" | grep -qi ubuntu; then bad "os-release does not name the image Ubuntu" "NAME, PRETTY_NAME or ID says Ubuntu"
else ok "os-release does not name the image Ubuntu"; fi
if grep -q '^DISTRIB_ID=' "$LSB" 2>/dev/null && ! grep -qi '^DISTRIB_ID=.*ubuntu' "$LSB"; then ok "lsb-release does not name the image Ubuntu (DISTRIB_ID)"
else bad "lsb-release does not name the image Ubuntu (DISTRIB_ID)" "etc/lsb-release is Ubuntu's (or missing)"; fi
for f in /usr/lib/os-release /etc/lsb-release; do
    if grep -qx "$f" "$RFS/var/lib/dpkg/diversions" 2>/dev/null; then ok "$f is diverted: no update of base-files writes Ubuntu's over ours"
    else bad "$f is diverted: no update of base-files writes Ubuntu's over ours" "var/lib/dpkg/diversions does not list it"; fi
done
for p in ubuntu-mono humanity-icon-theme; do
    if installed $p || ls "$RFS"/usr/share/icons 2>/dev/null | grep -qi "${p%-icon-theme}"; then
        bad "$p (Ubuntu's icon artwork) is not in the image" "dpkg lists it, or its icons are on the disk"
    else ok "$p (Ubuntu's icon artwork) is not in the image"; fi
done
if installed adwaita-icon-theme; then
    if awk 'BEGIN { RS = ""; FS = "\n" } /Status: install ok installed/ { for (i = 1; i <= NF; i++) if ($i ~ /^Provides:.*[ ,]ubuntu-mono([ ,(]|$)/) f = 1 } END { exit !f }' "$RFS/var/lib/dpkg/status"; then
        ok "adwaita's dependency on ubuntu-mono is answered by a package of ours (apt stays consistent)"
    else bad "adwaita's dependency on ubuntu-mono is answered by a package of ours (apt stays consistent)" \
            "no installed package Provides ubuntu-mono: apt would call the image broken, or take GTK's icons out"; fi
    if grep -qs 'Package: ubuntu-mono' "$RFS/etc/apt/preferences.d/auros-no-ubuntu-icons" && grep -qs 'Pin-Priority: -1' "$RFS/etc/apt/preferences.d/auros-no-ubuntu-icons"; then
        ok "apt is told never to fetch Ubuntu's icon theme back"
    else bad "apt is told never to fetch Ubuntu's icon theme back" "no etc/apt/preferences.d/auros-no-ubuntu-icons pinning ubuntu-mono to -1"; fi
else bad "GTK's icons: adwaita-icon-theme is installed" "it is not: every GTK program would have no icons"; fi

# ── 8. no login on any console ───────────────────────────────────────
#
# A login prompt is a question with no answer here (nobody has a password
# to type), on a black screen reached by accident. The serial console
# stays for the kernel log and the tests; a login on it never.
say ""
say "Console logins"
masked() { [ "$(readlink "$RFS/etc/systemd/system/$1" 2>/dev/null)" = /dev/null ]; }
for u in serial-getty@.service console-getty.service; do
    if masked "$u"; then ok "$u is masked (no login on the serial console)"
    else bad "$u is masked (no login on the serial console)" "etc/systemd/system/$u is not a link to /dev/null: the kernel's console=ttyS0 would start a login prompt"; fi
done
if grep -qsE '^allow_tty[[:space:]]*=[[:space:]]*no' "$RFS/etc/auros/policy.conf"; then
    miss=""
    for n in 1 2 3 4 5 6; do masked "getty@tty$n.service" || masked "getty@.service" || miss="$miss tty$n"; done
    [ -z "$miss" ] && ok "no login prompt on the virtual consoles (allow_tty = no)" \
        || bad "no login prompt on the virtual consoles (allow_tty = no)" "getty is not masked on:$miss"
    grep -qsE '^NAutoVTs=0' "$RFS"/etc/systemd/logind.conf.d/*.conf \
        && ok "...and Ctrl-Alt-F2 starts none (NAutoVTs=0)" \
        || bad "...and Ctrl-Alt-F2 starts none (NAutoVTs=0)" "no NAutoVTs=0 under etc/systemd/logind.conf.d"
fi

say ""
if [ "$fail" -eq 0 ]; then
    echo "hardentest: $checked checks, all ok"
    exit 0
fi
echo "hardentest: $fail of $checked checks FAILED"
exit 1
