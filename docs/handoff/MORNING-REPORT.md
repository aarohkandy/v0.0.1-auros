# Morning report (for Aaroh) — kept current by the lead

_Last updated: 05:55 UTC 2026-10-23 (22:55 your time). Usage ran out; the lead restarts itself at 01:18 your time._

## Done overnight (proof)

- **All four extra editions build and boot** (office, revive, school kiosk, multilingual): desktop in 14-18 s; revive idles at 313 MB RAM.

- **Moved to your repository** (aarohkandy/v0.0.1-auros, branch main). CI, Pages (on), secrets
  all in. Update-signing key built into AurOS (afa7755, fingerprint checked by the lead).
- **Website redesigned** (light, one green, real AurOS pictures, every promise checked against
  the code: 203/203 checks). Screens: docs/shots/website/.
- **Installer restyled** to match (docs/shots/wizard/).
- **Scam protection**: remote-control apps (AnyDesk, TeamViewer…) blocked on first install
  with a plain warning; "Get me out of this page" always works (Esc×3, Ctrl+Alt+Delete, Help).
- **Help** button works in every look; help pages rewritten; helper page for experts.
- **Wishes backend live** on Cloudflare (https://auros-wishes.aarohkandy.workers.dev),
  refuses strangers, logs nothing she says; real-Claude tests running with your workspace key.
- Plans: expert map (docs/ATLAS.md), master plan (docs/PLAN-END-TO-END.md), design direction
  (docs/DESIGN-DIRECTION.md), AI contract v1 after a red team (docs/AI.md §7).

## Still red / in progress

- No install has passed on real Windows yet (the e2e run is the gate).
- Data-safety fixes in flight: staging clock bug for US time zones, BitLocker never decrypted,
  Recycle Bin never emptied, double-click skipping the Ready page, shared password removed.
- Safety review, security audit and legal review: results arriving.

## Waiting for you (each with my recommendation)

0. Two commits from the profiles agent were refused by the permission system (desktop screenshots per edition; ~1 h of CI). They sit in the scratchpad worktree wt-p. Say "push P's commits" if you want them.

1. **Buy a short domain** (Cloudflare Registrar, ~$10/yr). Installed computers then never depend
   on GitHub addresses. Nothing goes public before this or your explicit "promote without it".
2. **SignPath Foundation application** (free code signing): agents prepare the text; you submit.
3. **5 real used PCs** for testing (one hard disk, one Wi-Fi-only, one BitLocker).
4. **A second person** who can pause downloads if something goes wrong.
5. **USB stick** for the update key (now in ~/AurOS-update-key on your Mac).
6. Set the watchdog task to Auto mode (Scheduled → "AurOS: restart the swarm if it stopped").
