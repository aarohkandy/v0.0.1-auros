# Stream D — the website (session B)

**Goal.** The front door at https://aarohkandy.github.io/v0.0.1-auros/
(GitHub Pages from `main`, once the owner switches Pages on): one sentence,
one "Get AurOS" button → `releases/latest/download/AurOS-Setup.exe` of the
repository it is published from (never hard-coded: `tools/repo.sh` via
`tools/site/stamp.sh`, and `assets/door.js` on any `*.github.io`), the three
picture steps past the unsigned-download warnings, the four looks, honest
small print, questions. Apple-grade simplicity; ≥18 px, ≥4.5:1, keyboard;
phones/Macs get "open this on your Windows PC" + send-yourself-the-link.

**Owns.** `website/**`, `.github/workflows/pages.yml`, `tools/site/**`,
`docs/shots/website/`.

## Done
- `tools/site/stamp.sh` works on a Mac (BSD sed has no bare `-i`).
  `node tools/site/check.mjs` green for aarohkandy/v0.0.1-auros.

- Redesign, first pass: one stylesheet (`website/assets/site.css`), system
  type, paper and ink, one teal accent; hero + real AurOS picture in a drawn
  laptop; three steps (Edge/Chrome/Firefox, SmartScreen, UAC, Smart App
  Control folded away; real installer screens with numbered circles); the
  four looks as one picture with four buttons (real Sandstone renders,
  `tools/site/aurshell-shots/`); small print; questions; footer.
  how-it-works/desktop/wallpaper pages and their 250 KB of scripts removed
  (old addresses redirect). advanced.html and guide.html on the same sheet.
- Promises checked against the code (`check.mjs` section 7, see
  `tools/site/README.md`): sizes and times from preflight.h/wizard.c,
  BitLocker/hold/charger/release claims, every button word, stale claims
  (Ferry import, self-updates, "switching on starts Windows", SAC can't be
  re-enabled, power cuts) refused. Mutation-tested (site and source).

- docs/DESIGN-DIRECTION.md section 5 (D), applied: tokens (paper/soft/card,
  ink links, one go-green #0B5D4F, marker yellow with ink edge, radii 8/12,
  no shadows, light only); Atkinson Hyperlegible Next 400/700/800 subset woff2
  self-hosted (`website/assets/fonts/`, OFL); the mark drawn as an SVG diamond;
  Get AurOS 72px/30px/radius 12; Smart App Control walkthrough removed (one
  sentence + "Show a helper how" to advanced.html#smart-app-control, K's);
  browser/Windows steps reworded (rule 4, string 9); SIGNED branch deleted;
  step 3 = AurOS Setup's three real screens with numbered circles + the
  "Does everything work?" drawing in C's exact words (ring on Let me look
  first); "Stuck? Show a helper" 48px button in each step card; time word
  for word ("About 10 to 25 minutes. Older computers take longer."); no GB,
  SSD or hard disk on the front page; "can't read the screen aloud yet";
  K's one-truth sentences for No / Put Windows back; Mac gets Copy first;
  no written address on the elsewhere box.
- `check.mjs`: 203 checks green; claims now: BitLocker refuses (B, 1bd6f6e),
  hold rearms, charger required, release tested (promote needs the e2e of that
  exe). `tools/wordlint.sh`: no website lines.

## Second pass (usability walkthrough, F's findings; Sonnet 5.5 session)
- B-01 + F findings 1-3: the Edge pictures redrawn from real Edge (run
  39923720924): three dots, Keep, the black Delete whose small arrow hides
  Keep anyway, Open file; dated "October 2026" with the words for when the
  buttons look different; UAC as it is (yellow band, unknown publisher, No is
  the blue highlighted one: the text says Yes is the white one); the blue
  "Windows protected your PC" box only "if you see it". The old "Show more" is
  refused by the check (the printed guide said it too). Chrome/Firefox
  pictures are NOT from real screenshots (the e2e only runs Edge).
- B-02: a calm "If your antivirus program takes the file" step (no brand,
  nothing installed, where to look, put it back only if it came from this page).
- A-01: phone/Mac: "Send this page to myself" (navigator.share), Copy the link
  with a 24px confirmation (the address itself if the browser won't copy),
  Email second; on a phone the button is above the picture and wholly on an
  iPhone's first screen. Tested with the share sheet mocked four ways.
- A-04: the time follows Setup (B: 25 to 60 minutes, 1 to 3 hours on a hard
  disk; unmeasured in the code). Hook for the measured median:
  `tools/site/journey.json` (null until F's journey clock has a whole run).
- Setup pictures = crops of B's final Light screens (`tools/site/pictures.py`);
  desktop pictures use C's names (Internet, All my programs, Wi-Fi).
- Plain zero (`tools/site/plainzero.py`): the font has no alternate, so the
  zero is redrawn; the typed address on the printed sheet keeps the slashed
  one. The printed sheet shows the website address; no phone number anywhere
  (checked). The "Their phone" blank on the sheet is for the helper to write,
  nothing is printed in it.
- check.mjs: 298 checks. Three green-to-red drifts were fixed on the way:
  advanced.html's BIOS title and welcome.c's answer sentences (6845420), B's
  new time (79a3641), the room figures moving to src/aurstage/plan.c (187a25e).

- Fast path switch (lead's order after A2's 33c51fa): pages.yml was red
  "bitlocker refuses, the code pauses" because check.mjs saw the define in
  phases.h without noticing it sits under `#ifdef AUROS_FASTPATH`, which the
  default build does not set. The check now reads the default from
  build/aurbridge and checks the page against the default build; "Locked
  drives" and a new "Your Wi-Fi" item carry both wordings, and
  `<html data-build="default">` in index.html is the one line to flip to
  "fastpath" when the lead flips the default (the check says so). Also: C's
  first question is now ONE check ("Check one thing first. Does the internet
  work?") with each answer's sentence beside it; the drawing follows. HW's
  #wifi-sticks section in advanced.html is untouched.

## Waiting on others
- Real content in the desktop pictures (a news page in Internet, 12 family
  photos; DESIGN-DIRECTION P0-3) needs C's screenshot set; today they are
  honest renders of an empty desktop with the programs list.
- The measured time: stream F's journey clock (no whole journey has completed
  yet: the last e2e stopped at the BitLocker refusal after the restart).
- P2-13 (no printed address): the guide prints it; a short address of our own
  is the owner's WP-15 domain. `AUROS_DOMAIN` moves the Get AurOS link only
  (stamp.sh); the printed address is still the github.io one.
- Third pass (walkthrough A-02, A-05, A-06, A-09, B-03, B-04, B-06; every
  check was written first and seen to fail, 17 red, then green; 323 checks):
  A-02 a line under the button ("Not sure which Windows you have? Press Get
  AurOS. Setup checks, and changes nothing if it can't go on", the last half
  tied to wizard.c's "Nothing was changed"); A-05 after the press, step 1 is
  scrolled up and says "You are here: your browser is asking if you trust the
  file" (a status; the download is not stopped; not on a phone/Mac); A-06 the
  buttons say "Stuck? Ask someone to help" and the page they open starts with
  what she does (close, press Not now, nothing is changed, show this to
  someone you trust), then Print; B-03 "The answers are always Keep, Run
  anyway and Yes. Never press Delete, Don't run or No" before step 1; B-04 the
  Tab-then-Enter way on the printed sheet; B-06 Smart App Control is "a
  Windows setting ... someone who knows computers can change it" (not "AurOS
  can't go on"), nothing was changed, and a button to K's page; A-09 "AurOS is
  made to keep your files ... copy the photos you treasure first" (no bare No).
- Still not done: A-02's Windows-version detection (the line says to press the
  button instead), the Chrome/Firefox pictures from real screenshots, real
  content in the desktop pictures (needs C's renders), the measured journey
  time in `tools/site/journey.json` (F's runs have not got past the staging
  refusal; watch docs/handoff/streams/F.md "M2").

## Waiting on others
- Real content in the desktop pictures (a news page in Internet, 12 family
  photos; DESIGN-DIRECTION P0-3) needs C's screenshot set; today they are
  honest renders of an empty desktop with the programs list.
- The measured time: stream F's journey clock.
- P2-13: the guide prints the github.io address; a short address of our own is
  the owner's WP-15 domain.
- The guide's hand-written "Their phone" blank stays (the helper's number, by
  hand: lead's decision); AurOS prints no phone number.

## How to verify
```sh
npm i --prefix /tmp/pw playwright@1.56.1 && /tmp/pw/node_modules/.bin/playwright install chromium
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs            # must end "0 failed"
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs --shots /tmp/s   # pictures
```
