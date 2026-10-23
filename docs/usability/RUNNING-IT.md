# Running it: stations, equipment, and what is and is not proven

For the observer and the owner. Read `SCRIPT.md` first. Everything here that
I have not done myself says **not run**. A step that says "proven" names the
test or run that proves it. Where the right answer is "someone else has to build
that", it is in section 5.

## 1. The two modes, and what each needs

| | Mode E: early check (WP-92, three people, now) | Mode F: the real test (WP-43, five people) |
|---|---|---|
| Wizard | the click-through simulation | the real `AurOS-Setup.exe` on her computer |
| AurOS | a spare or virtual machine, already installed, at the first-run question | the one the installer puts on her own computer |
| Her files at risk | none | yes: backup and data check (`SCRIPT.md` section 7) |
| Pass rule | not evaluated; the sheets and the findings are the product | evaluated |
| Needs first | nothing; can start today | WP-33 (five real PCs proven), WP-30 minimal, WP-12, WP-17, WP-26; the first installer on the owner's domain (WP-15) |

## 2. Mode E on the owner's laptop

The laptop is a MacBook Air, Apple M5 (arm64), 16 GB, with UTM installed. Both
stations run on it, or the AurOS machine runs on homebase and the Mac shows it.

### Station 1: the wizard simulation (run in the in-app browser, served locally; not yet opened from a file in Safari)

`docs/usability/wizard-sim/index.html` shows the real screens from
`docs/shots/wizard/` and moves on when she presses where the real button is.
It installs nothing. Every press, hit or miss, is logged with the time.

```sh
cd <checkout>                        # the repository; the pictures are ../../shots/wizard/
python3 -m http.server 8765 --bind 127.0.0.1 &   # the sure way; opening the file directly may also work
open http://127.0.0.1:8765/docs/usability/wizard-sim/index.html   # full screen
```

- Keys for the observer (she never needs them): `1` the happy path, `2` a PC
  that needs fixing, `3` a locked drive (cannot go on), `4` the install stops,
  `H` shows the hot spots, `L` saves the log (CSV: `ms, event, page, x, y,
  label`), `R` starts again, `N` skips a wait.
- What it simulates: the Setup window, its pages in order, Stop and the window's
  **X** (the in-window "Stop setting up AurOS?" and "Close AurOS Setup?" questions
  the real Setup now draws), Restart now, **25 seconds of black** (the real dark
  gap is longer: say so afterwards, do not shorten it for her), then the staging
  picture for 40 seconds. Any press or key during the black or the staging is
  logged as `press-during-dark`: that is the power button standing in.
- What it does not: Edge, SmartScreen and the permission box (steps 1.03 to
  1.06; use the pictures on the website, in
  `docs/shots/website/index-1366x768-full.jpg`, and ask "what do you think this
  says?"), the download wait (it shows the progress page for 25 seconds), and
  anything real.
- **When the shots change** (a restyle), press `H` and check that every box sits on
  its button. If not, re-measure: the buttons are the pixels of colour
  `#0B5D4F` (green, primary) and `#FCFAF5` (card, secondary) in the bottom 130 rows
  of the 1120x690 picture; the numbers live in `HOT` at the top of the file.
- Use her kind of mouse. A trackpad measures a different person.
- The picture is 1120 px wide; on the Mac's default resolution it shows at about
  its real size (the real window is 1120 px). Do not zoom: her text is 20 px.

### Station 2: the AurOS machine, at the first-run question

The first-run question, "Does everything work?", appears only on a machine that
AurOS was *installed onto* and has not been answered
(`src/aurfirst/state.c`: the phase is `asking` only when an AurOS boot entry
exists and neither Yes nor No has been said). A bare boot of the image does not
show it. Ways to get such a machine, best first:

**A. A real spare laptop, installed with the real installer from the first
prerelease** (`v0.1.0-618726b`, an unsigned installer; `docs/TRY-IT.md`: use a PC
whose files you do not need). This is "the owner's pre-installed AurOS laptop"
in the plan. After the install the question appears by itself. Press **Let me
look first**: it comes back at every start, so the machine can be reused for the
next person. *If a participant presses Yes or No the question is gone and the
laptop must be installed again.* Tell her, before, to press neither (Mode E only).
Not run.

**B. A virtual machine with a snapshot at the question** (reverts in seconds,
the machine for repeated sessions). Not run end to end. What is proven:
`tools/firstboottest.sh` is the test that boots the real image under OVMF with
the AurOS entry planted so that the state is `asking` (read its header; whether it
is green in CI is BT's to say). What is not proven: giving that machine to a
human. Two places:

- *On homebase* (KVM, fast; the proven place for the image): the same QEMU
  invocation with `-vnc 127.0.0.1:N` instead of `-display none`, a snapshot taken
  at the question, and the Mac showing it over Tailscale (Finder, Go, Connect to
  Server: `vnc://100.108.213.121:590N`). Follow `docs/handoff/FIELD-MANUAL.md`
  section 3: your own directory under `~/work/auros/`, `hb run`, never `sudo`,
  RAM capped, never `box down`, nothing left running. The Mac screen then shows
  her a window of a computer: a layer of unfamiliarity. Fine for finding words
  that confuse; wrong for timing.
- *On the Mac, in UTM*: the image is x86-64, the Mac is arm64, so UTM has to
  emulate (slow: expect a sluggish desktop; fine for reading and pressing, wrong
  for times). Import the image as a raw disk and boot it with UEFI. Not run; the
  planted entry is the part nobody has done outside the test.

**C. Stills.** If neither works, show `docs/shots/shells/*.png` and the staging
shots as slides in the order of `people-walkthrough-2026-10.md`. Weakest: it tests
whether she understands the words, not whether she can press anything.

## 3. Mode F on her own computer

Bring: a USB drive of at least 128 GB (exFAT) for her backup (`SCRIPT.md`
section 7); a laptop for the sheets (the sheet is paper; the laptop is for the
baseline commands); a phone on a small tripod; a second phone or laptop for
the grandson; a charger for her computer; water, tissues, the printed consent in
18-point type, the thank-you.

- **Recording.** Film the screen and her hands with a phone on a tripod, face
  out of the frame, microphone on if she agreed. Do not install screen-recording
  software on her computer: it changes her computer, and it cannot see the black
  gap, GRUB or the staging screens, which are the part that matters most.
- **The grandson.** A video call room (Jitsi: any name; the link on a card), open on
  two devices in another room, camera on, his name as she says it. The room is
  created before she arrives and left open.
- **Timeline.** T-30: arrive, set up, charger in. T-0: welcome, consent, backup, baseline
  (hash the pictures while you talk). T+20 min: start Clock A. T1 up to 75 min, T2 to T3 up to 25
  more; break; T4, T5, T6; the data check; tea and the interview. Plan on
  three hours door to door.
- **Her computer after.** She has AurOS on it unless T6 put Windows back. Write on a
  large card what is on it now and how to go back, and leave your number.

## 4. Dress rehearsal (no participant until this passes)

Run the whole script on a younger colleague, on both stations. It passes when: the sim's log
file downloads; the scribe can fill a sheet at the pace of the tasks without
missing a step; the observer says nothing outside section 3 of the script; the
backup and the data check commands run and produce a clean diff; the phone sees
the screen and the hands and not the face; the grandson answers; and the whole
thing, with tea, fits in the time you promised her. Fix the script, not the
colleague.

## 5. What I need from other streams (so that the sessions can start)

| From | What | For |
|---|---|---|
| BT / A2 | `tools/usability-vm.sh`: leaves a machine at the first-run question, with a VNC port and a snapshot to revert to. It is `firstboottest.sh` without its clean-up. | Station 2, mode B |
| HW | `AurOS-Setup.exe --report` (WP-32) | screening, section 3 of the recruiting note |
| F | the current Edge, Chrome and Firefox warning pictures from real runs, with the browser version | the website, and the observer's comparison |
| D, B | re-shot step pictures (light) | the sim and the site agree |
| R | reverse Ferry (WP-27) | the data check's last line (finding I-01) |
| PARITY | a Jitsi call fixture with a camera and a microphone | T3, and a Mode E that is not only slides |
| C | a real render of the first-run panel and the Light desktop | the walkthrough re-run |
| lead | name the second responder (WP-83) before the first Mode F session: she may phone in a problem | support |
