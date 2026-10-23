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

## Known red
- `check.mjs`: "the page says bitlocker refuses, the code decrypts". The
  site says what the lead ordered (a BitLocker PC is told it can't install
  yet; D11: never decrypt). It goes green when stream B lands the refusal
  (`fix_win.c` bitlocker-system no longer FIX_AUTO) or the pause
  (`AB_CHOICE_BITLOCKER_PAUSE`, then the site must say "pauses").

## Half-done / next
- docs/DESIGN-DIRECTION.md section 5 (D): in progress.

## How to verify
```sh
npm i --prefix /tmp/pw playwright@1.56.1 && /tmp/pw/node_modules/.bin/playwright install chromium
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs            # must end "0 failed"
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs --shots /tmp/s   # pictures
```
