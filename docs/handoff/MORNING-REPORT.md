# Morning report (for Aaroh) — kept current by the lead

_Last updated: 05:55 UTC 2026-10-23 (22:55 your time). Usage ran out; the lead restarts itself at 01:18 your time._

## Done overnight (proof)

- **First AurOS prerelease in your repository**: v0.1.0-618726b (AurOS-Setup.exe 35 MB + image pieces),
  https://github.com/aarohkandy/v0.0.1-auros/releases/tag/v0.1.0-618726b. The real-Windows test has
  now gone website → download → wizard → restart into the install screens for the first time
  (e2e run 39923720924); it still fails later in that step and is being iterated.

- **All four extra editions build and boot** (office, revive, school kiosk, multilingual): desktop in 14-18 s; revive idles at 313 MB RAM.

- **Moved to your repository** (aarohkandy/v0.0.1-auros, branch main). CI, Pages (on), secrets
  all in. Update-signing key built into AurOS (afa7755, fingerprint checked by the lead).
- **Website redesigned** (light, one green, real AurOS pictures, every promise checked against
  the code: 203/203 checks). Screens: docs/shots/website/.
- **Installer restyled and proven on a real Windows runner** (all 14 checks green, run 39959577778): light,
  one green, real look previews; BitLocker never decrypted, Recycle Bin never emptied, double-click safe,
  no auto-restart (docs/shots/wizard/).
- **Scam protection**: remote-control apps (AnyDesk, TeamViewer…) blocked on first install
  with a plain warning; "Get me out of this page" always works (Esc×3, Ctrl+Alt+Delete, Help).
- **Help** button works in every look; help pages rewritten; helper page for experts.
- **Wishes backend proven end to end with the real Claude** (https://auros-wishes.aarohkandy.workers.dev):
  your potato and compass examples work; 26/26 hostile tests handled (all 8 dangerous asks refused);
  strangers refused; nothing she says is logged; **$0.00009 per wish (~$80/month at 10,000 people a day)**.
- Plans: expert map (docs/ATLAS.md), master plan (docs/PLAN-END-TO-END.md), design direction
  (docs/DESIGN-DIRECTION.md), AI contract v1 after a red team (docs/AI.md §7).

## Still red / in progress

- **Found overnight, being fixed: Firefox opens as a blank window** (AurOS's compositor did not draw the
  inner surfaces browsers and video players paint into). A new agent owns the compositor and is fixing it;
  a check proves it once fixed. This is exactly why the nightly 'everyday tasks' tests exist.

- **The one blocker that matters most:** on a real Windows 11 (in the test VM) everything works up to the restart
  (Get AurOS → Restart now in 4 min 19 s), then the install step refuses: the Linux tool that shrinks Windows
  could not read a fresh Windows 11 drive (`cannot-measure`). **Solved:** a bug in our own code (it threw away
  the tool's answer when the output was long, which it always is on a real drive); fixed in 6917bde with a test.
  A new prerelease and a new real-Windows run are next. Also seen: Windows 11 switched
  BitLocker on by itself at first login, so encrypted PCs will be the norm (your D13 was right).

- **Honest install time today (the installer's own measured estimate): 25-60 minutes on a solid-state
  drive, 1-3 hours on a hard disk.** Under an hour holds on SSD PCs only. The fast path (stream A2:
  Windows shrinks itself and AurOS is written during the download) is the fix; it is being built.

- No install has passed end to end on real Windows yet (the e2e run is the gate); it now reaches the restart.
- Data-safety fixes in flight: staging clock bug for US time zones, BitLocker never decrypted,
  Recycle Bin never emptied, double-click skipping the Ready page, shared password removed.
- **Safety review done** (docs/safety/FINDINGS.md): 26 confirmed ways her disk or Windows could be hurt
  (12 high, 0 blockers after 3 skeptics each); 9 already fixed, the rest assigned with tests required.
- **Security audit done** (docs/security/FINDINGS.md): 0 critical, 8 high, all assigned: installer trust and
  DLL hijacking, the update-signing key's exposure, mutable release files, no boot fallback, no network time
  (updates silently stop in the US), no disk encryption yet. Legal review: writing its final document.

## Waiting for you (each with my recommendation)

0. Two commits from the profiles agent were refused by the permission system (desktop screenshots per edition; ~1 h of CI). They sit in the scratchpad worktree wt-p. Say "push P's commits" if you want them.

1. **Buy a short domain** (Cloudflare Registrar, ~$10/yr). Installed computers then never depend
   on GitHub addresses. Nothing goes public before this or your explicit "promote without it".
2. **SignPath Foundation application** (free code signing): agents prepare the text; you submit.
3. **5 real used PCs** for testing (one hard disk, one Wi-Fi-only, one BitLocker).
4. **A second person** who can pause downloads if something goes wrong.
5. **USB stick** for the update key (now in ~/AurOS-update-key on your Mac).
6. **Cloudflare Workers Paid, $5/month** (dash.cloudflare.com → Workers & Pages → Plans): the free
   plan's 10 ms CPU limit is tighter than the service's 11-33 ms cold starts.
7. **Anthropic paperwork** (console.anthropic.com): accept the Data Processing Addendum, request zero
   data retention, and confirm the AurOS Wishes workspace has a monthly spend limit (docs/OWNER-SETUP.md §7b).
8. **Protect the update key** (docs/OWNER-SETUP.md, written by the pipeline agent): create the environment
   `apt-signing` (main only, you as required reviewer), move the secret into it, turn on branch protection
   for main and GitHub's release immutability. Today any pushed workflow could read the key.
9. Set the watchdog task to Auto mode (Scheduled → "AurOS: restart the swarm if it stopped").
