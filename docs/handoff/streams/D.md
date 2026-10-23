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

## Waiting on others
- Pictures: AurOS Setup's are B's dark Wine renders (`setup-*.jpg` from
  docs/shots/wizard/); swap for B's Light restyle when it lands (same names,
  ring positions in index.html are percentages; recheck them). The desktop
  pictures say "Firefox" and "All Programs" until C renames them (Internet,
  All my programs); re-render with `tools/site/aurshell-shots/` then.
  DESIGN-DIRECTION P0-3 asks for real content (a news page in Internet, 12
  family photos) captured at 2x: needs C's screenshot set (C P2-16).
- P2-13 (no printed address anywhere) conflicts with guide.html, which is
  printed and must say where to go: owner decision on a short address.
- The slashed zero in "10" is the font's (P2: AurOS Sans, C/B/D together).

## Half-done / next
- P2-12 phones: a 16:10 crop of the home screen above "Send yourself the link".

## How to verify
```sh
npm i --prefix /tmp/pw playwright@1.56.1 && /tmp/pw/node_modules/.bin/playwright install chromium
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs            # must end "0 failed"
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs --shots /tmp/s   # pictures
```
