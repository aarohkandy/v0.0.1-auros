export const meta = {
  name: 'auros-handover-carryover',
  description: 'Adversarial review: everything the same-boot staging-to-AurOS handover (stage_switch_root) carries into AurOS first session, and what breaks',
  phases: [
    { title: 'Find', detail: 'six lenses on what crosses stage_switch_root' },
    { title: 'Verify', detail: 'a skeptic per lens refutes each finding' },
  ],
}

const REPO = '/Users/aaroh/Desktop/auros v0.0.1/v0.0.1-auros'
const GIT = `git -C "${REPO}"`
const EV = '/private/tmp/claude-501/-Users-aaroh-Desktop-auros-v0-0-1/914a7092-8dd8-4369-bb66-bf5c4e2ea1f2/scratchpad'
const CTX = `AurOS installs beside Windows: after one restart a STAGING environment (its own kernel + initramfs, src/aurstage/, build/staging, booted as /EFI/AurOS/staging.efi with cmdline 'aurstage.install ...') writes or commits AurOS, then does NOT reboot: stage_switch_root (src/aurstage/boot.c and nearby) switch_roots into the installed AurOS root in the SAME boot, so AurOS's FIRST session (where she answers 'Does everything work?', presses 'Start AurOS from now on', opens the store) runs on whatever staging left behind. Two bugs of exactly this class are already known: (1) efivarfs mounted read-only by staging carried into AurOS so AurOS could not set BootOrder (fixed 76d89b9 + 7dce7f3); (2) the NIC configured by staging made NetworkManager 'assume' the connection (sys-iface-state external), never run DHCP, so AurOS's first session had no internet (found tonight from AurOS's journal in e2e run 40050260949; evidence zip copy may be under ${EV}). Find EVERY other thing that crosses the handover and breaks or degrades AurOS's first session, or makes it differ from every later boot. Checkout ${REPO} (path has a space; quote it). READ-ONLY: \`${GIT} fetch -q origin\`, \`${GIT} show origin/main:<path>\`, \`${GIT} grep -n <pat> origin/main -- src/aurstage build rootfs src/aurscreen src/aurfirst\`. You may download e2e evidence with \`gh run download <id> -R aarohkandy/v0.0.1-auros -D <a NEW empty dir of your own under ${EV}/handover-<yourlens>/>\` (e.g. run 40050260949 artifact e2e-evidence-base-fastpath, which has diag/aur-journal.txt, and bare-image run 40052634313 for comparison). Never edit, commit or push. Each finding: the carried state, file:line where staging creates it and where the handover fails to reset it, the concrete consequence for her first session (and whether later boots are fine), evidence (journal line or code trace), severity (blocker/high/medium/low), smallest correct fix (staging reset vs AurOS-side guard).`

const F = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  title: { type: 'string' }, carried: { type: 'string' }, where: { type: 'string' }, consequence: { type: 'string' },
  evidence: { type: 'string' }, severity: { type: 'string', enum: ['blocker', 'high', 'medium', 'low'] }, fix: { type: 'string' } },
  required: ['title', 'carried', 'where', 'consequence', 'evidence', 'severity', 'fix'] } } }, required: ['findings'] }
const V = { type: 'object', properties: { verdicts: { type: 'array', items: { type: 'object', properties: {
  title: { type: 'string' }, refuted: { type: 'boolean' }, severity: { type: 'string' }, reason: { type: 'string' } }, required: ['title', 'refuted', 'reason'] } } }, required: ['verdicts'] }

const LENSES = [
  { key: 'kernel-modules-firmware', q: 'The first session runs the STAGING kernel, not AurOS\'s installed kernel. Do the staging kernel\'s version and AurOS\'s /lib/modules match (Wi-Fi, GPU, sound, Bluetooth, printer, webcam drivers load?), firmware_class.path pointing at the new root or not, modules staging loaded with odd parameters, the kernel cmdline (aurstage.*, console=, quiet) seen by AurOS units and by aurfirst, initramfs-only microcode, lockdown and LSM (AppArmor profiles loaded?), Secure Boot/MOK state, kernel updates later (does the first real reboot then pick AurOS\'s own kernel, and does auros-boot-ok/boot counting treat the handover session correctly?).' },
  { key: 'network-wifi-time', q: 'Network beyond the known NIC bug: Wi-Fi handover (D10 keyfile) when staging brought Wi-Fi up, rfkill, wpa_supplicant/iwd processes left running, resolv.conf/systemd-resolved state, hostname, DHCP leases, IPv6, the clock (RTC local time vs UTC, chrony state, timezone), and anything else network-related the first session inherits.' },
  { key: 'mounts-fs-run', q: 'Mounts and filesystems: what is mounted at the handover (ESP, AUROS-SAVED, Windows NTFS read-only?, loop devices, tmpfs), what systemd then sees in /proc/self/mountinfo, fstab vs actual, /run and /tmp contents carried, the root mounted rw/ro and with which options, fsck/resize2fs not run, the swap, journald\'s persistent storage, and whether Windows\' C: remains mounted (dangerous for Fast Startup hibernation).' },
  { key: 'devices-drm-udev-console', q: 'Devices and display: DRM/KMS master held by aurscreen or staging\'s framebuffer, the console and VT state, plymouth, the udev database and coldplug (does systemd-udev-trigger re-run so every device gets its rules: input, sound, Bluetooth, USB printers, webcam?), seat/logind state, sound card state, Bluetooth controller state, keyboard layout.' },
  { key: 'pid1-units-services', q: 'PID 1 and services: how exactly AurOS\'s systemd starts after switch_root (from staging\'s init via exec?), which first-boot units see a \'first boot\' vs not (ConditionFirstBoot, machine-id), units that check the cmdline or /run markers staging left (auros-answer, aurfirst, keeper, hold), environment variables inherited, cgroups, processes staging leaves running (orphans), the journal (staging\'s messages mixed into boot -0), and timers that fire immediately.' },
  { key: 'evidence-diff', q: 'Evidence first: download both journals (installed run 40050260949 diag/aur-journal.txt and bare run 40052634313 diag/aur-journal.txt) and DIFF the first session against the bare boot line by line: every unit that failed, was skipped, behaved differently, every warning present only in the installed session. Explain each difference and whether it hurts her.' },
]

const res = await pipeline(LENSES,
  l => agent(`${CTX}\n\nYour lens: ${l.key}. ${l.q}`, { label: `find:${l.key}`, phase: 'Find', schema: F }),
  (r, l) => (!r || !r.findings.length) ? { lens: l.key, findings: [], verdicts: [] } :
    agent(`${CTX}\n\nYou are a SKEPTIC. For each finding below, try to REFUTE it: is the state really carried (trace the handover code), does AurOS already reset it, would she ever notice, is the severity right? Default refuted=true when you cannot confirm from code or journal evidence.\n\n${JSON.stringify(r.findings)}`, { label: `verify:${l.key}`, phase: 'Verify', schema: V })
      .then(v => ({ lens: l.key, findings: r.findings, verdicts: v ? v.verdicts : [] })))
return res.filter(Boolean)