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

## Half-done / next
- Redesign (in progress).

## How to verify
```sh
npm i --prefix /tmp/pw playwright@1.56.1 && /tmp/pw/node_modules/.bin/playwright install chromium
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs            # must end "0 failed"
PLAYWRIGHT_DIR=/tmp/pw node tools/site/check.mjs --shots /tmp/s   # pictures
```
