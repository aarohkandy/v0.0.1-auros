# Morning report (for Aaroh) — kept current by the lead

_Last updated: 05:55 UTC 2026-10-23 (22:55 your time). Usage ran out; the lead restarts itself at 01:18 your time._

## Done overnight (proof)

- **Release candidate 1 is green and published as a prerelease**: v0.1.0-fc52718 (every gating test passed;
  both installers: normal and fast path). The real-Windows journeys on it are running now. Not public.

- **Video calls and films now proven on the real image, with the picture checked** (corrected runs
  40015793857/40021364066 after an earlier false pass was retracted): camera + microphone, H.264 and streaming
  video with sound, printing to a network printer, LibreOffice Writer/Calc open and save, PDF forms.

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

- **Found and fixed tonight: every click in modern (GTK4) programs landed ~25% off target** — why the store's
  small Install button did nothing. Fixed (7381d67); in the next release candidate.

- **8 of the 10 top programs install from "Get more programs" (no password) and open in 1-2 s**: WhatsApp,
  Teams, Spotify, Solitaire, Thunderbird, gThumb, LibreOffice, Chromium. **Zoom and VLC now draw too** (Xwayland landed: 6b7919b).
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
  (updates silently stop in the US), no disk encryption yet. - **Legal work done** (docs/legal/, no lawyer per your D15): every release now gets its GPL source release
  automatically; licences, terms, disclaimer and privacy pages (v3, each marked "prepared without a lawyer");
  a launch gate (tools/legal/check.py --launch) that refuses to deploy the site or promote while 35 items remain
  (most are other agents' wording fixes, now routed; yours: contact mailbox, the name, Mozilla's OK for the
  Firefox name).

## Waiting for you (each with my recommendation)

- **THE NAME (decide before anything goes public): "AurOS" collides with registered marks** — Emergent Systems
  holds US reg. 4512239 AUROS in class 9 (software), and UserTesting rebranded as "Auros" (29 Sep 2026, filed in
  4 countries). Recommendation: rename now (a find-and-replace today, a crisis after launch). A first naming pass
  (docs/legal/NAMES.md) was inconclusive (search budget ran out; weak candidates). If you have a name you like,
  say it and the agents vet it properly; otherwise a second round of coined names runs next. Evidence: docs/legal/NAME-SEARCH.md.

- **Legal, in order (docs/legal/COMPLIANCE.md §3):** (a) don't share the current prerelease link yet;
  (b) create one contact email someone reads (a free address is fine for now) and tell me it;
  (c) GitHub → Settings → Advanced Security → turn on private vulnerability reporting;
  (d) the domain (also removes GitHub addresses from installed PCs); (e) before public launch: a single-member
  LLC in your state + EIN + business bank account, and insurance quotes (tech E&O + cyber).

0. **Commits the permission system refused to push** (the lead does not push around a refusal; your call):
   - the profiles agent's two (per-edition desktop screenshots; ~1 h of CI), in worktree `wt-p`;
   - the help-pages agent's **full help-page rework to the current desktop** (aabfc81 in worktree `wt-k`; the
     release-blocking check is already green on main without it).
   - the encrypted-disk agent's **41 commits** (disk encryption + PIN, proven in a real-image VM with a virtual
     TPM, switched OFF by default) on branch `wt-s4`.
   Say "push P's commits" / "push K's commit" / "push S4's commits" (or "push all held
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
