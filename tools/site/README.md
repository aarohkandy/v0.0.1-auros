# The website (`website/`), and how it is kept honest

Not published (this folder is not `website/`).

**Pages.** `index.html` is the front door: one sentence, one "Get AurOS"
button, the three steps with a picture of every screen, the four looks,
the small print, questions. `advanced.html` is for someone who knows
computers (the installer names it when it cannot go on, `fix_win.c`).
`guide.html` is one sheet to print. `request.html` is a local-only preview
of the custom-build request contract. Every other `.html` is a redirect
from an old address. One stylesheet (`assets/site.css`), two small scripts
(`assets/door.js` and `assets/request.js`), no fonts to download, no other
server.

**Custom-build preview.** `request.html` writes the versioned
`org.auros.recipe` JSON contract. Its inputs are closed lists: one of the
five real profiles and optional shipped look/theme identifiers. It collects
no prose or personal details and sends nothing. The school kiosk cannot take
a public look override because its locked layout is policy. This is not the
delivery path: a trusted service still has to validate a recipe, build and
test an image, bind the normalized recipe to the signed artifact, and return
that artifact. Until that adapter exists, the page must keep saying that the
generic installer does not read the recipe and that saving one is not a
custom build.

**Design.** docs/DESIGN-DIRECTION.md section 5 (D) is the law: paper and
ink, one green (#0B5D4F) that means "press here" and "chosen", links in ink,
the yellow-and-ink numbered circles only inside pictures, radii 8 and 12, no
shadows, light only. Type is Atkinson Hyperlegible Next, self-hosted
(`assets/fonts/`, OFL) with a plain zero: the upstream zero is slashed and
the font has no other, so `tools/site/plainzero.py UPSTREAM_DIR` redraws it
(O's outline, narrowed) and subsets to woff2. A web address she types keeps
the slashed zero (`.addr`, `zero-slashed-*.woff2`, U+0030 only), or it reads
as the letter O. Floors: text 18px+, contrast 4.5:1+, targets 44px+.

**Pictures.** All made by `python3 tools/site/pictures.py` from the project's
own renders, and tied to them by `website/assets/shots/pictures.json`:
`setup-*.jpg` are crops of B's renders of AurOS Setup (`docs/shots/wizard/
hidpi-*.png`: the installer's paint code run under Wine, NOT photographs of a
real Windows screen, so the page says "made on a test computer" until
`REAL_WINDOWS = True` in pictures.py, when B retakes them on real Windows);
`look-*.jpg` are C's renders of the four looks (`docs/shots/shells/<look>-
1366x768-desktop.png`, the shipping shell code, Light, the programs build/
forge ships), shown 560px or wider. For each picture the sidecar holds the
source render's SHA-256, every line of text in it, and where its numbered
rings go; `check.mjs` (section "THE PICTURES") fails if a render was replaced
and the script not run again, if a line the picture shows is no longer a
string in the installer or the shell, if Setup's Ready page now says a row the
picture lacks, if a ring is not where the button is, or if the test-render note
is missing (or still there once they are real). The warnings of Edge, Windows
and UAC, and "Does everything work?", are HTML drawings (`.edge`, `.ss`,
`.uac`, `.ask`), sharp at any size and in the exact words. Edge's come from
real Edge (stream F's e2e evidence, run 39923720924, Windows 11 26H2) and are
dated on the page. Chrome's and Firefox's are NOT from real screenshots (the
e2e only runs Edge). Not done: a real program in the pictures (a news page in
Internet, a folder of photos): `tools/wltest-apps.sh custom:'...'` could put
Firefox on the shell, but the page it would show on a Linux user agent is the
"open this on a Windows computer" box.
`tools/site/aurshell-shots/` is the earlier kit (my own render of the four
looks from build/forge's names); the site no longer uses it.

**Promises.** Anything the site says the product does is marked and
checked against the code by `check.mjs` (section 7):
`data-fact` (sizes and times, read from `preflight.h` and `wizard.c`),
`data-claim` (BitLocker, the start-up hold, the charger, the release
gate), `data-ui` (button and screen words, which must exist in the
installer's or the desktop's source). Old claims that are false for this
build are listed in `STALE` and must not come back. When the code changes
what it does, the check goes red until the words change too.

**The time.** The page says what Setup says (wizard.c `eta_range`, read by
the check). `tools/site/journey.json` is F's measured journey (the e2e runs
that reached AurOS: about 6 and 7.5 minutes from Get AurOS to AurOS asking, on
a fast runner link; 8 rounded up): the check fails if the time the page
promises ends below it, and the page quotes it. `real_pc_runs` is 0 while
every run has been on a virtual machine, and the front page says "tried so far
on a virtual Windows 11 computer" until it is not. `hdd_median_min` stays null
until a throttled hard-disk run completes.

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
