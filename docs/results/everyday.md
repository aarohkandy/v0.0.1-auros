# Can she live on it? The everyday things, from evidence (lane gm-everyday, 2026-11-04)

She: over 80, Windows and a browser, reads slowly, poor eyes, unsteady hands, afraid of breaking it, nobody beside her
(docs/EASY.md). For each thing she does every day: what the evidence says today, and what was changed.

Evidence used, nothing else:

- **J** the Windows 11 journey, build 41780979384 (`e2e` evidence, 54 screenshots): website, Get AurOS, Edge's warnings, the wizard,
  restart, "does everything work?", "Get more programs", VLC.
- **P** the nightly `parity` runs (`gh release view parity-41817427536`; seven nightlies since 2026-10-25): AurOS's own image in a VM
  with a virtual camera, microphone and printer. `tools/parity/README.md` lists what is simulated.
- **C** a container with the same packages as `profiles/desktop.profile` and the file-type step of `build/forge` run in it
  (Ubuntu 24.04, Firefox from Mozilla's repository): what the desktop's file database answers, and what LibreOffice itself reads.
  Not an image; no window, no sound, no hardware.

"Unproven" means nothing above ran it. It does not mean broken.

| she does | state | evidence | what changed |
|---|---|---|---|
| the browser opens, a real site works | works (a page loads in Firefox; the call page showed in it) | P `firefox`, `everyday-call.png`; J ran Get more programs, which needs the net | nothing |
| email in the browser (Gmail, Outlook) | **unproven**: no run has signed in to a mail site. Nothing in the policy blocks one; HTTPS-only is forced, which is right for mail | none | nothing (see "Not in this lane") |
| a link to an email address (`mailto:`) | **broken**: nothing was registered, so a click did nothing | C: `gio mime x-scheme-handler/mailto` said "No default applications" | now goes to Internet (Firefox); what Firefox does with it next is **unproven**; see the first fix below |
| a video call (camera and microphone) | works in the VM with a virtual camera and microphone: both reach the far side, the window shows the picture. The permission question Firefox asks (Allow) is **unproven**: the test turns it off | P `call.webrtc`, `call.visible` | nothing |
| YouTube and films, with sound | sound leaves the decoder for H.264/AAC and for a DASH stream, window shows the picture. **Smooth playback is not**: 57 to 177 of 230 to 290 frames were dropped in 6 of 7 nightlies on a VM with no graphics card (one night: 0 to 39). Real speakers and a real old laptop are unproven | P `film.*`, table in media-perf.md | media-perf.md brought up to date |
| photos from a phone | **unproven**: libmtp and gphoto2 are in; an iPhone has no usbmuxd (read, not tested: docs/reviews/RECOMMENDS-2026-10-24.md). A phone photo is `.heic`, and that opens in Photos | C: `.heic` opens Photos | nothing |
| a USB stick appears | **unproven** on hardware. polkit lets her open and eject one (`49-auros.rules`); the Windows disk is hidden on purpose | rules read, no run | nothing |
| printing | works for a printer on the network and, by the same driverless route, one on a USB cable (`ipp-usb`): found over DNS-SD, added, a PDF arrives. What she presses: Print in the program, pick the printer, Print (help `printing.html`); Printers if it is missing. Older Epson and Brother printers had **no driver**. Printing from Letters is **unproven** (it lists only printers that already exist, docs/reviews/RECOMMENDS-2026-10-24.md) | P `print.discover`, `print.pdf` | Epson and Brother drivers added (0.6 MB); `print.drivers` checks the four brands |
| writing a letter and saving it | Letters opens a Word file and saves it (text and a formula survive). **Every first start opened a "Tip of the Day" window over the letter**; a new letter was saved as `.odt`, and a Word file she opened and saved asked "Use Word 2007-365!" or "Use ODF Format!" (the defaults, read from LibreOffice itself) | P `office.save`, `everyday-office-writer.png` | the window is off; Letters saves a Word file and a table an Excel file, with no question; read back from LibreOffice itself (C, both ways) |
| a PDF opens, a form is filled in and saved | works; saving straight after typing the last box keeps it | P `pdfform.*` (7 of 7 nightlies) | the stale gap line is gone |
| Solitaire | **was not in the image**: it installs from Get more programs (25 s, window "Klondike") | P `apps.solitaire` | in the image, called Solitaire (73 MB) |
| double-click a picture, a PDF, a letter, a table, a song | works for the kinds `mimeapps.list` names. **For every other kind nothing opened**: `.wmv`, `.wma`, `.aac`, `.flv`, `.opus`, `.m3u` and the rest of what the music and video program handles | C: 16 kinds of music and video, 12 of them answered | fixed in `build/forge`, one line; `types` checks 47 kinds by the name her screen shows |
| a program asks to open her file (Word's "open", a browser's) | works: our `xdg-open` hands it to the same place a double-click goes. The nightly said otherwise only because it asked `xdg-mime` | C: original xdg-open red, ours green | `office.types` asks the right thing now |
| Wi-Fi, volume | the band's Wi-Fi button and the taskbar's volume are there (screenshots). Joining a real Wi-Fi network, real speakers: **unproven** | J `0043-store-open.png` | nothing |
| finding her Windows files | the "Bring my files across" screen is there (J `0040-after-start.png`); the journey pressed Not now, so **the copy itself was not walked**. The Windows disk is hidden from Your files on purpose | J | nothing |
| Get more programs | works end to end: search, install with no password, open (VLC, 1:52) | J 0042 to 0054; P `apps.*`: all 10 programs open a window, Zoom and VLC through Xwayland (WhatsApp's stays blank, below) | apps.md brought up to date |

## The two fixes that were not about wording

**Nothing opened for most kinds of file.** `build/forge` renames the programs she sees (Firefox is "Internet", mpv is "Music and
video") by writing copies to `/usr/local/share/applications`, and indexes `/usr/share/applications` only. A copy no index mentions
answers for the kinds `mimeapps.list` names and for nothing else. Measured with the image's packages and the file-type step run as
`build/forge` runs it: 16 kinds of music and video, 12 opened; `.wmv`, `.flv`, `.aac`, `.wma` did not (nor `.opus`, `.m3u`). A link to an email address
opened nothing. After indexing the other directory too: all 16, and `mailto:` has a program (Internet). The same index moved `.svg`,
`.avif`, `.xml` and `.json` to Internet (Firefox), `.ps` and `.djvu` to Documents, `.iso` to Zip files; none of them is a
surprise, none was reachable before.

**Three lines in `known-gaps.txt` had been failing the nightly since 2026-10-25.** `run.py` fails a run when a known gap passes. It
had: `pdfform.last-box` and `firefox.firstrun` passed in all seven nightlies (the Terms-of-Use window is gone since
`SkipTermsOfUse`), and `office.types` was measuring `xdg-mime`, which our `xdg-open` no longer asks. A failed nightly emails the
owner. `python3 tools/parity/selftest.py` replays a healthy night against the file.

## Not in this lane (found, not changed)

- **Firefox has no start page and no bookmarks** (`rootfs/etc/firefox/policies/policies.json`): "Internet" opens Firefox Home, a
  search box. She gets to Gmail by typing an address. The policy can place Gmail, Outlook, YouTube, Facebook and Weather on a
  bookmarks bar (`Bookmarks`, `DisplayBookmarksToolbar`) and name a `Homepage`; the file belongs to the browser lane.
- **Letters is not among the first four programs on the home screen** (`rank_of` in `src/aurshell/apps.c` ranks Documents,
  Letters and Notes alike, alphabetically): she finds it under All my programs. `Documents` is the PDF viewer, which is a
  surprising name for it next to a word processor called Letters.
- **WhatsApp from the store**: its window opens, and in 5 of 7 nightlies the page stays blank (`apps.whatsapp.visible`). It is not a
  known gap, because it passes on some nights. Cause unknown; it may be the VM's software drawing.
- `.github/workflows/parity.yml` line 40 lists the checks in a description; `types` is not in the list (text only).
- Frames dropped on the no-graphics-card VM: nobody has a number for a real old laptop (WP-33).
