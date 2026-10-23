# The website (`website/`), and how it is kept honest

Not published (this folder is not `website/`).

**Pages.** `index.html` is the front door: one sentence, one "Get AurOS"
button, the three steps with a picture of every screen, the four looks,
the small print, questions. `advanced.html` is for someone who knows
computers (the installer names it when it cannot go on, `fix_win.c`).
`guide.html` is one sheet to print. Every other `.html` is a redirect
from an old address. One stylesheet (`assets/site.css`), one script
(`assets/door.js`), no fonts to download, no other server.

**Design.** Paper and ink; one accent, the deep teal, and it means
"press here" (buttons, links, the numbered circles in the pictures).
Type is the computer's own (Segoe UI on Windows). Floors: text 18px+,
contrast 4.5:1+, targets 44px+. One idea per section.

**Pictures.** `assets/shots/look-*.jpg` and `first-start.jpg` are real:
aurshell paints them in Sandstone (`tools/site/aurshell-shots/`, run on a
Linux box with docker). `setup-*.jpg` are the installer's own screens
from `docs/shots/wizard/` (refresh them when stream B renders new ones;
the numbered circles are placed in `index.html` by percentage). The
Windows and browser warnings are HTML drawings (`.win`, `.ss`, `.uac`),
worded from `docs/research/every-machine-2026-10.md` section 5.

**Promises.** Anything the site says the product does is marked and
checked against the code by `check.mjs` (section 7):
`data-fact` (sizes and times, read from `preflight.h` and `wizard.c`),
`data-claim` (BitLocker, the start-up hold, the charger, the release
gate), `data-ui` (button and screen words, which must exist in the
installer's or the desktop's source). Old claims that are false for this
build are listed in `STALE` and must not come back. When the code changes
what it does, the check goes red until the words change too.
