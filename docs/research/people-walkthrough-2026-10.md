# People walkthrough, October 2026: a cognitive walkthrough of the whole journey

Stream USABILITY, first pass. Written against `main` at `5006a1d`.
Method: three concrete people, walked screen by screen through the current
screenshots (`docs/shots/`) and the strings in the source, asking at each
screen the four walkthrough questions and four extra ones (below).
**This is a prediction, not a measurement.** Nobody over 70 has used it yet.
Every finding is a hypothesis the sessions in `docs/usability/SCRIPT.md` will
confirm, kill or re-rank. Where a finding is inferred from code and not seen
running, it says so.

## 0. How to read this

- **Severity.** S1: she stops for good, is frightened enough to quit, or loses
  data (counts against "zero data loss"). S2: she needs help or loses more
  than five minutes. S3: a small stumble she recovers from.
- **Persona letters.** E = Edith, M = Margaret, R = Ray (below). "E, M" means
  both are hit; a letter in brackets is a milder hit.
- **Owner.** D website, B wizard, A2 staging screens (the drawing code is C's
  `src/aurscreen`), C desktop, W2 Wishes, K help pages. Other streams are
  named in full (BT boot, R Ferry, F e2e, P packages, S2 browser, OPS domain).
- **Evidence.** A file under `docs/shots/` or `docs/research/`, or
  `file:line` in the source. Quote a string with `git grep`; if it is gone the
  finding is fixed.

### What I could and could not see

| Surface | State of the evidence |
|---|---|
| Website, all sizes | Seen. Light, post design-direction (`54fd273`). |
| Browser and Windows warnings | Edge on real Windows 11 seen once, in the e2e evidence of run 39902908530 (`0011` to `0017`). Chrome, Firefox, antivirus: not seen anywhere. |
| Wizard | Seen. Light, post restyle (`7fb9b2d`), 1120x690, 1024x530, 150%. |
| Restart, GRUB, black screens | Not seen. Read from `grub.cfg.in` and `aurscreen`. |
| Staging screens | Seen, but **still the dark, pre-restyle** screens (`docs/shots/staging-*.png`, `screen.c:60-66` still `0B0E14` and teal). |
| First-run "Does everything work?" | Not seen rendered. Read from `welcome.c`. The website draws a replica. |
| Desktop, four looks | Seen, but **dark, pre-restyle** (`docs/shots/shells/*`, taken before Light became the default). |
| "Get more programs" | The shell row is seen. The store itself (GNOME Software) is not seen anywhere. |
| Wishes | Not seen. `docs/shots/wishes/` does not exist. Read from `wish.c`. D14: not in the v0.1 image. |

Part 3 of my brief is to repeat this after the restyle lands. The wizard and
website have landed; staging, first run and desktop have not. That re-run is
planned at the end of this file.

## 1. The three people

They are not types. Each one has a bad hour in a specific place.

**Edith, 85. Presbyopia and a hand tremor. Afraid.**
Widowed, lives alone, a Windows 10 laptop her grandson set up in 2019. Uses it
for email, photos and a weekly video call. Reads every word, slowly, at arm's
length, with glasses she has to push up. Her hand drifts: a press that should
be one click is often two, and a small target is a miss. She has been told all
her life not to click on things. When a screen says "unknown", "risk" or
"changes", she stops and waits for someone. She does not explore. If nobody
comes she does not come back to it. She has never read a settings page.

**Ray, 72. Windows 10 habits. Impatient.**
Retired surveyor. Fast with a mouse, double-clicks everything, expects an X at
the top right of every window, a Start button at bottom left, right-click
menus and Alt+Tab. Reads headlines, not sentences. Clicks Next until it stops
(this is the exact habit `docs/EASY.md` names). Will walk away from a progress
bar for two minutes, then come back and press things. Believes he can fix
things himself, which is the dangerous trait: a "for someone who knows
computers" page is addressed to him in his own mind.

**Margaret, 78. iPhone user. Has hardly used a PC.**
Lives on her iPhone: tap, swipe, pinch, no files, no windows. Her PC was her
husband's, sits in the spare room, gets switched on at Christmas. She does not
know which Windows she has, what a "folder" is, or where Downloads lives. Her
email is on her phone. She would arrive at the website from a link someone
texted her, on the phone. She knows what an app is and what the cloud is. She
does not know what "the internet" is as distinct from "Facebook".

### The questions asked at every screen

The four classic ones: will she try to do the right thing, will she see that
the right thing is available, will she connect the control with the effect,
will she see that it worked. Four more, because of who she is:

1. **Stop.** Is there a point where she would put the mouse down?
2. **Misread.** Is there a word or number she would take the wrong way?
3. **Mis-click.** Is there a target she would miss, double-press or press early?
4. **Fright.** Is there a sentence or a screen that would make her think she
   broke it?

## 2. The journey, as screens

| # | Stage | Screens walked |
|---|---|---|
| A | Website | hero (desktop, phone, Mac), three steps, four looks, small print, questions, print page, helper page |
| B | The file and the warnings | browser download warning, SmartScreen "More info" / "Run anyway", UAC, antivirus, Smart App Control |
| C | The wizard | welcome and check, "A few things to sort out first", "can't go on", look picker, Ready, progress, Ready to restart, stopped, the X |
| D | The restart | Windows shutdown, the dark gap, GRUB, staging (installing, refused, restore) |
| E | First start | "Does everything work?", its checks, Yes, No, "Bring my files across" |
| F | The desktop | four looks, footer (Help, Wi-Fi, Settings, Smaller, Bigger, Turn off), Help pages |
| G | Get more programs | the shell row, the store, what she can and cannot get |
| H | Wishes | "Make a wish", consent, ask, confirm, My wishes |
| I | Going back | "No, go back to Windows", Settings, "Put Windows back" |

Predicted unaided finish, from this walkthrough alone: **0 of 3**. Edith stops
at B (the Edge picture does not match the Edge she has). Margaret stops at A
(the link is on the wrong device). Ray gets furthest and is most likely to
finish, and most likely to lose data by switching off during D. These
predictions are what the first three sessions test.

## 3. Findings

Format: id, severity, who, where; what happens; the fix; the owner.

### A. The website

**A-01 · S1 · M [E] · "Open this page on the Windows computer", phone and Mac**
Where: `docs/shots/website/index-phone-fold.png`, `index-mac.png`;
`website/assets/door.js:72`, `website/index.html:61`.
What: Margaret opens the link on her iPhone. The page tells her to send
herself the link. "Email me the link" is `mailto:?subject=...` with **no
recipient**: Mail opens with an empty To line and she must type her own
address. Then she must open that email on the PC, where she has no email
set up (it is on her phone). The journey ends here, and nothing on the page
says what else she can do. The printed alternative is the helper page, which is
not for her.
Fix: put the short address big on the page ("On the Windows computer, type:
...") once WP-15's domain exists; add "Text me the link" (`sms:`); fill the To
line from nothing, but add "Print this page"; say "Ask someone to open this
page on your Windows computer" as a third choice.
Owner: D (OPS for the short address).

**A-02 · S2 · E, M · "For Windows 10 and 11"**
Where: `index-1366x768-fold.png`.
What: She does not know which Windows she has, and the hero is where she
decides whether this is for her. The reassurance ("Setup checks first, and
changes nothing if it can't go on") is further down, under Good to know.
Fix: one line under the button, same size: "Not sure which Windows you
have? Press Get AurOS. Setup checks, and changes nothing if it can't go on."
Owner: D.

**A-03 · S3 · all · The slashed zero**
Where: "Windows 1Ø and 11", "1Ø to 25 minutes" on every website screenshot;
the wizard's Ready page ("4Ø GB", "1Ø7 GB", "1Ø to 25 minutes",
`wizard/4a-ready.png`); the printed address "v0.0.1" on `guide-1366x768-full.jpg`.
What: Atkinson Hyperlegible's zero carries a slash. `docs/DESIGN-DIRECTION.md`
section 3 already shipped it "upstream first" and flagged a plain zero as P2
because it "is unfamiliar to her". It is on every number she will read. At 20
px with presbyopia a slashed zero can read as an 8 or a different letter, and
"1Ø" is a word she has never seen.
Fix: the plain-zero font script (DD P2) before the first session; until then,
ask in the session "what does this say?" on a number.
Owner: C (font), then D and B pick it up.

**A-04 · S2 · R [E] · "About 10 to 25 minutes"**
Where: hero line, `wizard/4a-ready.png`, `docs/research/journey-audit-2026-10-23.md` section 3.
What: the audit estimated (from the code, not measured) 33 to 50 minutes on a
good SSD laptop and hours on a hard disk, before the fast path. The number on the page is an estimate, not a
journey-clock median (WP-40 is still to do). Ray gives it 25 minutes, then
starts pressing things; Edith turns it off at minute 30 "because it has hung".
Fix: stamp the number from the e2e journey clock; say what part is dark:
"About N minutes. The screen goes dark for some of it."
Owner: D, B (display), F (the measurement), A2 (the engine time).

**A-05 · S2 · E, M · Nothing carries her from the button to the steps**
Where: `website/assets/door.js` (no scroll or banner after the download starts);
`index-1366x768-full.jpg`: the steps start about 1,000 px below the hero.
What: She presses Get AurOS. The browser pops a warning at the top right.
Step 1 ("Download it", with the picture of that warning) is two screens down,
and she does not know to scroll. She is looking at the same hero.
Fix: after the press, scroll to step 1 and mark it "You are here: your browser
is asking if you trust the file", or make the hero button open a short page of
exactly the three warnings.
Owner: D.

**A-06 · S2 · E, M · "Stuck? Show a helper" is not for her**
Where: `website/index.html:104,162,204`; `guide-1366x768-full.jpg`.
What: She presses it while stuck. It opens "A page to print. For whoever is
helping. Fill in the two blanks, print it..." with a "Their phone:" line. It
helps a helper who is already there; it does nothing for her. "Show a helper"
reads as "show it to a helper", and she has none. Print needs a printer she
may not have set up.
Fix: rename to "Stuck? Ask someone to help", and make the first screen of that
page what she does: three sentences ("Close what is open. Nothing has
changed. Show this page to someone you trust"), then Print.
Owner: D, K.

**A-07 · S2 · R · "Programs made for Windows don't run" is buried**
Where: Good to know and Questions, 7,000 px down; `index-1366x768-full.jpg`.
What: Ray runs Office, a banking program and a photo editor. He learns that
none of them run after he has installed. EASY rule 9 (tell her before) is met
in the wizard by nothing yet (WP-17 not landed).
Fix: one sentence in the hero ("Windows programs don't come with you; here is
what replaces them") and the same on the wizard's Ready page until WP-17 lands.
Owner: D, B.

**A-08 · S2 · E · The pictures of Setup are stale and unreadable**
Where: `index-1366x768-full.jpg`, step 3; compare `wizard/1c-welcome-ready.png`.
What: the three pictures show the **dark** Setup from before the restyle; the
real Setup is light. Each picture is 600 px for a 1,120 px window, so its text
is about 10 px. She sees a dark window in the page and a light one on her
screen and doubts it is the same program. The "Does everything work?" picture is
a hand drawing, not a screenshot (no shot of the real panel exists).
Fix: re-shoot from `docs/shots/wizard/`; keep the numbered rings and the
sentence under each picture, which are what she reads; get C to commit a real
shot of the first-run panel.
Owner: D, B, C.

**A-09 · S3 · E · "Will I lose my files? No." beside "keep a copy of the photos you treasure"**
Where: Questions and Good to know, `index-1366x768-full.jpg`.
What: an absolute answer and a hedge, a page apart. WP-40 already wants the
first softened.
Owner: D.

**A-10 · S3 · E, M · The printed address**
Where: `guide-1366x768-full.jpg` ("aarohkandy.github.io/v0.0.1-auros").
What: a stranger's account name and a version number, to be typed from paper.
Owner: OPS (WP-15), then D.

### B. The file and the warnings

**B-01 · S1 · E, M · S2 · R · The Edge picture does not match the Edge she has**
Where: website step 1, second Edge picture ("Make sure you trust ... Delete,
Show more, 4 Keep anyway", `index-1366x768-full.jpg`); real Edge in the
release asset `e2e-39902908530` (`e2e-evidence-base.zip`, `flow/shots/0014`,
`0016`, `0017`).
What: on Windows 11 the second Edge panel has **Cancel** and a dark primary
**Delete with a chevron**; there is no "Show more" and no visible "Keep
anyway" (it sits behind the chevron). The e2e driver, which can read the
accessibility tree, spent eight minutes and failed on exactly this
(`FAILED: the download never finished`, `timings.md`). The dark Delete is the
most obvious button on the screen: Ray presses it, the download is deleted, and
the whole journey restarts. Edith waits.
Fix: redraw the Edge, Chrome and Firefox pictures from real current screenshots
(F already has them) and date each ("shown for Edge version N"); add the text
fallback "If the buttons look different, press the three dots, then Keep"; keep
a small library so a browser update is caught by the next e2e run.
Owner: D (pictures), F (the evidence and a freshness check).

**B-02 · S1 · E, M [R] · Antivirus takes the file**
Where: not on the site at all; `docs/research/journey-audit-2026-10-23.md` B2
(estimated 25 to 40% of PCs in this age group have an OEM trial of Norton,
McAfee or Avast).
What: a red "Threat blocked" box from a program she trusts more than us. No
numbered circle helps. She stops and calls someone, or deletes it.
Fix: a section "If your antivirus took the file" with a picture per big brand
(WP-40), and screen recruits for their antivirus before a session.
Owner: D, K.

**B-03 · S1 · E · Three alarms in a row**
Where: website steps 1 and 2: Keep, Run anyway, Yes.
What: each box says "isn't commonly downloaded", "prevented an unrecognized
app", "unknown publisher ... make changes to your device". The first teaches
her it is dangerous; by the third she has stopped. The site does say "Press
Keep only if you got this file from this page a moment ago", which is right.
Fix: say once, before step 1, "Windows will ask three times. The answers are
Keep, Run anyway, Yes. Nothing else." With D12 (SignPath) the third box
changes to "Verified publisher", but **reputation may take weeks to build even
after signing (unverified assumption: check on the first signed build)**, so
sessions must run on the warning path.
Owner: D.

**B-04 · S2 · E · Tiny targets inside other people's dialogs**
Where: the Edge "..." button (24 px), SmartScreen "More info" (a small text
link): `0013-click-edge-keep.png`, website step 2.
What: with a tremor she misses, or presses twice and gets the wrong item.
Nothing we draw can enlarge them.
Fix: add the keyboard way on the helper page ("Press Tab until it is
outlined, then Enter") and let the helper do these three presses; watch it in
the session.
Owner: D, K.

**B-05 · S1 · M [R] · A standard Windows account**
Where: UAC, website step 2, `fix_win.c` "not-elevated".
What: on an account that is not an administrator, UAC asks for an
administrator's password. Margaret does not know there is one. Setup's own
"installer wasn't given permission" page is never reached.
Fix: next to the UAC picture: "If Windows asks for a password, stop. It needs
the person who set up this computer."
Owner: D.

**B-06 · S2 · R · Smart App Control**
Where: the amber box in step 2 ("AurOS can't go on this computer yet").
What: correct and honest, but Ray reads "can't go on this computer" and quits,
when it is a setting on a new Windows 11. Fixed for good only by D12.
Owner: D (wording while unsigned).

### C. The wizard

The wizard is the strongest surface in the journey: one question per screen, 20
px body, 48 px targets, one green button, the reassurance "Nothing on this
computer has been changed" repeated where it matters. The findings are small
and one of them is about data.

**C-01 · S2 · all · "Fix these for me" restarts Windows without saying "save your work"**
Where: `wizard/2a-blocked-fixable.png`; `fix_win.c` pending-reboot line "We
restart Windows."
What: she presses the green button to fix three things. One of them restarts
Windows. Nothing above the button says "Save what you have open first". Her
email draft or her spreadsheet is lost if she picks "restart anyway" on
Windows's own screen. This is a **zero-data-loss** item for the people test.
Fix: above the button, "Windows restarts when you press this. Save anything
you have open first." Only when a restart is among the fixes.
Owner: B.

**C-02 · S2 · E, M · The look picker names do not mean anything to her**
Where: `wizard/3a-style.png`, `small-3a-style.png`.
What: four name-only cards over abstract line drawings. The one-line
description appears only for the chosen card. The page says "You chose Like
Windows" before she chose. Margaret has no Windows or Mac habit and cannot pick
from "Like Windows" and "Like a Mac"; Edith does not press a card to find out
what it means (she does not explore).
Fix: under each name, one line, always visible ("Big buttons. Nothing ever gets
hidden."). Replace "You chose" with "Picked for you: Like Windows. Press
another to change." Measure which look each person picks and which one she can
then use.
Owner: B.

**C-03 · S1 (conditional) · R, E · The time**
Where: `wizard/4a-ready.png` "It takes about 10 to 25 minutes", `4b` "30 to 60
minutes. This computer has an older, slower kind of drive."
What: see A-04. The wizard is where the number matters most: if the install
runs 40 minutes against a 25-minute promise, the person who switches the
computer off is the person who loses data.
Fix: as A-04. The wizard can already tell SSD from hard disk (`4b`); keep that,
and show the measured range.
Owner: B, A2, F.

**C-04 · S3 · all · Numbers she cannot use**
Where: `wizard/4a-ready.png`: "AurOS uses 40 GB. Windows keeps 107 GB free." "It
downloads about 1.8 GB".
What: GB means nothing to Margaret and cannot decide anything for Edith; what
she needs to know is that there is enough room and that it needs the internet.
Fix: one sentence ("There is enough room. AurOS needs the internet for about
N minutes.") with the numbers under "Show details".
Owner: B.

**C-05 · S3 · all · "Press No"**
Where: `wizard/4a-ready.png` ("Press No, and the computer goes back to
Windows") against the real button "No, go back to Windows" (`welcome.c:200`).
Fix: use the button's exact words.
Owner: B.

**C-06 · S3 · R [E] · "Step 3 of 4" beside a sidebar whose fourth step is the highlighted one**
Where: `wizard/5a-progress.png`.
What: two "of 4" counts on one screen: the sidebar (Check, Pick, Ready,
Install) and the sentence "Step 3 of 4: Downloading AurOS". She sees "3 of 4"
next to a highlighted 4.
Fix: "Part 3 of 4" or drop the sidebar's count while installing.
Owner: B.

**C-07 · S2 · R, E · The X is a stock Windows dialog**
Where: `wizard.c:4005,4021` (`MessageBoxW`, Yes/No, small stock buttons);
DD B-15 asks for an in-window page and it is not done.
What: closing during the install asks "Stop setting up AurOS? ... Yes / No".
Closing on "Ready to restart" asks "Close AurOS Setup and take AurOS back? ... To
keep it for your next restart, choose **No**, then press Restart later." A
question whose safe answer is No, with buttons she cannot hit, in the one place
Ray will click when he gets impatient.
Fix: the in-window page DD B-15 specifies: "Keep it and restart later" /
"Close and undo".
Owner: B.

**C-08 · S1 (data) · all · "Restart later" hands the dangerous part to an unsupervised moment**
Where: `wizard/5c-ready-to-restart.png`: "Restart later: AurOS Setup closes.
AurOS goes on next time you restart."
What: it does not say that the next start will run the install (which shrinks
Windows, 5 to 45 minutes) and must not be switched off. She shuts down for the
night. Tomorrow the computer starts "by itself" into a screen she has not
been warned about, and she does what anyone does to a screen that looks stuck.
Fix: "Next time you switch on, AurOS Setup finishes first. That takes about N
minutes. Don't switch off." And the same sentence on the staging screen.
Owner: B (wizard), A2 (staging).

**C-09 · S3 · E · "Ready to restart" arrives silently**
Where: `wizard/5a-progress.png` ("You can walk away. When you come back,
press Restart now.").
What: she walks away; the page changes; there is no sound or notice. If the
screen has gone dark she is told to move the mouse, which she then does.
Acceptable; note for the session timing (how long until she comes back).
Owner: B.

**C-10 · S2 · R · "Show a helper how" and Ray**
Where: `wizard/2b`, `2c`, `2d-blocked-secure-boot-helper.png`; `website/advanced.html`
(a BitLocker section with `manage-bde -protectors -disable C: -RebootCount 3`).
What: the button is meant for a helper and Ray will press it himself. The page
opens with "If that isn't you, it is fine to stop here". Ray believes it is him.
A wrong BitLocker or firmware step is a recovery-key screen and a lost
evening, and on a PC with no backup, worse.
Fix: a first block on the helper page: "Stop here unless your files are copied
and you have the BitLocker recovery key written down. Show this to someone who
does." Watch what Ray does.
Owner: K.

**C-11 · S2 · E, M · The BitLocker stop (until WP-50)**
Where: `wizard/2g-blocked-encrypted-hard-disk.png`: "Windows has locked this
computer's drive".
What: she reads "locked" and thinks she locked it or forgot a code. Newer
laptops hit this page often (journey audit estimates 40 to 60% of clean
Windows 11 24H2 installs). D13 puts BitLocker PCs in v0.1, so this page should
mostly go; until then it is the commonest dead end on a new laptop.
Fix: while it exists, add "This is a Windows safety feature on by default. It is
not a password you forgot." and "AurOS will work on locked drives soon".
Owner: B, A2.

**C-12 · S3 · all · The Next button looks live while the check runs**
Where: `wizard/1a-welcome-checking.png`.
What: green and pressable, but nothing happens (a line explains beside it:
"Still looking at this computer."). A tremor press early is harmless; Edith may
still read nothing happening as a hang.
Owner: B (watch it).

### D. The restart and the staging screens

Everything in this stage is dark and technical, and it is the stage where a
person with a finger on the power button can lose her files.

**D-01 · S1 (data) · E, R · Minutes of black, with no words about how long**
Where: `wizard/5c-ready-to-restart.png` says "the screen goes black" and not
how long; `docs/research/journey-audit-2026-10-23.md` section 1D to 1E (45 to
75 s on an SSD, up to 120 s on a hard disk, before the staging screen appears).
What: Windows shuts down, the maker's logo, a text GRUB menu, black, then
staging. Roughly one to two minutes with no AurOS words on screen (an estimate: time
it). Margaret thinks it
crashed; Ray presses the power button.
Fix: "The screen goes black for a minute or two. Don't press anything." on 5c,
and the same on the first line of the staging screen. A printable sheet of
what she will see (K).
Owner: B, A2, K.

**D-02 · S2 · R · The GRUB menu**
Where: `rootfs/usr/lib/auros/grub.cfg.in:4-5` (3-second menu on every start) and
`:84-118` (entries "AurOS (verbose boot)", "AurOS (aurinit, experimental)",
"AurOS (recovery shell)").
What: a text menu with those names flashes on every start. A key press stops
the timer, and Ray presses keys. "Recovery shell" is exactly what EASY rule 1
forbids as a path.
Fix: hide it unless a start failed or she holds a key (WP-23: move expert
entries to a submenu).
Owner: BT.

**D-03 · S1 (data) · E, M · Machine output on the screen at the riskiest moment**
Where: `docs/shots/staging-installing.png` (a test VM, so the size is tiny; a
real PC prints its own): "windows  the Windows drive is now 8 MB" and "Copying AurOS onto this computer.", ~12 px, grey; `staging-restore.png`:
"vda6 48 MiB vfat", "checking the saved copy on /dev/vdb";
`src/aurscreen/screen.c` `NTAIL` (the raw tail lines); `install.c:578`.
What: the one line in the log about Windows's size reads like a disaster to a
person who is not supposed to be reading it. DD section 6 rule 10 forbids it
and C's P0-3 is pending.
Fix: no log lines by default; "Step N of M" and one sentence; the technical lines
only behind "Show details for a helper".
Owner: A2 (C for `screen.c`).

**D-04 · S1 · E, R · The longest step has no movement**
Where: `docs/shots/staging-installing.png` (steps with no estimate);
`screen.c` has no ETA; the audit's E4 and E6 ("reading every sector", "making
room") were the quiet ones. `7e874e7` shrinks the work; the screen is unchanged.
What: a screen that does not change for ten minutes under a bar that says "Do
not turn the computer off" is read as frozen.
Fix: "Step N of M, about N minutes left", and "It can look still. Nothing is
wrong." (DD C P0-3).
Owner: A2.

**D-05 · S1 · E, M · The red failure screen**
Where: `docs/shots/staging-refused.png`; `src/aurstage/install.c:109-110`.
What: "AurOS was not installed. Turn this computer off and on again. Windows
will start as usual -- nothing on it has been changed. To try again, open
AurBridge in Windows and press Start installing." Four problems: "AurBridge"
is a name she has never seen (the program is "AurOS Setup"); "Start
installing" is not a button anywhere (it is "Install AurOS"); "--"; and "off
and on again" asks her to hold a power button for several seconds. There is no
automatic return to Windows, and when she gets there nothing explains what
happened (WP-16's "How it went" page does not exist yet), so she must find the
.exe in Downloads again. The last line is a machine verdict
(`aurstage-report v1 verdict=image-damaged ...`).
Fix: WP-13 (restart into Windows by itself after 60 seconds, a QR code and a
six-character code instead of the verdict) and WP-16; in the words, "AurOS Setup"
and "Install AurOS".
Owner: A2, B, K.

**D-06 · S2 · E · The restore screen uses the same words at her most vulnerable moment**
Where: `docs/shots/staging-restore.png` ("Checking the copy of how Windows
started that was saved before AurOS went on"; device paths).
Owner: A2.

**D-07 · S3 · all · Staging is still dark and teal, the wizard is light and green**
Where: `screen.c:60-66`.
What: after Restart now the program changes character completely. A person
who has just trusted a light green screen meets a black one with teal text. It
reads as "something else has taken over".
Owner: A2 (C's drawing code).

### E. The first start

**E-01 · S1 · E, M · "Can you see your photos?" before the photos have come across**
Where: `src/aurshell/welcome.c:488` (the question), `:562` (`A_FILES` opens the
files program), `:196-204` (`Bring my files across` is offered only after Yes,
`W_CONFIRMED`); commit `e9d1ba0` hides internal NTFS drives from the file
manager. **Inferred from code; check on a booted image.**
What: the first of two checks sends her to "Your files", which shows her AurOS
home folder, which is empty. The Windows drive is hidden, and the copy runs
only after she has pressed Yes. She sees no photos, answers "No, go back to
Windows", and the install she waited an hour for is undone.
Fix: make the first check say what is true ("Can you see your Windows photos?")
and show them read-only from a "From Windows" folder; or move the photo check
after the copy. The session task "find a photo" tests this directly.
Owner: C, R.

**E-02 · S1 · E, M · "Does the internet work?" on a laptop with no cable**
Where: `welcome.c:489` (the check), `src/aurbridge/wizard.c:1302` (`wifi_offered()`
is 0 until `AB_CHOICE_BRING_WIFI` is defined, and it is not defined anywhere),
`src/aurshell/net.c` (the Wi-Fi panel types a key).
What: Her Wi-Fi did not come across (D10 is built but switched off). She presses
"Open Internet", gets a browser error page, and has nowhere to go: the Wi-Fi
button is in the footer and nothing on the question points at it. If she finds
it, she must type a 12 to 20 character key from the router with a tremor.
Fix: A2 to switch D10 on. In the meantime the check should say "Not connected.
Press Wi-Fi" and open the Wi-Fi panel.
Owner: A2 (the engine), C (the check).

**E-03 · S2 · R, E · After "Open Your files" the question comes back only when she closes the window**
Where: `welcome.c:130-141` (`welcome_step`); `foot.c:78-86`.
What: Ray looks for an X; in five of six looks only the footer's **Close this**
closes a window. Until it is closed the question is gone.
Edith never finds the way back.
Fix: keep the question visible as a strip ("Back to the question") while the
check window is open.
Owner: C.

**E-04 · S3 · all · The question is abstract**
Where: `welcome.c:372` "Does everything work?" / "Check two things first."
What: well designed, with separate confirm pages and "Let me look first" in the
safe middle. The risk is the word "everything". Counted as a hypothesis: ask
each person what she thinks Yes does.
Owner: C.

### F. The desktop

**F-01 · S1 · E, [R, M] · "Bigger" does not make her programs bigger**
Where: `src/aurshell/foot.c:389` ("Smaller and Bigger change the size of the
words everywhere"), `foot.c:633-646` (it writes `~/.config/auros/text-scale`);
`git grep` finds no reader of that file outside the shell, no GTK text scaling,
no `Xft.dpi`, no Firefox `layout.css.devPixelsPerPx` anywhere in `src`, `rootfs`
or `build`. The help index tells her instead to "hold down Ctrl and press +".
What: Edith presses Bigger. The bar, the menus and the question grow. Internet
(Firefox), the store and every document stay exactly as small as before. The
task "make the words bigger" (the second most natural thing she will try)
fails in every program she actually reads, and the footer says it works
"everywhere". It is also a claim a test should hold (WP-41).
Fix: one text-scale file read by GTK (text-scaling-factor), Xwayland (Xft.dpi)
and Firefox (a policy preference), plus the shell; or change the sentence until
it is true and put "Make this page bigger" where she reads.
Owner: C, S2 (Firefox policy), P (GTK).

**F-02 · S2 · E, R, M · The first screen of "Like Windows" is an empty wallpaper**
Where: `docs/shots/shells/taskbar.png`, `docs/shots/website/index-1366x768-fold.png`
(the website's picture keeps the menu open).
What: the look she is most likely to keep, because it is pre-picked, greets her
with a blank desktop, one small "All my programs" button and a small grey hint
("Nothing open yet — pick something from All my programs"). The website's
picture has the menu open. Windows users have icons. Edith waits; Margaret
touches the glass.
Fix: open "All my programs" on first run, or put Internet, Your files, Photos
and Get more programs on the desktop as big tiles.
Owner: C (D redraws the picture to match).

**F-03 · S2 · R · Windows habits that do nothing**
Where: `src/aurshell/foot.c:78-86` (a window's close lives in the footer in
five of six looks), `taskbar.c:1067` (the Windows key opens the menu: only in
that look).
What: no X at the top right in five looks; right-click, Alt+Tab and a Recycle
Bin are unverified (not found in the source). Each is a missed expectation, not a fault. Ray will press them
all in the first five minutes.
Fix: log them in the sessions; decide per look which the default look must
honour (the X in "Like Windows" exists).
Owner: C.

**F-04 · S2 · M · "Photos" is not on the Home of "Big and simple"**
Where: `docs/shots/shells/rail.png` (Internet, Your files, Documents, Notes,
All my programs, Get more programs), `rail-all-programs.png`;
`shellcommon.c:823-854` (the Home keeps places for the store and, since
`d23f421`, for Make a wish, so four are left for everything else).
What: Margaret's first thought is "photos". It is under "All my programs", and
"Your files" is a word she has not used. Make a wish now takes a place Photos
could have had.
Fix: Photos on every Home; fold Documents and Notes into one row.
Owner: C.

**F-05 · S3 · E · Footer text is 15 px and grey**
Where: `foot.c:35` (`FOOT_MIN_PX 15.f`), `FOOT_TARGET 44`; DD says 18 px and 48.
What: "Help Wi-Fi Settings ... Smaller Bigger Turn off" is the one band that must
be readable on a screen she can no longer read.
Owner: C.

**F-06 · S3 · E · No debounce in the shell**
Where: DD input rule (700 ms after a page change, 500 ms on the same control)
exists in the wizard (`938ea78`), not in `src/aurshell/main.c`.
What: a tremor second press can land on whatever the first one opened. The
power question appears away from its button (`foot.c:177-186`), so the obvious
case is safe; others are not tested.
Owner: C.

### G. Get more programs

**G-01 · S2 · all · The store is not ours**
Where: the shell row is seen (`shells/*.png`); the store itself is GNOME
Software and is not seen anywhere. `build/forge:395-420`; `software-ready.sh`.
What: its screens carry words EASY rule 2 forbids ("Flathub", "runtime",
"verified", "sources", "updates"), small type that Bigger does not touch (F-01),
and an empty catalogue until the first online run (`software-ready.sh`:
Flathub is added and refreshed "the first time it is online"). No picture exists
to check what she sees.
Fix: take screenshots of the store at first open and after a search; hide
sources and updates chips; give it a large-text setting; show a plain "Getting
ready, about a minute" while the catalogue loads.
Owner: C, P.

**G-02 · S1 (expectation) · M, R · The program she wants may not be there**
Where: the website says "Get more programs has many more, free" and "You can use
Microsoft Office on the internet". Parity is not yet proven
(`docs/PLAN-END-TO-END.md` WP-34, WP-56).
What: Margaret's grandson calls on WhatsApp or FaceTime; neither is a program in
the store. Zoom, Meet and Jitsi work in the browser. The page about video calls
(`rootfs/usr/share/auros/help/video-calls.html`) starts "Someone sends you a
link": it assumes an email on this computer (see A-01) and does not mention the
camera and microphone question until a call fails.
Fix: say on the website which video calls work; PARITY proves a Jitsi call with
a camera; the session task "make a video call to your grandson" is run twice,
once with a link in an email and once with the program he uses.
Owner: D, K, PARITY.

**G-03 · closed since the audit.** Programs appear in the list at once after
install (`12d5201`) and Flathub is on the image (`build/forge:395-420`).

### H. Wishes (read from `wish.c`; not rendered; not in the v0.1 image, D14)

**H-01 · S2 · all · The consent screen is a legal text**
Where: `src/aurshell/wish.c:83-94` (five sentences naming Cloudflare,
Anthropic, Claude, "the United States", "up to 30 days, or up to 2 years if its
safety checks flag them").
What: Edith's real question is "is someone listening to me?" The text answers a
regulator's question. She presses "No, keep Wishes off" or, worse, "Yes, turn on
Wishes" without reading.
Fix: three sentences in her words with the choice first, the names behind
"More". The family-first cohort (D14) is the right place to read what she does.
Owner: W2, K.

**H-02 · S2 · E, M · "Make a wish" sits among the programs**
Where: `apps.c:722-735` (hint "Tell this computer what to do"), `shellcommon.c`.
What: she reads a wish as a prize, a lottery, a gift. Ask in the session what
she thinks it does before she presses it.
Fix: the hint is right; make it the label's second line on every Home.
Owner: W2.

**H-03 · S3 · all · Six examples, one of them nonsense**
Where: `wish.c:49-54` ("Fill my screen with potatoes when I type potato").
What: it is the owner's own example and funny to him. To Edith it is a computer
that fills a screen with potatoes.
Owner: W2.

**H-04 · S3 · E · Voice**
Where: `WT_STOP_LISTEN` "I've finished", up to 30 seconds, tap to start and stop.
What: a double press stops it before she speaks; she hears "I didn't quite
catch that". Fine; test with a tremor.
Owner: W2.

### I. Going back

**I-01 · S1 (data) · all · Putting Windows back loses what she saved in AurOS**
Where: `rootfs/usr/share/auros/help/windows.html` ("Anything you saved in AurOS
goes with it"), `settings.c` ("This removes AurOS and everything saved in it."),
`docs/PLAN-END-TO-END.md` WP-27 (reverse Ferry copies her files into a "From
AurOS" folder first; not landed).
What: it is honest, and it is still a loss. A grandchild's photos saved into
AurOS, or the cards she copied from her phone, are gone. This is the biggest
single threat to "zero data loss" in the task "get back to Windows".
Fix: WP-27 before the first session. Until then the session script must ask the
person to save nothing in AurOS before the task.
Owner: R.

**I-02 · S2 · E · Two things called "go back"**
Where: "No, go back to Windows" (keeps AurOS, `welcome.c:200`), "Put Windows
back" and "Remove AurOS and put Windows back" (removes it, `settings.c:588`,
`:362`), "Going back to Windows" (help).
What: after "No" she is in Windows with AurOS still on the disk and no way to
reach it again except "ask someone who knows computers" (`windows.html`).
Three names for two different things breaks the one-name-per-thing rule.
Fix: "Go back to Windows (for now)" and "Remove AurOS". Offer a Windows-side
"Try AurOS again" when "No" was pressed (WP-16 and WP-77).
Owner: K, C, B.

## 4. The fifteen that matter most

Ranked by how many of the three stop and by whether data is at stake. The
sequence is the one the lead asked for: this is what to fix before the first
recruit sits down.

| # | Id | S | Who | What | Owner |
|---|---|---|---|---|---|
| 1 | D-03 | 1 data | E, M | Staging shows "the Windows drive is now 8 MB" and device paths in 12 px at the riskiest moment | A2 |
| 2 | D-04 | 1 | E, R | The longest staging step has no movement and no estimate, so she switches it off | A2 |
| 3 | D-01 | 1 data | E, R | One to two minutes of black after Restart now, never named | B, A2, K |
| 4 | C-08 | 1 data | all | "Restart later" hands the install to an unsupervised moment with no warning | B, A2 |
| 5 | D-05 | 1 | E, M | The red failure screen says "AurBridge", "Start installing", "off and on again", with no way back by itself | A2, B, K |
| 6 | B-01 | 1 | E, M [R] | The website's Edge picture is not the Edge she has; the dark Delete is the loudest button | D, F |
| 7 | A-01 | 1 | M | The phone-to-PC link is `mailto:` with no recipient and lands in an inbox the PC cannot see | D |
| 8 | E-01 | 1 | E, M | "Can you see your photos?" asks before the photos have come across (inferred from code) | C, R |
| 9 | E-02 | 1 | E, M | "Does the internet work?" on Wi-Fi-only laptops; her Wi-Fi is not carried (D10 off) | A2, C |
| 10 | F-01 | 1 | E | "Bigger" only enlarges AurOS's bars; the footer says "everywhere"; the task fails in every program | C, S2, P |
| 11 | I-01 | 1 data | all | "Put Windows back" deletes what she saved in AurOS (reverse Ferry not landed) | R |
| 12 | B-02 | 1 | E, M | Antivirus quarantine is on 25 to 40% of PCs and the site says nothing | D, K |
| 13 | A-04 / C-03 | 1 cond. | R, E | "About 10 to 25 minutes" is not measured; a longer run makes the impatient switch off | D, B, F |
| 14 | C-01 | 2 data | all | "Fix these for me" restarts Windows with no "save your work first" | B |
| 15 | F-02 | 2 | all | "Like Windows", the pre-picked look, opens on an empty wallpaper | C |

Also close behind: B-03 (three alarms), B-05 (standard account password),
C-07 (stock Yes/No dialogs with inverted logic), A-08 (stale installer
pictures), D-02 (GRUB menu names), F-04 (no Photos on Home).

## 5. What the sessions must settle

Hypotheses, each a thing to watch for in `docs/usability/SCRIPT.md`:

1. Does Edith get past the Edge warning at all, unaided, with the current picture?
2. Does anyone press Delete (B-01)?
3. What does Margaret do with the link on her phone (A-01)? Run this one on the
   phone, with her own phone.
4. Does anyone read a slashed zero wrongly (A-03)? Ask "what does this say?"
5. Which look does each person leave with, and could she use it ten minutes
   later (C-02, F-02, F-04)?
6. What does Ray do when the screen has been still for five minutes (D-04)?
7. What does anyone do on the black gap (D-01)? Record the exact second the
   hand moves to the power button.
8. Does the first check fail (E-01, E-02)? Record what she answers.
9. Press Bigger and then open Internet (F-01): what does she say?
10. Does anyone choose "Restart later" and what happens at the next start (C-08)?
11. Does anyone read the Wishes consent screen to the end (H-01)?
12. After "get back to Windows" is every file she saved where she expects (I-01)?

Predicted minutes, unmeasured, to compare with the stopwatch tables:

| Stage | Edith | Ray | Margaret |
|---|---|---|---|
| A website | 8 to 12 | 1 to 2 | stuck (0 on this PC) |
| B file and warnings | 10 to stuck | 2 to 4 | not reached |
| C wizard | 6 to 10 | 3 | not reached |
| install wait (shown on screen) | 15 to 40 | 15 to 40, interrupted | not reached |
| D restart and staging | 10 to 45, power button likely | 10 to 45 | not reached |
| E to I first use, store, call | 15 to 25 | 10 | not reached |

## 6. Closed since the journey audit of 2026-10-23

Credit where it is due; each was a stop in `journey-audit-2026-10-23.md` and is
now gone or smaller (checked against the tree, not the commit message):

- The website opens on one button and the steps follow; no checkboxes stand
  between her and the file (`54fd273`).
- The wizard is three screens, no typing, no "Type AGREE", light, 20 px, one
  green button, no countdown (`7fb9b2d`), and a double press cannot skip Ready
  (`938ea78`).
- "Get more programs" is on every home (`shells/*.png`), and a program appears
  at once after install (`12d5201`).
- No shared password (`77adbfe`); her look survives a restart (`482dab2`).
- The Help button opens the help pages (`f3c42b0`).
- The Wi-Fi firmware probe is fixed in staging (`probe.c:327`), so a Wi-Fi-only
  laptop is no longer refused after the shrink.
- The staging shrink takes only what AurOS needs (`7e874e7`).

## 7. The re-run after the restyle (part 3 of the brief)

The wizard and website changes landed in `7fb9b2d` and `54fd273`, which this
pass already reads. Still to land and to re-walk: staging screens (A2, C P0-3),
first run and desktop in Light (C), the Wishes panel in a render (W2),
`docs/shots/website/` step pictures (D). Plan: when `docs/shots/staging-*.png`
and `docs/shots/shells/*.png` change again, write
`docs/research/people-walkthrough-2026-10-restyle.md` with the same ids, a
"now" column (closed, smaller, same, new) and the new top fifteen. Findings
marked "inferred" must be settled by a booted image before the re-run.
