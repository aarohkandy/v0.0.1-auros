# Morning report (for Aaroh) — kept current by the lead

_Last updated: 05:55 UTC 2026-10-23 (22:55 your time). Usage ran out; the lead restarts itself at 01:18 your time._

## Done overnight (proof)

- **CORRECTION: "video calls and films work on the real image" was NOT proven** — the check that said so was
  fooled by a card left on screen; a corrected run is queued. The compositor fix itself is proven by its own
  tests (Firefox, GTK4, Qt, video draw on the real desktop). Being fixed: printing, Word/Excel (LibreOffice).

- **Your potato wish works on AurOS** (rendered from the real code): docs/shots/wishes/rail-1366x768-08-confirm.png
  ("Is this what you want?" in plain words, equal Yes/No) and docs/shots/wishes/rail-1366x768-30-fill-potato.png.
  Wishes engine + screens + voice done (~1,500 checks, 180+ deliberate breakages caught); stays off until the
  last gates (Firefox fix, signed cloud address) pass.

- **Security updates reach her by themselves, proven**: AurOS's own update channel is live, signed with your key;
  forged or altered updates are refused; on the real image an update installs while she uses it with no restart,
  and a deliberately broken update is rolled back automatically (16/16 checks, run 39965081792).

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

- **8 of the 10 top programs install from "Get more programs" (no password) and open in 1-2 s**: WhatsApp,
  Teams, Spotify, Solitaire, Thunderbird, gThumb, LibreOffice, Chromium. Zoom and VLC wait for Xwayland.
  LibreOffice Writer+Calc are in the image; printing works. (run 40012157679)
- **Programs from "Get more programs" would not start** (an extra permission the app sandbox refuses): **fixed and
  proven** (6b6c438). Next gap: older programs that only speak X11 (VLC, likely Zoom, Spotify, WhatsApp/Teams
  wrappers) need Xwayland, which AurOS's compositor doesn't have yet — being built.

- **Caught before anyone used it: Ubuntu security updates would never have installed** (renaming the system to
  AurOS broke the updater's source names). **Fixed and proven on the real image** (956af4a); now a release gate.

- **Fixed overnight: Firefox opened as a blank window** (the compositor never drew the inner surfaces browsers
  and video players paint into). Now Firefox, GTK4, Qt and video all draw; 21 pixel-exact tests guard it.

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
  (updates silently stop in the US), no disk encryption yet. - **Legal review done** (docs/legal/COMPLIANCE.md, 58 findings, no lawyer per your D15): before anyone uses
  AurOS we must publish the full source of the Ubuntu software we ship (GPL) and have a contact address; the
  legal agent is building the source release, licences page, terms and honest wording now.

## Waiting for you (each with my recommendation)

- **Legal, in order (docs/legal/COMPLIANCE.md §3):** (a) don't share the current prerelease link yet;
  (b) create one contact email someone reads (a free address is fine for now) and tell me it;
  (c) GitHub → Settings → Advanced Security → turn on private vulnerability reporting;
  (d) the domain (also removes GitHub addresses from installed PCs); (e) before public launch: a single-member
  LLC in your state + EIN + business bank account, and insurance quotes (tech E&O + cyber).

0. **Commits the permission system refused to push** (the lead does not push around a refusal; your call):
   - the profiles agent's two (per-edition desktop screenshots; ~1 h of CI), in worktree `wt-p`;
   - the installer agent's **DLL-hijacking protection (SEC-003, high)**, commit fb463fb in worktree `wt-b`
     (tested on homebase; its last amend may not have completed);
   - the help-pages agent's **full help-page rework to the current desktop** (aabfc81 in worktree `wt-k`; the
     release-blocking check is already green on main without it).
   - the encrypted-disk agent's **38 commits** (disk encryption + PIN, proven in a real-image VM with a virtual
     TPM, switched OFF by default) on branch `wt-s4`.
   Say "push B's commit" / "push P's commits" / "push K's commit" / "push S4's commits" (or "push all held
   commits"). Or say "allow agent pushes" and I add a project permission rule so this stops happening.

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
