# AurOS design direction: one product, one hand

Session B, revision 2. Written for the four streams that draw what she sees:
D (website), B (installer), C (AurOS desktop, first run, staging screens) and
K (help pages). Where this differs from [DESIGN.md](DESIGN.md), this wins.

**Checked before writing.** Every claim below was tested against `main`
(`df2b5d8`, then `e74c062`). Line numbers are from `df2b5d8`; some have moved
since. Two critics (an elder-care reviewer and a build reviewer) challenged
revision 1; what they got right is folded in, and what was rejected is listed,
with reasons, at the end.

Facts that shape the direction:

- **Signing is ruled out.** The owner decided against code-signing for good
  (`docs/SIGNING.md`, `docs/handoff/SESSION-2026-10-23.md` D7). The website has
  to live with browser warnings.
- **She already gets a light desktop.** `wizard.c:167-176` writes
  `theme=sandstone` for all four looks and `rootfs/usr/lib/auros/choices.sh:221-227`
  applies it. What is dark is the image default
  (`profiles/desktop.profile:34 theme="nocturne"`), the staging screens and
  every shell screenshot.
- **The shell ignores theme font names today.** `open_font()`
  (`src/aurshell/shellcommon.c:551`) only loads a value starting with `/`;
  anything else falls to a fixed list headed by PT Sans Caption. forge already
  links each named font to `/usr/share/auros/fonts/<name>.ttf`, but nothing
  reads those links. Every font decision below is dead until this is fixed.
- **The rasterizer** (`src/common/font.h`) is unhinted, reads `glyf` and CFF,
  refuses CFF2 and does no shaping or GSUB. Static TTFs only; every glyph must
  be in the cmap. No OpenType feature changes anything on screen.
- **It has no per-glyph fallback.** A glyph the font lacks draws as a box.
- **Atkinson Hyperlegible has no Cyrillic and no →.** The shipped v1 file
  (241 glyphs) has neither; Next is Latin-only. v1 advance widths: I 414,
  l 239, 1 402 (distinct).
- **The shell cannot draw the mark yet.** `draw.c` has no filled polygon. The
  shell shows `brand_text` ("AurOS") in rail, dock and tiles; `brand_glyph`
  only reaches the terminal prompt templates. The taskbar draws an 8×8 accent
  square as its mark (`layouts/taskbar.c:731`). The installer already has an
  anti-aliased `fill_diamond()` (`wizard.c:464`).
- **The installer takes a double-click as two presses.** No debounce, no
  `CS_DBLCLKS`; "Next" on the look page and "Install AurOS" on Ready sit in the
  same place. Her habit (EASY: "double-click it, click Next until it stops")
  skips the one page that tells her the cost.
- **The footer is deliberately theme-proof** (`foot.h`: "the control she uses
  when the screen has become unreadable must not be styled by the thing that
  made it unreadable"). It holds **Close this** whenever a window is open —
  five of six looks have no other way to close a window (`foot.c:86`).
- **Staging screens get no new font by themselves.** `build/staging:342-355`
  copies only DejaVu; nothing creates `/usr/share/auros/fonts/ui.ttf`, which
  `screen.c:213` and `sorry.c:67` look for first.
- **Contrast failures in today's themes:** Nocturne muted `#6A7178` on its
  surface 3.43:1; Sandstone muted `#8F8068` on its background 3.39:1; white on
  Sandstone's terracotta button 3.93:1; the website's gold diamond `#C89B4A` on
  paper 2.51:1.
- **Also confirmed:** footer "Internet" opens Wi-Fi (`foot.c:46`); first-run
  heading "AurOS is on this computer" (`welcome.c:376`); 60 s restart countdown
  (`wizard.c:297`); notifications vanish after 8 s / 30 s and on-screen
  messages after 1.4 s / 3.2 s (`notify.c:55-57`, `osd.c:12`, `:45`);
  "AurBridge … Start installing." (`aurstage/install.c:82-83`); teal `#7DD3C0`
  hardcoded in 8 files; violet backdrop at `wizard.c:545`; `shell_name` values
  don't match the wizard's look names; `choices.sh` has no `text_scale`;
  `windows-latest` runners are Windows Server, not Windows 11.

---

## 1. The idea in one sentence

AurOS should look like **the clear printed instructions that come in the box**:
warm paper, black ink, one green that always means "this way", and the same
picture of her screen wherever she sees it, from the website through the
installer to the desktop.

## 2. Brand

- **Name.** "AurOS", one word. The installer is **AurOS Setup** everywhere:
  window title, dialogs, website, help, staging screens. "AurBridge" is never on
  a screen she can see.
- **Mark, "the waypoint".** A solid diamond: a square rotated 45°.
  - Height equals the cap height of the text beside it. Corners are sharp.
  - It is always **drawn as a shape**, never typed: the installer's
    anti-aliased `fill_diamond()` (not GDI `Polygon`, which is jagged), the
    same 12 lines copied into the shell's `draw.c`, and an SVG `<path>` on the
    web.
  - Fill is the accent: `#0B5D4F` on light, `#5DBB9E` on dark.
  - Minimum size 12 px. Once per screen, beside the word "AurOS" (in Like
    Windows, on the All my programs button, replacing today's 8×8 square).
    Never decorative: no wallpaper, no register mark.
- **Wordmark.** The mark, a 0.3 em gap, then "AurOS" in the UI face at 800,
  tracking −0.5%. For the installer: "AurOS Setup", with "Setup" in 400.
- **Accent.** One colour, **green `#0B5D4F`**, meaning one thing: *the way
  forward, and what you chose*.
  - It marks the primary button, the current step and the selected card, and
    nothing else. Selection is never colour alone: a chosen card also carries
    a check mark and the word **Chosen**.
  - Gold `#C89B4A`, terracotta `#C2683D`, teal `#7DD3C0` and violet `#A78BFA`
    are all retired.
  - Links are ink with a 2 px underline, not blue. The focus ring is ink.
- **Light or dark.** **Light by default on every surface**: website, installer,
  staging screens and desktop.
  - Dark is a choice in Settings → Colors → **Light writing on a dark
    background**, shown with a picture of each choice. It is not a brightness
    control and is never named like one.
  - Boot is the only black screen. Wherever AurOS draws it: the mark centred at
    64 px and one line under it, "AurOS is starting. About one minute."
- **The one fixed element** (DESIGN.md test 3): **Help is always in the bottom
  bar, first in its left group.** On the website, "Stuck? Show a helper" is a
  48 px button bottom-left of each step card. In the installer, the footer's
  left end. In AurOS, the footer's left end; in Like Windows, where the footer
  is folded into the taskbar, Help comes straight after **All my programs**,
  at least 16 px from it and outlined differently. Everything else follows a
  left-aligned grid.

## 3. Tokens

### Colour: Light (default; theme id `sandstone`, shown as "Light")

| Token (theme key) | Hex | Use | Contrast |
|---|---|---|---|
| paper (`bg`) | `#F6F2E9` | page, desktop, wizard content | — |
| soft (`bg_alt`) | `#ECE6DA` | bars, wizard sidebar, alternate sections | — |
| card (`surface`) | `#FCFAF5` | windows, menus, dialogs (not pure white: glare) | — |
| selected (`surface_hi`) | `#E1ECE7` | the chosen row or card only (never hover) | — |
| rule (`overlay`) | `#6E675C` | every border and outline | 5.0 paper, 4.5 soft, 5.4 card, 4.6 selected |
| ink (`fg`, `fg_hi`) | `#17181A` | all text she must read or act on, focus ring, warning text | 15.9 paper, 14.3 soft, 14.7 selected |
| ink-2 (`subtle`) | `#3A3D42` | secondary text, "why can't I press this" explanations | 9.8 paper, 8.8 soft, 9.0 selected |
| quiet (`muted`) | `#5C6066` | **legal lines only** | 5.7 paper, 5.1 soft |
| go (`accent`) | `#0B5D4F` | primary fill, selected border, mark | white on it 7.8; as text 7.0 paper |
| go-dark (`accent_warm`) | `#07463B` | primary pressed | white on it 10.8 |
| warn (`warn`) | `#8A5A12` icon and left bar on `#F7EBD2` | only "you need to do this"; the text is ink | ink on tint 15.0; icon 5.0 |
| marker | `#FFC21A` with a **3 px** ink edge | **only** inside instruction pictures | ink edge vs yellow 11.0 (yellow alone is 1.6 on light shots) |

There is no disabled grey and no red for anything she reads (`err` stays for
terminal colours only).

**What can be pressed looks pressable; what can't, doesn't.**
- Anything she can press: a **2 px** rule border (or the go fill), a verb as its
  label, radius 8.
- Anything she can't press (Good to know, info and warn boxes): **no border**,
  only a 4 px left bar or a tint.
- **Hover** changes only the border, to 2 px ink. The selected fill and the
  check mark appear only when she actually chooses.
- The **primary button is always present** in the same place. If she presses
  it too early it explains in ink-2 beside it ("Pick a look first."). Secondary
  things she can't use yet are hidden.

### Colour: Dark ("Light writing on a dark background", theme id `nocturne`)

| Token | Hex | Contrast |
|---|---|---|
| paper / soft / card / selected | `#121417` / `#181B1F` / `#1E2226` / `#203630` | — |
| rule | `#7A828C` | 4.7 / 4.4 / 4.1 / 3.3 |
| ink | `#ECEEF0` | 15.9 paper, 11.1 selected |
| ink-2 | `#C3C8CE` | 11.0 paper, 7.6 selected |
| quiet | `#A3AAB2` | 7.9 paper, 5.5 selected (legal lines only) |
| go | `#5DBB9E` fill with `#121417` text | 8.0; as text 6.9 on card |
| warn | `#E0B060` icon and bar on `#2A2418`, ink text | 7.7 |

Every value is checked by `tools/contrast.c --strict` against every surface in
its theme, including the selected row.

The **footer's colours stay fixed** (`foot.c:17-21`, 15.6:1) in both themes.
That is the one place a theme does not reach (`foot.h`), and it stays that way.

### Type

**Family: Atkinson Hyperlegible Next** (Braille Institute, SIL OFL 1.1, made
for low vision; I, l and 1 already differ). Called "the UI face" below.

- **Files:** the upstream **static** TTFs (`fonts/ttf/…-Regular`, `-Bold`,
  `-ExtraBold`): 400, 700, 800. No variable font (the rasterizer refuses CFF2
  and forge refuses `fvar`), no italics. Subset with `pyftsubset` (keeps the
  upstream hinting); no ttfautohint step.
- **Character set:** Latin-1, Latin Extended-A, and ’ “ ” — … •. Arrows (‹ › →)
  are **drawn as icons**, never typed.
- **Fallback, not Cyrillic:** the face has no Cyrillic. `font_draw` and
  `font_text_width` gain one fallback handle (DejaVu Sans) for any glyph the
  face lacks, so a Cyrillic file name, network name or program name draws as
  letters, not boxes (C, about 30 lines in `src/common/font.c`).
- **Zero:** ship upstream first, slashed zero included. The plain-oval zero is
  P2: a fontTools script in `tools/` copies O's two contours into `zero` and
  narrows them to zero's advance, repeatably in the build, then the family is
  renamed **AurOS Sans** with the OFL text and a FONTLOG. No hand editing in a
  font editor.
- **Import check** (`tools/`, on every font import): I, l and 1 have distinct
  advance widths; every required codepoint is in the cmap; no `fvar`; and,
  once the P2 edit lands, `zero` has the same contour count as `O`.
- **How the shell finds it:** `open_font()` tries
  `/usr/share/auros/fonts/<name>.ttf` for any name without a `/` (one guard in
  the shared function; `shellpreview.c:65` gets the same lookup). Theme names
  have **no spaces** (forge splits on spaces):
  `font_display="AtkinsonHyperlegibleNext-ExtraBold"`,
  `font_text="AtkinsonHyperlegibleNext-Regular"`,
  `font_text_bold="AtkinsonHyperlegibleNext-Bold"`.
- **No monospace on any surface she sees.** Martian Mono stays on
  `advanced.html` only.

**Scale (px at 100%, at 1024×600)**

| Step | Size / line | Weight | Used for |
|---|---|---|---|
| Display | 40 / 48 | 800 | page question ("What would you like to do?", "Does everything work?"), wizard page title |
| Title | 32 / 40 | 800 | section titles, website h2 |
| Heading | 26 / 34 | 700 | card titles, look names, the helper code |
| Lead | 22 / 32 | 700 | primary button label, emphasis line |
| Body | 20 / 30 | 400 | everything she reads (web body stays 21 / 32) |
| Small | 18 / 26 | 400–700 | labels under icons, footer labels, captions: **the floor for anything she needs** |
| Legal | 16 / 22 | 400, quiet | legal footer only |

Web hero h1: `clamp(40px, 5vw, 56px)`, 800.

Shell mapping (`shellcommon.c:611-630`): `font_size="20"`, `font_size_sm="18"`,
`font_size_lg="18"`, giving huge 40, big 26, mid 22 bold, label 18 bold,
small 20, tiny 18. Change the `dmid` multiplier from 0.95 to 1.2 (22 px).

Labels wrap to two lines; **nothing she reads is ever cut off with "…"**.

### Spacing

A 4 px base. Steps: **4 · 8 · 12 · 16 · 24 · 32 · 48 · 64**.
- Screen inset 24 (1024×600) / 32 (larger). Between groups 32. Inside a card 24.
  Label to control 8.
- Targets: **48 px minimum** on the shorter side. Primary buttons 56 px tall.
  The website's Get AurOS stays 72.
- Things with different consequences are never neighbours without a gap: at
  least **24 px** between Bigger text and Smaller text, **32 px** before any
  button that leaves or undoes something.
- One content width per surface; its right edge is the primary button's right
  edge.

### Input

- **One press is one press.** Ignore every press for **700 ms after a page
  changes**, and a second press on the same control within **500 ms** — in the
  installer and in the shell.
- A small wobble between press and release is still a press, not a drag
  (shell: 8 px slop).
- Pointer: `cursor_size` defaults to **32**, black with a white outline, and
  scales with Bigger text (48 at 1.5×).

### Radii

**8** for buttons, fields and chips. **12** for cards, menus and dialogs. **0**
for surfaces that meet a screen or window edge. Focus ring follows its
element's radius + 3. The website's 14/16/18 go to 8/12.

### Elevation

**No shadows and no blur, anywhere.** `blur=0`, `shadow=0`,
`opacity_panel="1.00"`.
- **Level 0:** paper.
- **Level 1:** a card: a value step plus a rule (2 px if pressable).
- **Level 2 (dialogs):** a card with a 2 px ink rule over a flat 40% ink scrim.
  One alpha fill: cheap on a 2013 Atom and correct at `animation_ms=0`.
- No rounded panel floating on a backdrop.

### Iconography

- Geometry on a 24-unit grid, 2-unit stroke scaled with text, round caps and
  joins (`draw_line` already rounds). Ink; the accent only when selected.
- Rendered at 32 (lists), 48 (tiles, footer) and 64 (dock).
- **Every icon has a text label of at least 18 px.** No icon-only buttons.
- No halo discs, no tinted backgrounds, and **no two things share a
  silhouette.** The required set (about eight are new: printer, zip, Wi-Fi,
  speaker, A+, A−, power, notes):

| Thing | Icon |
|---|---|
| Internet | globe with one meridian and one equator |
| Your files | plain folder, no minus |
| Documents | page with folded corner |
| Notes | pencil on a lined page |
| Printers | printer |
| Settings | gear |
| Zip files | box with a zip |
| Help | ? in a circle |
| Wi-Fi | three arcs |
| Sound | speaker |
| Bigger text | A+ |
| Smaller text | A− |
| Turn off | power symbol |
| Close this | × in a square |
| Get more programs | bag with a plus |
| Back / forward | drawn chevron (never ‹ ›) |
| Chosen | filled check mark |

- Third-party colour icons from "Get more programs" are allowed only as program
  icons, at the same sizes.

### Motion

- **120 ms ease-out**, opacity and colour only, movement 8 px at most.
  `animation_ms="120"`, `animation_curve="ease"`; the spring is retired.
- **Nothing moves by itself, disappears by itself, advances by itself or counts
  down.** The one exception is a restart she has already pressed for, and the
  screen says beforehand what she will see ("The screen goes black, then
  Windows starts. About 2 minutes.").
- Progress: a determinate bar, "Step 3 of 7" and "About N minutes left". With no
  estimate, a 3-segment stepping bar (600 ms) plus the step count.
- The web honours `prefers-reduced-motion`. Every surface reads correctly at
  `animation_ms=0`.

## 4. Voice

1. **Say what happens next, in her terms.** No component, file, code, size unit
   or brand of someone else's software. A failure says what happened to *her
   task* and what she can do now.
2. **One name per thing: the name on the button.**

   | Thing | Its only name |
   |---|---|
   | The installer | AurOS Setup |
   | The browser | Internet |
   | The network button | Wi-Fi |
   | The file manager | Your files |
   | The program list | All my programs |
   | The program store | Get more programs |
   | The way back | Go back to Windows |
   | The text-size buttons | Bigger text / Smaller text |
   | The dark theme | Light writing on a dark background |
   | The four looks | Like Windows, Like a Mac, Big and simple, One thing at a time |
   | The computer | this computer (never "PC") |

3. **Tell her the cost before, not after.** Time as a range ("About 10 to 25
   minutes"). What goes away, and how to keep it. Whether she must stay
   connected or plugged in.
4. **Reassure with facts, never adjectives.** "Nothing on this computer has been
   changed." Never "normal", "simply", "just", "oops", "don't worry".
5. **Short sentences ending in full stops, in US English.** Buttons are verbs
   that name the result ("Install AurOS", "Restart now", "Not now"). Never a
   bare OK, Yes, No or Close. Use "—", never "--".
6. **Every way out says where she lands.** "Not now: AurOS Setup closes. Nothing
   has been changed." "Restart later: AurOS goes on next time you restart."
   "Stop" is shown only while stopping is still safe, with "Nothing has been
   changed yet."

**Before / after**

| # | Before | After |
|---|---|---|
| 1 | AurOS is on this computer | **Does everything work?** Check two things first. |
| 2 | It restarts by itself in 52 seconds. | **Ready to restart.** Save anything you're working on, then press Restart now. |
| 3 | windows  the Windows drive is now 8 MB | Making room for AurOS. Windows is being made smaller. |
| 4 | To try again, open AurBridge in Windows and press Start installing. | To try again, open AurOS Setup in Windows and press Install AurOS. |
| 5 | This PC trusts Microsoft's key for Windows (Microsoft Corporation UEFI CA 2011)… | **One setting needs changing first.** This computer is set to start only Windows. Someone who knows computers can change it in about five minutes. Nothing has been changed. |
| 6 | Drive C: is encrypted. We switch encryption off. | Windows has locked this drive. We unlock it. This can take an hour or more, so leave the computer on and plugged in. |
| 7 | Firefox — Browse the World Wide Web | **Internet** — Websites, news and shopping |
| 8 | What happened: (Windows error 5) | Windows wouldn't let us make room. Restart Windows, then try again. |
| 9 | Every program they haven't seen many times… That's normal. | Windows doesn't know AurOS yet, so it asks if you trust it. Press Keep only if you got this file from the AurOS page a moment ago. |
| 10 | Almost there. A few things first | **A few things to sort out first.** |

## 5. Per-surface specs

Each item is tagged with its owner stream and priority. **Dependency:** D's
pictures come from C's and B's restyled builds, so C P0-1 and B P0-1 land
first. Until then D ships text fixes only.

### D — Website (`website/**`)

1. **P0 (D). Stop walking her through switching off a Windows protection.**
   - Delete the Smart App Control walkthrough from `index.html`. Replace it
     with: "If Windows says Smart App Control blocked it, AurOS can't go on this
     computer yet. Nothing was changed." Add a **Show a helper how** button to
     `advanced.html#smart-app-control` (K).
   - Rewrite the browser-warning steps per voice rule 4 and string 9.
   - Delete the dead `SIGNED` branch and its signed wording in `door.js` (D7).
2. **P0 (D). Step 3 shows the real installer.**
   - Replace the `#shot-installer` drawing with the three real screens: welcome
     → Next, look picker → Next, Ready → **Install AurOS**. Restyled B shots,
     640 px max width.
   - Each ring is the marker token (3 px ink edge). Number badges sit at the
     target's top-left, 8 px outside, `position:absolute`, numbered from 1
     within each picture group.
   - Retitle: "Pick a look, then press Install AurOS". Call them "numbered
     circles", never by colour.
3. **P0 (D). One true picture of her screen.**
   - Every product image is the **Like Windows** look, Light theme, real
     content: a news page in Internet, a folder of 12 family photos.
   - Help visible in the bottom bar. Clock at 9:41. Captured at 2×, shown
     ≥560 px wide so labels read at ≥18 px.
   - The four look cards use the same theme, no empty windows. Remove "Email"
     from all copy (there's no Email program).
4. **P0 (D). Time and loss told up front.**
   - "About 10 to 25 minutes. Older computers take longer." Word for word the
     installer's, in both places.
   - FAQ "Can I go back?" gains the loss sentence K settles (K P0-2).
5. **P0 (D). "Stuck? Show a helper" is a 48 px button**, bottom-left of each
   step card, not a text link.
6. **P1 (D). Fonts.** Replace `atkinson-400/400i/700.woff2` with the UI face
   400/700/800 woff2, subset and self-hosted, `font-display: swap`. No CDN.
   Martian Mono loads only on `advanced.html`.
7. **P1 (D). Tokens.**
   - Rewrite `door.css :root` to the token table, including the pressable /
     not-pressable rule (2 px border vs left bar).
   - `--link` becomes ink with the underline; `--focus` becomes ink (4 px ring,
     offset 3).
   - Replace the gold header diamond and the favicon with the inline SVG mark
     in go green. Radii to 8/12.
8. **P1 (D). The Get AurOS button.** Flat `#0B5D4F`, pressed `#07463B`. Remove
   the 3 px border, the hard shadow and the `:active` translate. 72 px tall,
   30 px 700, radius 12.
9. **P1 (D). Remove words she doesn't use.**
   - Move "More systems, like Ubuntu and Linux Mint…" to `advanced.html`.
   - "Room it needs": "AurOS needs some empty room on your computer. AurOS
     Setup checks, and tells you if there isn't enough."
   - No GB figures, no SSD or hard disk on this page.
10. **P1 (D). The "Does everything work?" drawing.** 720 px max so the three
    buttons share one row, in C's order. Ring on **Let me look first**. Heading
    text matches C exactly.
11. **P1 (D). Say what AurOS can't do yet, before she installs** (rule 9):
    "AurOS can't read the screen aloud yet."
12. **P2 (D). Phone and Mac.** On phones, a 16:10 crop of the home screen
    between the sub-headline and "Send yourself the link". On a Mac, **Copy the
    link** first. "Is it safe?" opens with: "Your files and Windows are kept
    until you say everything works."
13. **P2 (D, owner decision).** A short address of our own replaces the
    `…github.io/…` address in anything printed or spoken. Until then, no
    surface prints any address; use buttons.

### B — Installer (`src/aurbridge/wizard.c`, `fix_win.c`, `sbdb.c`)

1. **P0 (B). Light restyle in one block.**
   - Replace the `C_*` defines at `wizard.c:100-116`:

     | Define | Value |
     |---|---|
     | `C_BG` | paper |
     | `C_BG_ALT` | soft (sidebar) |
     | `C_SURFACE` | card |
     | `C_SURFACE_HI` | selected |
     | `C_OVERLAY` | rule |
     | `C_MUTED` | quiet |
     | `C_SUBTLE` | ink-2 |
     | `C_FG`, `C_FG_HI` | ink |
     | `C_ACCENT`, `C_OK` | go |
     | `C_WARN` | warn |

   - Delete `C_ACCENT_ALT`, the backdrop ribbons (`wizard.c:540-580`,
     `BACKDROP_A1/A2`), the violet dot (`:2121`) and the step-dot glow.
   - Sidebar flat soft, 280 px with a 32 px gutter; below 1100 px client width
     it collapses to one "Step 3 of 5" line. Content flat paper to the window
     edge, no floating panel. One right edge, flush with the button edge.
2. **P0 (B). One press is one press.**
   - Ignore presses for 700 ms after any page change and a repeat on the same
     widget within 500 ms (`WM_LBUTTONUP`, `:4025`, and the keyboard path).
   - Ready's **Install AurOS** looks different from Next: wider, a check-mark
     icon, and "This starts the install." above it.
   - Add to the existing `nt_check` self-test: two presses on Next within
     300 ms land on Ready, not on progress.
3. **P0 (B). Embed the UI face.**
   - The three subset TTFs as `RCDATA`, loaded with `AddFontMemResourceEx` at
     start-up; the OFL text as one more `RCDATA` resource.
   - GDI selects by **legacy family name (nameID 1)**: 400/700 as "Atkinson
     Hyperlegible Next" at `FW_NORMAL`/`FW_BOLD`; 800 as its own family at
     `FW_NORMAL`. `FACE_DISPLAY` is the 800 family, `FACE_TEXT` the other.
   - Keep `CLEARTYPE_QUALITY`. If loading fails, fall back to Segoe UI, never
     Georgia.
   - Sizes: page title 32/800, body 20/400, labels and buttons 20/700,
     footer 18.
4. **P0 (B). No countdown.**
   - Remove the `COUNTDOWN_SECONDS` path (`:297`, `:2788`, `:3147`, `:3491`).
     The finish page is always "Ready to restart" with **Restart now**
     (primary) and **Restart later** ("AurOS goes on next time you restart."),
     in a neutral box.
   - The progress page: "You can walk away. When you come back, press Restart
     now. If the screen has gone dark, move the mouse." On resume from sleep
     or unlock, bring the window to the front.
   - Never `EWX_FORCEIFHUNG` without her press.
5. **P0 (B). Secure Boot page (`sbdb.c:266-283`).**
   - String 5 as title and body.
   - **Show a helper how** opens the firmware sketch plus a **Copy for a
     helper** text block (the technical paragraph). **Not now** is secondary
     and says where she lands.
   - Delete the duplicated "WHAT TO DO" paragraph. Fix the contradictory "It
     trusts no signing keys at all" clause (`sbdb.c:271`).
6. **P0 (B). Error text.** Map every `What happened: %ls` (`:2812`) and `We
   couldn't fix this: %ls` (`:2143`) to one of four sentences; the raw text
   goes to the log only:
   - "The internet stopped partway. Check you're online, then press Try again."
   - "This computer ran out of room. Press Try again."
   - "Windows wouldn't let us make room. Restart Windows, then try again."
   - "Something went wrong on our side. Nothing was changed."
7. **P0 (B). Look picker** (`3a`; small `small-3a`).
   - A 2×2 grid, no scrolling. Cards hold the preview and the name only; one
     line under the grid describes the chosen look.
   - At 1120 wide: previews at least 230×145, name under each.
   - At 1024×530 (sidebar collapsed): previews 150 px wide, name beside each
     at 22 px.
   - Where even that doesn't fit (150% scaling on 1366×768, or Bigger text):
     a 2×2 of name-only cards with one preview of the chosen look beside them.
     All four choices stay visible; never a scroll bar.
   - Focus: 3 px ink, offset 4. Selected: 3 px go border, selected fill, a
     check mark and **Chosen**. Hover: 2 px ink border only.
8. **P0 (B). Remove printed web addresses** (2b, 2f). Use **Show a helper
   how**, which opens the K page in her browser.
9. **P0 (B). Plain words on the fix pages** (`fix_win.c`).
   - Locked drive: string 6, with the time repeated on Ready. When D11 ships:
     "Windows' drive lock is paused for one restart and comes back on by
     itself."
   - Recycle Bin: "We empty the Recycle Bin (3.2 GB) and remove Windows' old
     update files. Your own files are not touched."
   - The other jargon lines: BIOS, NTFS, Storage Spaces, start-up area.
10. **P1 (B). Bigger text in the installer.** Read Windows' `TextScaleFactor`.
    An **A+ Bigger text** button top-right, at least 48×48, rescales the
    installer. Pass the result on: `choices.sh` writes it to the file the shell
    already reads (`foot_load_text_scale` → `scale_path`); no new key in the
    engine stream.
11. **P1 (B). "A few things to sort out first" (2a).** Two lists, **You:** and
    **We do these for you:**, one dot colour. **Not now** replaces Close on 2a,
    2c and 2g. "We'll fix this one after the ones above." replaces any wording
    that points at colour.
12. **P1 (B). Good to know (1d).** Ink-2 text with an "i" glyph and a left bar,
    no border, no amber heading. Chips become flat text, not button-shaped.
13. **P1 (B). Ready (4a).** "It will look like Windows: a bar along the
    bottom." "AurOS uses 40 GB. Windows keeps 107 GB free." "It downloads about
    N GB, so stay connected to the internet."
14. **P1 (B). Progress.** "Step N of M" and "About N minutes left" on 5a/5b.
    A secondary **Stop** on 5a only while stopping is safe, with "Nothing has
    been changed yet." The stepping bar on 5b. Delete footer hints that repeat
    the body.
15. **P1 (B). Draw the close confirmation yourself.** Replace the stock dialogs
    (`:4108`, `:4118`) with an in-window page: **Keep it and restart later** /
    **Close and undo**. "Say No, and you are back in Windows" becomes "Press
    No, and the computer goes back to Windows."
16. **P1 (B). Mark and welcome.** The mark with `fill_diamond` beside "AurOS
    Setup". The welcome page shows a 360 px preview of Like Windows. Narrator
    reads each problem's fix line, not just its name (`acc_page_name`,
    `:3466-3474`).
17. **P2 (B). Copy and cleanup.** 5e: "The internet dropped while downloading.
    Your files are safe." Drop the redundant "Press Try again…" line. "check
    mark", not "tick". Delete unused `PHASES[].desc` strings.
18. **P2 (B). Retake every `docs/shots/wizard/` shot on real Windows** at 100%
    and 150%: Windows Server 2025 runners (the Windows 11 look) or the owner's
    machine, not Wine. Delete the stale `aurbridge-wizard-welcome.png`.

### C — AurOS desktop, first run, staging screens (`src/aurshell`, `src/common`, `themes`, `src/aurscreen`, `src/aurstage`, `build/forge`, `build/staging`, `profiles`)

1. **P0 (C). The shell reads theme fonts.** `open_font()` tries
   `/usr/share/auros/fonts/<name>.ttf` for any name without `/`; same in
   `shellpreview.c`. Theme font names carry no spaces. Without this, every
   type decision here changes nothing on screen.
2. **P0 (C). Light is the default, everywhere.**
   - `profiles/desktop.profile`: `theme="sandstone"`.
   - Rewrite `sandstone.theme` to the Light tokens, `theme_name="Light"`,
     `wall_style="solid"`.
   - Rewrite `nocturne.theme` to the Dark tokens, `theme_name="Light writing on
     a dark background"`. Both: font keys per §3, `blur="0"`,
     `opacity_panel="1.00"`, `animation_ms="120"`, `animation_curve="ease"`.
   - Settings → **Colors** lists only these two, each with a picture. Delete
     `synthwave.theme`. Ember, moss and slate stay for profiles but are hidden
     from her list.
   - Delete the register mark (`wall.c:375-380`, `:472`) and the teal
     hardcodes (`wall.c:537`, `shellcommon.c:495`).
   - The footer keeps its fixed colours (`foot.c:17-21`); only its layout
     changes (item 9).
3. **P0 (C). Staging screens** (`aurscreen/screen.c`, `aurstage/install.c`,
   `aurstage/shrink.c:361`).
   - `build/staging` copies the 400 and 700 subsets (about 40–60 KB each) as
     `/usr/share/auros/fonts/ui.ttf` and `ui-bold.ttf`; `screen.c`'s bold list
     and `sorry.c:67` look for them. DejaVu stays as the fallback.
   - No log lines by default. Never print a size.
   - **Only when something has failed:** the helper code at 26 px in ink,
     digits only, in groups of three ("482 917 365"), with "If you need help,
     read this number to them." A code on a screen where nothing went wrong
     looks like an error.
   - ~~**Show details for a helper** button~~ -- **not built, by decision
     (D16c, 2026-10-24).** The screen paints from a pipe under PID 1 and reads
     no input, so a button on it could not be pressed. What a helper needs is
     there without one: the number on a stopped screen (the same stop gives the
     same number), and `\EFI\AurOS\stage.log`, written before the restart on
     the computer's start-up partition and readable from Windows afterwards
     (docs/AURBRIDGE.md, "The stage log"). `aurstage.details` on the kernel line
     stays for experts who start the computer by hand. website/advanced.html
     says where to look.
   - Same Light tokens and the drawn mark.
   - Failed install: "AurOS didn't go on this time" in ink, then the string 4
     wording.
   - "Step N of M" and "About N minutes left" under the steps. The restore
     screen gets the same list and, before it restarts: "The screen goes black,
     then Windows starts. About 2 minutes."
4. **P0 (C). Names.**
   - Footer "Internet" (`foot.c:46`) becomes **Wi-Fi**. The taskbar tray's
     Wi-Fi opens the same page.
   - `firefox|Internet|Websites, news and shopping` in forge's rename table
     (`:591-601`). Taskbar "All Programs" becomes **All my programs**.
   - `shell_name`, word for word: taskbar "Like Windows", dock "Like a Mac",
     rail "Big and simple", tiles "One thing at a time".
   - Hide `locked` and `workbench` from Settings unless a profile turns them on.
   - Rail tagline: "Big buttons. Nothing ever gets hidden." Tiles uses forge's
     names: "Your files", "Notes", "Music and video".
5. **P0 (C). First run** (`welcome.c`).
   - Heading string 1, then two checks, each with a button: "Can you see your
     photos? **Open Your files**" and "Does the internet work? **Open
     Internet**".
   - Buttons in this order: **Yes, it all works**, **Let me look first** (the
     safe one, in the middle), a 32 px gap, **No, go back to Windows** at the
     far end. Each has one line saying what it does (K P0-2 sets the words).
     Both Yes and No confirm on a separate page.
   - The declined screen: **Restart now** and "Press Restart now to go back to
     Windows."
   - "Saving your answer." "Your files are copied. Nothing in Windows changes."
     "Open Your files to see them." Name the report's real place.
6. **P0 (C). Nothing vanishes, nothing piles up.**
   - Notifications: the newest shows as one card in a fixed corner that never
     covers a button; more stack into "3 messages". It stays until she presses
     **OK** or opens Messages. Replace-in-place notices (volume, downloads)
     update the same card.
   - On-screen messages (volume, brightness) sit in a strip that covers
     nothing; they stay until her next press, and that press still reaches
     what she pressed.
   - A **Messages** list opened from the clock.
7. **P0 (C). One press is one press** (§3 Input): the same 700 ms / 500 ms
   rule in the shell's event loop, plus 8 px press slop.
8. **P0 (C). Unique icons** per §3. Fix the Zip files fallback (`apps.c:428`).
   Port `fill_diamond` into `draw.c` for the mark.
9. **P1 (C). The footer.**
   - Keep two groups. Left from the corner: **Help**, Wi-Fi, Settings, **Close
     this** (whenever a window is open, last so the others never move). Right
     from the corner: **Turn off**, Bigger text, 24 px gap, Smaller text.
   - Each item: 2 px rule outline on the fixed colours, radius 8, at least
     96×48, icon above label. Above 1.3× text the bar wraps to two rows.
   - In Like Windows the footer folds into the one 56 px taskbar: **All my
     programs** hard left, Help after it (≥16 px gap, different outline), the
     rest as above. No second bar.
10. **P1 (C). Type.** Install the UI face 400/700/800 into the image; theme keys
    per §3; sizes per §3 with `dmid` at 1.2; the DejaVu per-glyph fallback in
    `font.c`. `cursor_size` defaults to 32 and scales with Bigger text
    (`main.c:439-453`).
11. **P1 (C). Taskbar.** One 56 px bar. "All my programs" menu 380 px wide;
    descriptions wrap to two lines, never cut off. Tray icons labelled Wi-Fi
    and Sound. Hover changes the border only (`taskbar.c:724` uses the selected
    fill today).
12. **P1 (C). Dock.** Centred. 64 px icons with 18 px labels that wrap to two
    lines. "+ Find" becomes **All my programs**, which opens the full list with
    nothing typed. The store is one icon at the end.
13. **P1 (C). Rail.** Remove the 01–06 numbers and the decorative rule. All rows
    the same size. **Back to Home** (drawn chevron) top-left. The store goes
    last, after the A–Z list.
14. **P1 (C). Tiles.** "What would you like to do?" as the 40 px heading.
    Home stays visible, marked "You are here". Replace the 30×34 page dashes
    with **Previous page** / **Next page** buttons, 56 px, drawn chevrons.
    One icon placement rule.
15. **P1 (C). Smaller fixes.** Top bar: clock at 20 px. The accent only for
    selection and the primary button. The time-zone page title becomes "Your
    time zone".
16. **P2 (C). Screenshots and cleanup.** New set: Light theme at 1024×600 and
    1366×768, every look, plus welcome, Settings, a notification and the store.
    This set feeds D. Remove the "Install a program you downloaded" (gdebi)
    row; its passwordless grant is on the security backlog.
17. **P2 (C). The plain zero.** The `tools/` fontTools script and the rename to
    AurOS Sans (§3), across image, staging, installer and website in one
    change.

### K — Help pages and `advanced.html` (`rootfs/usr/share/auros/help/*.html`, `website/advanced.html`)

1. **P0 (K). Helper pages the installer and website point to.**
   - Smart App Control: the one-way warning comes **first**, in the warn box,
     before step 1.
   - Secure Boot: steps and the firmware sketch.
   - BIOS to UEFI, and RST to AHCI.
   - Plus the moved "More systems" line and the GB figures.
   - Written for the helper, still free of shell commands.
2. **P0 (K). One truth about going back.** Establish exactly what declining
   keeps ("AurOS is still on this disk") and what "Put Windows back" removes
   ("Anything saved in AurOS goes with it"). Write one sentence for each
   moment, plus the one-line consequence under each first-run button, and give
   them to C (welcome, declined screen) and D (FAQ).
3. **P1 (K). Apply the names from voice rule 2 to every help page.** For
   example: "Internet, the program for websites".
4. **P1 (K). Remove what breaks the rules.**
   - `windows.html`: describe the in-AurOS route (Help → Go back to Windows);
     if a step still depends on a start-up menu, raise it with C rather than
     documenting a timed one.
   - `printing.html`: "GitHub, under Issues" becomes "Ask someone who knows
     computers, and show them the number on the Help page."
   - "double-click" becomes "press Open file".
5. **P1 (K). Help pages use the website's token CSS and the UI face woff2,**
   loaded locally from the image.
6. **P1 (K). `tools/wordlint.sh`, run in CI.** Fails if a user-facing string
   contains an EASY rule-2 word, AurBridge, `--`, PC, wifi, World Wide Web,
   "That's normal", "Start installing", "Darker screen", "yellow", or a bare
   "Close". Scans `website/*.html`, help pages, wizard and `fix_win.c` string
   literals, `install.c`, `screen.c` and `welcome.c`.
7. **P2 (K).** "check mark" everywhere, and the screen-reader sentence mirrored
   from D P1-11.

## 6. What NOT to do

1. **Don't code-sign, and don't plan around signing** (D7). Design the warnings
   honestly.
2. **Don't teach her to switch off a Windows protection.** A one-way change
   belongs on a helper page, with the warning first.
3. **Don't use colour as the only signal.** Selected has a check mark and a
   word; warnings have an icon; pictures say "numbered circles". Cover the
   colour and the meaning must survive.
4. **No gradients, glows, blur, translucency, shadows or halo discs.** Separate
   surfaces with value steps and rules.
5. **Don't type the mark,** and don't draw it with GDI `Polygon`. Use
   `fill_diamond`.
6. **Don't reach for OpenType features** to fix a glyph. Fix the font file.
7. **No variable fonts, Google CDN, web frameworks or icon fonts.**
8. **Nothing she needs below 18 px. No text below 4.5:1, no rule below 4.5:1 on
   light, no target below 48 px.** Quiet is for legal lines only.
9. **No timers on anything she reads or answers.** No auto-restart she didn't
   press for, no self-dismissing message, no "for a few seconds".
10. **Don't show machine output on her screen**: logs, sizes, error numbers,
    file paths, device names. A helper number only when something failed.
11. **Don't name things twice.** A new label comes from the voice rule 2 table,
    or the table changes first.
12. **Don't add a third accent,** and don't use the accent for decoration.
13. **Don't show a screenshot from an old build, from Wine, or from a theme she
    won't get.**
14. **Don't offer her looks that trap her** (kiosk, tiler), or themes she can't
    read. Never remove **Close this** from a look that has no other way to
    close a window.
15. **Don't move buttons under her.** The primary button never disappears;
    controls never shift between visits; Home never vanishes because she is
    home.
16. **Don't let the theme reach the footer.** It is her way back from a screen
    she can't read.

---

## Considered and rejected

- **Elder 7, "every pressable thing gets a fill at least 1.3:1 from the
  page".** A 1.3:1 fill on paper is a mid-beige slab; on every secondary button
  it would compete with the one green primary. The 2 px rule at 4.5–5.4:1, the
  verb label, and borderless info boxes carry the difference.
- **Elder 4, "change the footer's fixed values to a light pair".** The point of
  the footer is that its colours never change; the current pair is tested
  (15.6:1) and the dark band also separates it from the light desktop. No reason
  to touch it.
- **Elder 14, "default pointer 48 px".** EASY's person is 68 with presbyopia,
  not 85 and frail; a 48 px pointer at 1024×600 covers what she is pointing at.
  Default 32, scaling to 48 with Bigger text. The press-slop half is accepted.
- **Elder E, "one look per row with Show the other looks ›" when the picker
  doesn't fit.** That hides three of four choices behind a button, which is the
  "buttons that appear" problem the same critic raised. Name-only cards keep all
  four visible.
- **Elder 19, "Last change at 9:41".** Comparing a timestamp with the clock is a
  puzzle. "Step N of M" plus the minutes estimate shows progress without one.
- **Elder 11, "or one short word list" for the helper code.** Digits are read
  the same way by her, the helper and a phone line; a word list needs a second
  reference. Digits only, without 0 and 1.
- **Build, "keep the slashed zero for good".** Shipping upstream first is
  accepted (it unblocks everything else), but a slashed zero in "10:05" and in
  prices is unfamiliar to her. The plain zero stays, as a repeatable P2 script,
  not a hand edit.
- **Build, "open_font with absolute paths in the theme instead"** (an option we
  considered ourselves). It would need no C change, but bypasses forge's
  glyf/fvar check. One guard in `open_font` keeps that check in the path.
- **Revision 1's own claims, withdrawn:** the Cyrillic requirement (the face
  has none; the fallback covers it); ttfautohint (upstream statics are hinted,
  and it isn't installed); GDI `Polygon` for the mark; "`brand_glyph` falls back
  to ■ in the shell" (the shell never draws it); I/l widths of 282/154 (they
  are 414/239); "Help is the first of six footer buttons running from the left"
  (it dropped Close this and moved Turn off out of its corner); "notifications
  stay until OK" as modal boxes (they would pile up); "hide what she can't use
  yet" for primary buttons.
