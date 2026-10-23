# The website (`website/`), and how it is kept honest

Not published (this folder is not `website/`).

**Pages.** `index.html` is the front door: one sentence, one "Get AurOS"
button, the three steps with a picture of every screen, the four looks,
the small print, questions. `advanced.html` is for someone who knows
computers (the installer names it when it cannot go on, `fix_win.c`).
`guide.html` is one sheet to print. Every other `.html` is a redirect
from an old address. One stylesheet (`assets/site.css`), one script
(`assets/door.js`), no fonts to download, no other server.

**Design.** docs/DESIGN-DIRECTION.md section 5 (D) is the law: paper and
ink, one green (#0B5D4F) that means "press here" and "chosen", links in ink,
the yellow-and-ink numbered circles only inside pictures, radii 8 and 12, no
shadows, light only. Type is Atkinson Hyperlegible Next, self-hosted
(`assets/fonts/`, OFL) with a plain zero: the upstream zero is slashed and
the font has no other, so `tools/site/plainzero.py UPSTREAM_DIR` redraws it
(O's outline, narrowed) and subsets to woff2. A web address she types keeps
the slashed zero (`.addr`, `zero-slashed-*.woff2`, U+0030 only), or it reads
as the letter O. Floors: text 18px+, contrast 4.5:1+, targets 44px+.

**Pictures.** `assets/shots/look-*.jpg`: aurshell in the Light theme, run on a
Linux box with docker (`tools/site/aurshell-shots/shots.sh`; its app names are
build/forge's rename table). `setup-*.jpg`: crops of B's own Light Setup
screens (`python3 tools/site/pictures.py` reads `docs/shots/wizard/hidpi-*.png`
and prints where each numbered ring goes: paste the percentages into the
`.ring` elements of `index.html`). The warnings of Edge, Windows and UAC, and
"Does everything work?", are HTML drawings (`.edge`, `.ss`, `.uac`, `.ask`),
sharp at any size and in the exact words. Edge's come from real Edge
(stream F's e2e evidence, run 39923720924, Windows 11 26H2); Chrome's and
Firefox's from Chromium's source and the research doc, since the e2e runs
only Edge. Edge is dated on the page: when a newer e2e shows different
buttons, redraw and change the date.

**Promises.** Anything the site says the product does is marked and
checked against the code by `check.mjs` (section 7):
`data-fact` (sizes and times, read from `preflight.h` and `wizard.c`),
`data-claim` (BitLocker, the start-up hold, the charger, the release
gate), `data-ui` (button and screen words, which must exist in the
installer's or the desktop's source). Old claims that are false for this
build are listed in `STALE` and must not come back. When the code changes
what it does, the check goes red until the words change too.

**The time.** The page says what Setup says (wizard.c `eta_range`, read by
the check). `tools/site/journey.json` is where stream F records the median of
the e2e journey clock (SSD and HDD) once a whole journey has been measured;
from then on the check fails if the page's time ends below it
(docs/PLAN-END-TO-END.md WP-40). Until then it says "none yet" and the check
says so in its output.

**Phones.** A phone or tablet sees "Send this page to myself" first (the
share sheet), then Copy the link and Email me the link; the check mocks the
share sheet, cancels it, fails it, and removes it. There is no phone number
anywhere on the site (AurOS never phones), and the check fails if one appears.

**The served build, and the fast-path switch.** The "Get AurOS" button serves
the DEFAULT build of `AurOS-Setup.exe`. Whether that build has the fast path
(BitLocker paused for one restart and never decrypted; her Wi-Fi carried
across) is `${AUROS_FASTPATH:-0}` in `build/aurbridge`; `src/aurbridge/phases.h`
defines the choices only under `#ifdef AUROS_FASTPATH`. `check.mjs` reads
that default and checks every promise against the behaviour of that build.
Where the answer differs, `index.html` carries both wordings
(`data-build-for="default"` / `"fastpath"`), each checked against the code with
the fast path off / on, and ONE attribute chooses what is shown:
`<html data-build="default">`. When the lead flips the default in
build/aurbridge, the check goes red with that one message; change that one
attribute to `"fastpath"` and it is green (both directions were shown).
