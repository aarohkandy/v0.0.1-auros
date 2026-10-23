# Stream K: help, support and legal drafts

Brief: `docs/handoff/STREAMS.md` (row K) and the session-B orders in
`docs/handoff/SESSION-2026-10-23-B.md`. Environment and push loop:
`docs/handoff/FIELD-MANUAL.md` (repository aarohkandy/v0.0.1-auros, branch `main`).

## Goal

1. In-OS help pages (`rootfs/usr/share/auros/help/`): short, warm, illustrated where it
   helps, reachable from the Help button in every look, no web address in them.
2. `website/advanced.html` ("for someone who knows computers"; the installer's can't-install
   page names it: `src/aurbridge/fix_win.c` `ADVANCED`). Every address is this repository's.
3. `SECURITY.md`, `docs/SUPPORT.md`.
4. `docs/legal/`: drafts, each marked "DRAFT: must be reviewed by a lawyer before use".

## Done (session B)

- f3c42b0 **Help button → help pages, every look.** The band's Help panel (`foot.c`, shared
  by all looks; not in a kiosk) starts with a filled button "Show me how to do things" (Return
  does the same). It runs `sh -c 'firefox --new-window file:///usr/share/auros/help/index.html'`
  (falls back to `xdg-open`) through `c->spawn`, so the new window is adopted and brought
  forward. Geometry: `foot_help_button()` (paint, press, `tools/targets.c`). targets: 21273
  measured, 0 fail; shown to fail with a 40px button and with the button missing. modaltest green.
  Rendered and looked at (1024x600 at 100% and 200%, hover).
- 8c5e19b **Pages.** New `something-wrong.html` (stuck program: Close this twice; words size;
  no sound; not online; scam page; frozen screen: Turn off / hold power 10 s; AurOS not
  starting; who to ask). Inline SVG pictures of the bar's corner with the button to press
  marked (getting-online: Internet; never-call and something-wrong: Close this; something-wrong:
  Bigger; windows: Turn off), bar colours from foot.c, outlined for dark mode. Index no longer
  claims Bigger/Smaller resize the pages (they don't reach the browser): says Ctrl and +, and
  how to come back (Help, then Show me how to do things).
- `tools/helptest.py` 41 checks. New: pictures have role=img + aria-label (words checked too),
  no own styling, only labels the bar really has (read from foot.c's `LABEL[]`), exactly one
  marked + one ring; the index names the Help panel's button exactly as foot.c paints it; no
  page spells out a web address. Every new check shown to fail on a broken copy (11 mutants)
  and on a foot.c with a renamed button.
- 00cc71b advanced.html: the learn.microsoft.com link is now words; `check.mjs` (D's file,
  7 lines) fails if advanced.html links anywhere but `https://github.com/<repo>` or the site
  (shown to fail with `...-auros-evil` and with the old site). 108 passed.
- 45dca15 legal drafts: Wishes (`docs/AI.md`) is the one thing that sends her words out:
  privacy section 4a, a terms paragraph, lawyer questions in README. All four files still
  carry the DRAFT banner.
- (this commit) windows.html/something-wrong.html no longer say "straight away"/"a few
  seconds"; advanced.html footer no longer says the page "doesn't send anything anywhere"
  (GitHub logs visitors' IPs); OWNER-SETUP 4b: switch on private vulnerability reporting
  (checked: off) and add a security contact.
- Addresses in SECURITY.md, SUPPORT.md, legal: moved to the new repository by stream M
  (2a0ac90, 45dbc93). SUPPORT.md lists the new page and why the pages carry no address.

## Why the help pages name no web address

They are copied into the image as they are (`build/forge` "THE WHOLE rootfs/ TREE"). Stamping
an address in at build time would need `tools/repo.sh` inside forge, which runs under
`sudo` on Actions: sudo drops `GITHUB_REPOSITORY` and root's git refuses the runner's
checkout ("dubious ownership"), so repo.sh would fail and break the image build. No address
= nothing to go stale; helptest enforces it. If an address is ever wanted, pass `AUROS_REPO`
through `sudo env` in build.yml first, then stamp with `tools/site/stamp.sh`.

## Next steps

1. Watch the first image built with f3c42b0 in a real run (F's e2e or a KVM test): press Help,
   then "Show me how to do things"; a Firefox window with the help index should come forward.
   Not yet seen on a real AurOS (only rendered and unit-measured).
2. When Wishes ships: `/v1/ask` returns a `help_page_id` (docs/AI.md change 2). Use the file
   stems as ids (`getting-online`, `programs`, `printing`, `video-calls`, `something-wrong`,
   `windows`, `never-call`); renaming a page then breaks that contract. Re-check privacy 4a
   against the shipped consent screen and service.
3. When S2's "Get me out of this page" lands, mention it in never-call.html.
4. When BT gives a no-timer way to start Windows once (BootNext / grub-reboot from Settings or
   Turn off), rewrite windows.html "Use Windows for a moment" around it.
5. Legal: owner fills `[OWNER]`, lawyer answers `[LAWYER]`, engineer resolves `[CHECK]`
   (privacy host table: packet-capture an idle AurOS VM).

## Facts the pages quote (re-check when C/BT change them)

- Band (`foot.c` `LABEL[]`): Help, Internet (= wifi), Settings, Close this (only with a window
  open); right: Smaller, Bigger, Turn off. Turn off asks: Turn it off / Start it again / Let it
  sleep / Never mind. Close this twice within a few seconds stops a program that doesn't answer.
  Bigger/Smaller change the shell only, not Firefox.
- Wifi (`net.c`): "Looking for wifi near you", Connect, "You are connected", "That password did
  not work", "has no wifi". Browser on home is **Firefox** (Name=Firefox); printers app "Printers".
- Store "Get more programs" (taskbar: All Programs, last; dock: beside Find; rail: home; tiles:
  last big button). Downloaded installer files are not a way to add programs.
- Welcome: "Yes, it all works" / "Let me look first" / "No, go back to Windows" / "Bring my
  files across". Settings: "How loud the sound is", "Headphones and mice", "Put Windows back"
  → "Remove AurOS and put Windows back" (press again).
- Boot menu (`grub.cfg.in`): 3 s; AurOS, three developer entries (verbose, aurinit, recovery
  shell), Windows, "Put Windows back (remove AurOS)" with "No, keep AurOS" first.

## Traps

- A program-list entry whose Exec is `xdg-open` leaves a "Starting..." slot that becomes "This
  did not start" (no app_id match). The Help hook spawns directly, without a slot.
- Opening a page in an already-running Firefox without `--new-window` makes a tab in a window
  she may not be looking at.
- EASY rule 2's word list applies to the help pages and to picture labels.
- The bar's band colours sit on a near-identical dark page: the pictures need their outline.

## How to verify

```sh
python3 -I tools/helptest.py                       # 41 passed, 0 failed
PLAYWRIGHT_DIR=<dir with playwright> node tools/site/check.mjs   # 108 passed, 0 failed
# targets + modaltest (Linux, e.g. homebase): gcc -O2 -std=gnu11 -o targets tools/targets.c \
#   src/aurshell/{foot,net,settings,bt,welcome,power,run,draw,shellcommon,anim}.c \
#   src/aurshell/layouts/[a-z]*.c src/common/{theme,font}.c -I src/aurshell -I src/common -lm
```
