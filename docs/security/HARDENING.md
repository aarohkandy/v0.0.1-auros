# Hardening and self-repair of the AurOS image (stream S1)

The owner's bar: *"never crashing, not like Windows' blue screens; not
the hackable OS; lightweight."* For a 2013 laptop with 4 GB of RAM.

This is what a stock AurOS image does about it, what each control costs,
and how we know it is there. Every control has a test that **fails when
the control is removed**; that is proven, not assumed (below).

## What is in the image

| # | Control | Where | Why |
|---|---|---|---|
| N1 | **Firewall: nothing on the network can start a conversation.** nftables, inbound policy drop; let in only answers to her own traffic, DHCP/DHCPv6, IPv6 neighbour basics, and mDNS *answers* (printer search). Forwarding dropped. | `rootfs/etc/nftables.conf`, enabled in `sysinit.target` by `build/harden` | Café Wi-Fi and hostile routers. Whatever a future package starts listening on, nobody outside can reach it. |
| N2 | **cups-browsed is not in the image.** cups (USB and network printing, localhost only) and avahi printer search stay. | `profiles/desktop.profile`; `build/harden` purges it whichever profile asks | It was the UDP 631 listener of the CVE-2024-47176 chain (a stranger's packet adds a printer whose driver runs a command). Printing does not need it. |
| N3 | **avahi announces nothing** (`disable-publishing=yes`); it still finds printers. | `build/harden` edits `/etc/avahi/avahi-daemon.conf` | The computer does not tell the café who it is. |
| K1 | **Kernel settings** (sysctl): `kptr_restrict=2`, `dmesg_restrict=1`, `unprivileged_bpf_disabled=1`, `bpf_jit_harden=2`, `ptrace_scope=1`, `kexec_load_disabled=1`, `protected_symlinks/hardlinks=1`, `protected_fifos/regular=2`, `suid_dumpable=0`, `sysrq=176`, `ldisc_autoload=0`, `unprivileged_userfaultfd=0`, `io_uring_disabled=2`, no ICMP redirects / source routing, SYN cookies. | `rootfs/etc/sysctl.d/60-auros-hardening.conf`, `99-protect-links.conf` (that name, to win over systemd's own file of the same name) | Each closes a door local root exploits have used (BPF, io_uring, userfaultfd, kernel address leaks, /tmp link tricks). |
| K2 | **Rare, often-exploited kernel modules cannot load** (dccp, sctp, rds, tipc, n_hdlc, ax25 family, can family, atm, ksmbd, vivid, cramfs, freevxfs, jffs2, hfs, firewire): `install X /bin/false`. | `rootfs/etc/modprobe.d/auros-hardening.conf` | An ordinary program's `socket()` would otherwise load them (CVE-2017-6074 dccp class). |
| K3 | **Kernel command line**: `init_on_alloc=1 slab_nomerge randomize_kstack_offset=on vsyscall=none debugfs=off panic=10 fsck.repair=yes lockdown=integrity`. Not `init_on_free=1` (several per cent on every free, for little over init_on_alloc). | every AurOS line in `rootfs/usr/lib/auros/grub.cfg.in` and the previous-kernel line in `bootchain` (files of stream BT; S1 owns the options) | Memory handed out is zeroed; kernel objects of different kinds never share a slab; no fixed-address page; no debug filesystem. `lockdown=integrity`: nothing running as root can change the running kernel (unsigned modules, `/dev/mem`, kexec, MSR writes, hibernation images). Secure Boot already turns lockdown on; the option keeps it on for a PC with Secure Boot switched off. |
| K4 | **Flatpak keeps working**: user namespaces are NOT switched off; Ubuntu's AppArmor rule lets only Flatpak's sandbox make them. | `/etc/apparmor.d/flatpak` (Ubuntu's) | `user.max_user_namespaces=0` would break every Flathub program. Checked, both ways. |
| R1 | **A kernel panic restarts the computer after 10 s** instead of freezing (`kernel.panic=10`, `panic=10` on the cmdline); one driver's oops does not take the machine down (`panic_on_oops=0`). | `61-auros-reliability.conf`, K3 | The blue-screen case. (A panic that repeats at every start is stream BT's boot counting.) |
| R2 | **zram**: compressed swap in RAM, 50 %, zstd; `swappiness=150`, `page-cluster=0`. | `profiles/desktop.profile` (`zram_percent`), `zram-tools` | 4 GB behaves like about 6. |
| R3 | **earlyoom**: below 5 % free memory and 10 % free swap, the biggest program gets SIGTERM, while the machine still answers. Never `aurshell`, `systemd`, Xwayland; Firefox/Chrome/LibreOffice first. | `profiles/desktop.profile` (`earlyoom_min_free_percent`), `/etc/default/earlyoom` | Without it, the kernel's own killer acts only at the very end, after the machine has stopped answering. Not systemd-oomd: aurshell's apps share its cgroup tree, so oomd would kill the whole desktop. |
| R4 | **The desktop's safe mode**: 3 failures in 10 minutes and the next start is the plain home with the machine's own settings (then also the built-in look). Her own settings are not touched; restarting the computer tries them again. | `rootfs/usr/lib/auros/shell-guard`, `aurshell.service.d/50-auros-safe-mode.conf` | A crash caused by something she chose must not loop for ever. |
| R5 | **An update cut off by a power cut is finished at the next start** (`dpkg --configure -a`, keeping her config files); a clean machine costs one directory listing. | `rootfs/usr/lib/auros/dpkg-repair`, `auros-dpkg-repair.service` | Otherwise every later update refuses with a message she will never read, and security fixes silently stop. |
| R6 | **The journal is capped**: 200 MB, always 1 GB of the disk left free, 48 MB in RAM, 3 months. | `rootfs/etc/systemd/journald.conf.d/50-auros.conf` | A noisy driver cannot fill her disk. |
| R8 | **The clock is right** (finding SEC-007). Windows keeps the PC's hardware clock in local time; this system read it as UTC and had no time daemon, so every machine west of Greenwich started 4-12 hours behind, apt refused every Release file ("not valid yet"), and unattended-upgrades never installed a fix. Now: **chrony** (no `rtcsync`, `makestep 1 -1`, NTS servers first, no listening port) and, for an install made by the installer, `/etc/adjtime` LOCAL plus a one-time correction from the hardware clock at first start. | `profiles/desktop.profile` (`chrony`), `rootfs/etc/chrony/`, `rootfs/etc/NetworkManager/dispatcher.d/30-auros-time`, `rootfs/usr/lib/auros/choices.sh` (`clock_local`), `build/harden` | See "The clock" below. |
| R7 | **A damaged disk is repaired at start** without asking (`fsck.repair=yes`). | K3 | Instead of stopping at "requires a manual fsck". |

Not S1's, and listed so nobody thinks they are covered here: the GRUB
menu's edit/command-line/recovery-shell lock and boot counting (stream
BT), the shared account password (atlas risk 5), Firefox and scams (S2),
disk encryption (S4), the update channel (S5).

## The clock

Why chrony and not systemd-timesyncd, which is smaller and already part of
systemd: timesyncd tells the kernel the clock is synchronised, and the
kernel then writes it into the PC's hardware clock every 11 minutes, **in
UTC**. On a PC that also runs Windows that is Windows' clock, kept in
local time: after her first minutes in AurOS, Windows would show the wrong
time (hours out) when she went back to it, until it next synchronised. AurOS
must leave Windows as it found it. chrony without `rtcsync` never lets the
kernel do that; it needs no other configuration to step a wrong clock
(`makestep 1 -1`) and it can ask for **authenticated** time (NTS), which a
stranger on the same café Wi-Fi cannot move. Cost: a few MB of disk, about
2 MB of RAM, nothing listening (`port 0`, `cmdport 0`).

What the installer's answers do (`choices.sh`, first start, only on an
install made by AurBridge): `/etc/adjtime` says LOCAL, so from the next
start systemd applies the zone's offset (the clock is right before any
network); and right now, once, if the clock still shows the hardware's value
(hours out, never years: a dead battery is not "a zone"), the time is set from
the hardware clock read as local time. `rtc=utc` in `choices.conf` (for a
Windows that was told to keep UTC; AurBridge does not send it yet) leaves it
alone. A PC without Windows, or an install without answers, is untouched:
the hardware clock is UTC there and chrony only has the network to correct.

`tools/timeboottest.sh` boots the image in a VM whose RTC is in
America/Los_Angeles local time, online, three ways (the fix taken out; chrony
alone; the image as it is) and checks the clock against the host's, `apt-get
update`, and that the RTC is still in local time after the sync, and after a
restart. Measured on Actions (KVM, the image of build `4c16e98`, the clock
in Los Angeles local time = 25200 s behind UTC, online):

| | fix removed | chrony alone | the image as it is |
|---|---|---|---|
| clock at the first start, before any network | 25202 s behind | 25202 s behind | **1 s off** (first-boot setup) |
| clock once the network is up | still 25202 s behind | **1 s off**, stepped by an NTS server (time.cloudflare.com) 10 s after the network | 1 s off |
| `apt-get update` | refuses every Release file ("not valid yet", exit 100) | accepted | accepted |
| the PC's hardware clock afterwards (Windows') | untouched | **still local time** (reads 25201 s behind UTC: not rewritten) | still local time, after a restart too |
| clock after a restart, before any network | - | - | **1 s off** (systemd applies `/etc/adjtime` LOCAL) |
| listening sockets of the time daemon | - | none | none |

`tools/timeboottest.sh`, 14 checks, all ok; the first two columns are the
control (the same checks fail without the fix).

## How it is proven

Three tests, all in the repository.

1. **`tools/hardentest.sh`** (static, ~1 s) reads a built rootfs: the
   effective sysctl value after every `sysctl.d` file in systemd's order,
   every kernel line in the boot menu, the firewall's accept rules
   against an allow-list, the units actually enabled, the packages
   actually installed, and it *runs* the shipped `shell-guard` and
   `dpkg-repair` against scratch state. `build/harden` runs it at the
   seal and **the build stops on any FAIL**: an image without these
   controls cannot be made.
2. **`tools/hardentest-mutants.sh`** takes the controls away one at a
   time (50 mutations: a sysctl line, a cmdline option, the drop policy,
   an extra open port, cups-browsed back, the safe-mode drop-in, the
   guard's thresholds, the dpkg repair's detection, ...) and requires
   hardentest to fail on the check that names that control.
3. **`tools/hardenboottest.sh`** boots the published image itself
   (`out/auros-desktop.img`, byte for byte what the installer writes)
   under OVMF with Microsoft's keys and **Secure Boot on**, in a
   **4 GB, 2-core** KVM machine, and asks the running system. Then it
   boots the same image **with every S1 control taken out** and requires
   every check tied to a control to FAIL there. It also presses the keys
   a person at the keyboard would press at the GRUB menu (`e`, `c`, the
   recovery entry). Workflow `.github/workflows/harden.yml` runs it on
   every build whose image changed a hardening input (or `[harden]` in a
   commit subject); the report is the job summary.

### What the booted machine showed

`tools/hardenboottest.sh` on Actions (`harden.yml`, KVM, Secure Boot on, 4 GB
and 2 cores), image of build `4c16e98` with BT's menu lock. "Removed" = the
same image with every control switched off. Every control-tied row passes on
the hardened image and **fails** with the control removed:

| control | hardened | controls removed |
|---|---|---|
| 24 sysctl settings (kptr_restrict, BPF, io_uring, ...) | in force | 13 fail; 11 are Ubuntu's own default (the static test alone sees those go) |
| 8 kernel options (init_on_alloc, slab_nomerge, ..., lockdown=integrity) | all on `/proc/cmdline` | all missing |
| 32 kernel modules cannot load: `socket(AF_INET, SOCK_DCCP)` | refused, module not loaded | allowed |
| firewall: a stranger's TCP connection / UDP 631 packet | reach nothing, though a listener is waiting | reach it (a banner comes back) |
| no listening socket but avahi's and DHCP's | yes | (nothing to compare) |
| avahi announces nothing | `auros.local` does not answer | answers |
| journal cap 200 MB | 62 MB used | 377 MB and growing |
| a half-configured package after a power cut | finished at the next start | stays broken |
| a memory hog (`tail /dev/zero`) | zram takes 1.7 GB, **earlyoom stops it in 8 s**, the desktop is the same process | the kernel's OOM killer fires after ~2 minutes and the machine does not answer for over 5 |
| 3 crashes of the desktop | the 4th start is the safe mode (plain home, machine's settings) | a start like any other |
| Flatpak's sandbox (bubblewrap, userns) | still starts | (same) |
| her own traffic (DHCP, a page from outside) | works | works |
| GRUB: `e`, `c` | ask for a username and password | open (before BT's lock) |
| tools/hardentest.sh on the booted disk; its 51 mutations | passes; every mutation caught | fails |
| S2: AnyDesk from the store | refused under the booted polkitd; the card appears; her keyboard "yes" lets the next try through | goes straight through |

Not yet green, and not S1's: see the last section.

### The cost (measured)

Same runs, 3 starts of each disk in turn, on a shared runner:

| | controls off | AurOS | |
|---|---|---|---|
| boot finished (best of 3) | 5050 ms | 4926 ms | no difference (host noise is larger) |
| desktop started (best of 3) | 4778 ms | 4893 ms | +0.1 s |
| CPU the guest used until then (best of 3, 2 cores) | 7180 ms | 7260 ms | +1% |
| RAM in use 60 s after the boot (median of 3) | 384 MB | 391 MB | +7 MB |

The services the controls add: `nftables` and `zramswap` (one-shot, about
100 ms of service time each, off the critical path) and `earlyoom` (about 1 MB).
Over other runs the idle RAM difference ranged +7 to +20 MB, the CPU +1% to
+3%. `blame.txt` and `meminfo.txt` per start are in the evidence artifact.

### Open findings from the booted image (for the owners named)

- **S2: PackageKit installs are not gated.** `pkcheck --action-id
  org.freedesktop.packagekit.package-install ... package_ids teamviewer;...`
  returns **0 (authorized)** on the booted image although the gate's signal is
  sent; the same question about a Flatpak is refused. Evidence: `polkit.txt` in
  the `hardenboot` artifact (every rule polkit read; the action's defaults).
- **aurwl: a second Firefox window can die of a Wayland protocol error**
  ("xdg_surface: no configure with serial N was sent") after the first was
  closed by "Close this page"; seen once on Actions, not on the next run.
- **Firefox shows its first-run dialog** ("By continuing you agree to the
  Terms of Use ... sends diagnostic and interaction data to Mozilla") over the
  first page although telemetry is off by policy; and its crash reporter tries
  to reach `incoming.telemetry.mozilla.org` when Firefox is killed.
- **Login prompts**: `serial-getty@ttyS0` runs (`console=ttyS0` is on every
  kernel line); the virtual consoles are off in this image.

### What is not proven

- A real PC. The VM has virtio disks and QEMU's network; the timings are
  on a fast host CPU, so read them as *before vs after*, not as what a
  2013 laptop takes.
- The "before" image is this image with the S1 controls switched off,
  not an older build: cups-browsed is absent from both, so its own RAM
  is not in "before" (the real saving is a little larger than shown).
- Printing to a real Wi-Fi printer through the firewall (mDNS answers +
  IPP) has not been tried on hardware. USB printing does not touch the
  network.
- Several sysctls were already Ubuntu's default (`dmesg_restrict`,
  `ptrace_scope`, `protected_symlinks/hardlinks/regular`, `sysrq`,
  `syncookies`, `panic_on_oops`): our lines pin them, and only the
  static test can see one go, because removing it changes nothing at
  run time. The report marks them "Ubuntu's default is the same".
- Kernel lockdown at boot is shown with Secure Boot on, where the
  firmware path already enforces it; `lockdown=integrity` for a PC with
  Secure Boot off is checked on the command line, not booted that way.

## Running it

```sh
RFS=work/forge/desktop/rootfs sh tools/hardentest.sh           # static, any Linux
RFS=work/forge/desktop/rootfs sh tools/hardentest-mutants.sh   # every check can fail
sudo sh tools/hardenboottest.sh out/auros-desktop.img          # KVM, qemu, ovmf; ~25 min
```

On a machine without a built tree: download a build's `auros-pieces`
artifact, `cat auros-desktop.img.gz.* | gzip -dc > out/auros-desktop.img`,
and check it against `image.sha256`. Without root on the host, run it in
`docker run --privileged --device /dev/kvm` (ubuntu:24.04 with
qemu-system-x86, ovmf, gdisk, e2fsprogs, python3).
