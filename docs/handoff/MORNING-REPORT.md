# Morning report (for Aaroh) — kept current by the lead

_Last updated: 00:36 UTC 2026-10-24 (17:36 your time, 23 Oct)._

## Done overnight (proof)

- **On release candidate 6's image, a program installs from the store and opens, end to end and checked by machine**
  (run 40047454422): AurOS's first screen in 36 s, "Start AurOS from now on", the store, the Install button now readable
  (5.2:1), VLC installed in 2 min, opens in 26 s, and found on the disk afterwards. The same journey starting from
  Windows (website → install → this) is running on RC6 now.

- **Safety review done** (docs/safety/FINDINGS.md): 26 confirmed ways her disk or Windows could be hurt
  (12 high, 0 blockers after 3 skeptics each); 9 already fixed, the rest assigned with tests required.
- **Security audit done** (docs/security/FINDINGS.md): 0 critical, 8 high, all assigned: installer trust and
  DLL hijacking, the update-signing key's exposure, mutable release files, no boot fallback, no network time
  (updates silently stop in the US), no disk encryption yet. - **Legal work done** (docs/legal/, no lawyer per your D15): every release now gets its GPL source release
  automatically; licences, terms, disclaimer and privacy pages (v3, each marked "prepared without a lawyer");
  a launch gate (tools/legal/check.py --launch) that refuses to deploy the site or promote while 35 items remain
  (most are other agents' wording fixes, now routed; yours: contact mailbox, the name, Mozilla's OK for the
  Firefox name).
- **Legal work done** (docs/legal/, no lawyer per your D15): every release now gets its GPL source release
  automatically; licences, terms, disclaimer and privacy pages (v3, each marked "prepared without a lawyer");
  a launch gate (tools/legal/check.py --launch) that refuses to deploy the site or promote while 35 items remain
  (most are other agents' wording fixes, now routed; yours: contact mailbox, the name, Mozilla's OK for the
  Firefox name).

- **AurOS installs on real Windows 11 and reaches its first screen in about 8 minutes** (fast path, machine time:
  website +1:29, download clicks +2:19, three installer screens, Restart now +6:36, install step 18 s, "Does
  everything work?" +8:06). One step still fails right after: "Start AurOS from now on" (making AurOS the
  default). **Root cause found and fixed (76d89b9)**: the install step opens the PC's start-up settings
  read-only on purpose, and that carried over into AurOS's first start, so AurOS could not make itself the
  default. Proven in a VM (red before, green after); not yet re-proven on real Windows. **Release candidate 5
  (9dc73fe) is building with it** and journeys on real Windows by itself. Evidence: e2e runs 40025060913 / 40025046920.

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

## Still red / in progress (00:36 UTC 24 Oct)

- **Release candidate 6 (v0.1.0-b28851a, every gate green) on real Windows 11, fast path:** website → download →
  installer → restart → AurOS installed in 1 min 8 s → "Does everything work?" at 6 min → **"AurOS starts from now on"
  works** (the step that failed on every real install before; fixed and now proven). The one step still red: the first
  time she opens the store after a real install, its window opens but stays empty (on the same image started on its
  own, VLC installs and opens: see above). Being fixed two ways: the store's catalogue will ship inside AurOS so it is
  never empty, and the cause on the installed path is being traced. The older-route journey and BitLocker are running.
- **The store's Install button was unreadable and its icons missing**: fixed (f38af6f, 14b3d2a) with a contrast
  test over every theme; in RC6.
- **Gate audit, adversarially checked (docs/handoff/GATE-STATUS.md):** every high-rated safety finding has its fix on
  main; most also have a test that fails if the fix is removed, the rest are being tested now. Two real bugs surfaced
  and are being fixed: the installer could end "installed" after failing to write AurOS's start-up entry on a PC
  whose firmware store is full (S-04), and a full disk during the download is reported as "the internet stopped".
  Many design rules hold in the code but no test pins them yet; routed to the owners.
- **Security launch blockers still open:** no disk encryption and no lock screen (SEC-008/029): the work exists
  but sits in the encryption agent's 41 held commits (item 0 below); and the update key stays readable by any
  pushed workflow until you do item 8 (SEC-004).
- **Small things she would notice, found by a sweep and being added (about 10 MB):** Bluetooth headphones paired but
  silent, no photo thumbnails in Files, some USB printers not set up, .zip files not opening, Eject failing.
- **Programs:** all 10 top programs draw now, including old-style ones (VLC, Zoom), with copy-paste between old and
  new programs (79211d8). An adversarial review of that code is running before it ships.
- **Install time:** fast path measured at 8 minutes on real Windows 11 (VM, solid-state). The website's button still
  gets the older route (25-60 min on SSD, 1-3 h on a hard disk) until the fast path passes real Windows in both the
  normal and BitLocker cases.
- **Caught tonight:** a hardening test (USB printing) had been failing for 100 runs while showing green, because the
  job was skipped. Fixed (c56e891); a public release is now refused unless that test passed on that exact build.

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
   - ~~the help-pages agent's help-page rework~~: **now on main** (cecd55d, carried by K's next push at 8dd0c05, which
     the permission system allowed; nobody pushed around the refusal). Help pages only; helptest 49 checks green.
   - the encrypted-disk agent's **41 commits** (disk encryption + PIN, proven in a real-image VM with a virtual
     TPM, switched OFF by default) on branch `wt-s4`.
   **S4's commits are the two open security launch blockers (disk encryption, lock screen).** Also decide: a PIN at
   start-up for her (recommendation: a 4-digit PIN she chooses at install, with a printed rescue code; AurOS asks
   for it only at start-up and after sleep), or no PIN (then a stolen laptop's files are readable).
   Say "push P's commits" / "push S4's commits" (or "push all held
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
8. **Protect the update key.** Today any workflow on any branch can read it: a test run tonight loaded it on a
   throwaway branch (our own code, printed only "yes", nothing leaked). The steps are in docs/OWNER-SETUP.md 4a-lock.
   When you're done, run `sh tools/sec004check.sh` on your Mac (read-only). It is red on 6 counts now; when it says
   ok, you're done. Optional and cheap only now, before anyone has installed AurOS: make a fresh update key at the
   same time (agents walk you through it; you confirm the new fingerprint). Leave GitHub's release immutability off
   for now (the pipeline agent is moving evidence uploads first).
9. Set the watchdog task to Auto mode (Scheduled → "AurOS: restart the swarm if it stopped").
