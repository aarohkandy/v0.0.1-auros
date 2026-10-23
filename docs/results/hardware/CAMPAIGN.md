# The real-PC test: five computers, in your hands

For the owner. Written 2026-10-23 by stream HW. Plain on purpose.

**Why.** Every test so far ran in a pretend computer. A pretend computer
cannot show the things that actually hurt a person: a firmware that ignores
"start AurOS next" (so she lands in Windows and thinks AurOS is gone), a
Wi-Fi chip with no driver, an encrypted drive, a slow hard disk that turns
ten minutes into two hours. Five real computers show those. The public
link stays shut until they are green (`docs/PLAN-END-TO-END.md`, WP-33).

**When.** Not yet for the install itself: the lead says "go" when the
prerelease installer and the safety tests are green. **Step 1 (the report)
is safe today and can be done on any PC at any time, including a PC you are
thinking of buying (ask the seller to run it) and the PCs of the five
people you recruit for the people test.** The report only looks; it changes
nothing.

---

## 1. Which five computers to get

Used or refurbished is right: this is what her computer looks like. Roughly
$100 to $400 each, about $1,000 for the five. Prices are my guess, not a
quote. Buy from somewhere with returns.

How to read a listing: ask the seller for a photo of **Device Manager**
(Windows key, type "device manager", open it, open "Network adapters"). The
Wi-Fi line names the chip. That line is what decides machines 1 and 4.

| # | What | Why this one | What to ask for in the listing |
|---|---|---|---|
| 1 | **A 2013 to 2016 laptop with a Broadcom Wi-Fi chip and no network-cable socket.** Thin laptops of that time (some Dell XPS 13 and Inspiron models, some HP thin laptops) used Broadcom "802.11ac" cards (names with BCM4352, BCM4360, DW1560). | The one case where we **refuse before anything changes**. The report must say AurOS would have no internet here. Proves the refusal screen on a real one, and that nobody is left stuck. | "Broadcom" in the Wi-Fi line of Device Manager; Windows 10; any size of drive. |
| 2 | **A Windows 11 laptop from 2023 or later, set up with a Microsoft account** (a budget Lenovo IdeaPad, HP 15 or Acer Aspire is fine, new about $350). | New Windows 11 laptops turn on **BitLocker (drive lock) by themselves**. Today every such PC is refused. This tells us how many PCs that is, and later tests the "pause for one restart" path. | New, or "set up with Microsoft account". |
| 3 | **An old desktop or laptop with a spinning hard disk** (not SSD) and 4 GB of memory: Dell OptiPlex, Lenovo ThinkCentre, HP ProDesk 2012 to 2016, Windows 10, with a network cable. | The one-hour promise lives or dies on a slow disk. Also: **no Wi-Fi at all**, the cable path. | "HDD", "500GB" or "1TB" 7200 RPM; Windows 10. |
| 4 | **An AMD Ryzen laptop from 2021 to 2024** (Lenovo IdeaPad 3/5, HP 15, Acer Aspire 5) whose Wi-Fi line says **Realtek RTL8852BE or RTL8852CE** (very common in budget laptops). | Our list says that Wi-Fi works but may drop out. Nobody has checked on the real thing. Also tests AMD graphics and AMD sound. | "Realtek 8852" in Device Manager; or just buy and read the report. |
| 5 | **A business laptop from 2021 to 2024 with a 12th or 13th generation Intel chip** (used ThinkPad T14 / X1, Dell Latitude, HP EliteBook; $300 to $500). | These have the camera on an Intel chip (IPU6) that AurOS cannot use yet: the report must say **"Your camera won't work yet"** and the camera really must not work. Newest Intel Wi-Fi and sound. A very common kind of PC. | "12th Gen" or "13th Gen Core i5/i7", "Windows 11 Pro". |

That is five computers, at least four makers, one with a hard disk (3), one
Broadcom Wi-Fi-only (1), one with BitLocker (2) — which is what WP-33 asks.
If one of them is hard to find, the report from a different PC with the
same trouble does the same job. **A sixth is worth having if you can: any
Surface laptop or a Windows 10 tablet-style PC.** They break more rules
than anything else.

---

## 2. Before you touch any of them

1. **Write the exact model** (sticker on the bottom, or Settings > System
   > About). Photograph the sticker **with your thumb over the serial
   number**.
2. **Do not update or "fix" Windows first.** We want it as a person has it.
   The exception: if Windows says it has an update waiting to restart, restart
   once and write that down.
3. **Make a canary.** One folder with three files you will check at the end,
   to prove we lost nothing. In Windows, open **PowerShell** (Windows key,
   type `powershell`, Enter) and paste these three lines:

   ```
   $d = "C:\Users\Public\canary"; New-Item -ItemType Directory -Force $d | Out-Null
   1..3 | ForEach-Object { Set-Content "$d\file$_.txt" ("canary " + $_ + " " + (Get-Random)) }
   Get-FileHash "$d\*" | Format-Table Hash, Path
   ```

   Photograph the three hashes (the long letters and numbers). At the end
   the same command must print the same three.

---

## 3. Step 1: the report (5 minutes, any time, changes nothing)

1. Download the newest `AurOS-Setup.exe`. It is on the project's releases
   page, https://github.com/aarohkandy/v0.0.1-auros/releases ; take the
   newest one marked **Pre-release** (or the live website once it is up).
   Save it in **Downloads**. Windows will warn that it is an unknown
   program: that is expected for now (`docs/SIGNING.md` shows the clicks).
2. Press the **Windows key + R**. In the little box, paste exactly this and
   press Enter:

   ```
   "%USERPROFILE%\Downloads\AurOS-Setup.exe" --report
   ```

3. Windows asks "Do you want to allow this app?" Press **Yes**. A box says
   where the report is saved (usually the Downloads folder) and that nothing
   was changed. Two files: `AurOS-report.txt` and `AurOS-report.json`.
4. Open `AurOS-report.txt`. Three parts matter:
   - **WOULD THE AurOS INSTALLER ACCEPT THIS PC?** A plain yes or no, and why.
   - **WHAT WILL WORK:** sentences like "Your Wi-Fi will work." and "Your
     camera won't work yet." These are what she would be told.
   - **DEVICES WINDOWS SEES:** the chips, so a human can check the sentences.
5. Rename the two files to the machine's name (`dell-xps-13-9343.report.json`
   for the JSON; keep the text one too) and put them somewhere you can send.

The same five minutes is how to screen a person's PC for the people test
(WP-43): if the report says "NO", find another PC, so the session is not
wasted.

**Printers and scanners:** plug in a printer the machine has used (or any
printer you own) **before** step 1, so the report lists it. A USB printer
and a Wi-Fi printer are different tests: do both if you can.

---

## 4. Step 2: photographs, before the install (10 photos)

Use your phone. One photo each, in this order, named `01-` `02-` and so on:

1. The model sticker (thumb over the serial).
2. Windows key, type `msinfo32`, Enter. Photo of the first page: it shows
   **BIOS Mode** (UEFI or Legacy) and **Secure Boot State** (On or Off).
3. Settings > Privacy & security > **Device encryption** (or "BitLocker").
   Photo of what it says. If neither exists, photo the Settings > System > About page.
4. Device Manager with **Network adapters**, **Bluetooth**, **Cameras**,
   **Sound, video and game controllers**, **Display adapters** and **Other
   devices** all open. Any yellow `!` is important.
5. The report box from Step 1, and the three canary hashes.
6. The back or side of the machine, to show its ports (is there a network
   cable socket?).
7. The Windows desktop, so we know how it looked before.
8. The hard disk or SSD: Task Manager (Ctrl+Shift+Esc) > Performance > Disk.
   It says "HDD" or "SSD".
9. Settings > Windows Update (are updates waiting?).
10. Anything that looks odd.

---

## 5. Step 3: the install (only when the lead says go)

1. Plug the machine in. Plug in the printer.
2. Start a stopwatch on your phone when you press **Install AurOS**. Stop
   it at the screen that asks **"Does everything work?"** Write the minutes.
3. Photograph **every screen the installer shows**, one photo per screen,
   in order. The ones that matter most: any screen that says AurOS can't
   go on, any screen that makes you wait, the restart, anything that asks
   you a question you did not expect.
4. **Say out loud anything that confuses you**, or write it on a sticky note.
   You are the great-grandmother here. If you hesitate for more than ten
   seconds, that is a finding.
5. Do not help the installer. If it stops, photograph and stop.

### Inside AurOS, the checklist (write yes / no / n/a for each)

- [ ] Wi-Fi: connect to your network, open any website.
- [ ] Sound: play the test sound; plug in headphones if it has a socket.
- [ ] Microphone: say something into Settings > Sound > Input; the bar moves.
- [ ] Camera: open the camera or a video-call page.
- [ ] Bluetooth: pair a mouse or headphones.
- [ ] Printer: print a test page.
- [ ] Scanner: scan one page.
- [ ] Fingerprint reader: try it, if the machine has one.
- [ ] Screen: bright enough, brightness keys work, full resolution.
- [ ] Laptop: close the lid, open it, it comes back. Battery shown?
- [ ] Open **Get more programs** and install one (VLC).

Then answer "Does everything work?".

---

## 6. Step 4: Put Windows back (every machine)

1. In AurOS: **Put Windows back**. Follow it; photograph each screen.
2. When Windows is back, run the canary command again (Step 2.3). **The
   three hashes must be identical.** Photograph them next to the first photo.
3. Photograph the Windows desktop.

If anything is missing, stop, photograph, **do not retry**, tell the lead.
A single lost file on any machine stops the whole campaign: that is the
rule that outranks all the others.

## 7. Step 5: the power pull (two machines only, only if the lead says so)

Only after the lead confirms the same cut has passed in simulation
(WP-87). On two of the five: start the install; when the lead's sheet says
"phase N", pull the power cord (and battery if it comes out). Then power on
and see what the machine does. Photograph. Expected: either the install
carries on or Windows comes back untouched. **Not expected: anything else.**

---

## 8. Hand over the results

For each machine, copy `docs/results/hardware/TEMPLATE.txt` to
`<maker-model>.txt`, fill in the lines, and add the `.report.json`. Send the
files and photos to the lead (or any agent): they commit them, run
`python3 -I tools/hw/results.py`, and the table in
`docs/results/hardware/README.md` updates itself. The table says, for every
machine, what the report **predicted** and what **really** happened, and
puts the two bad kinds of mistake in red:

- "works" but it did not (we promised something false);
- "won't work" but it did (we turned someone away who would have been fine).

## 9. What "green" means

All of these, on at least five PCs:

- zero lost files on any machine (canary hashes identical);
- **Put Windows back** works on every machine that installed;
- every machine either installs and reaches "Does everything work?: Yes", or
  is refused **before anything changed**, with a sentence that says why;
- the report's predictions are right, or the wrong ones are fixed;
- the fraction of PCs the installer refused, and why, is written down (it
  decides whether BitLocker PCs are worth the extra work before launch);
- at least one hard-disk machine under an hour;
- the power pull passed on two.

Then the lead can open the link to the first few people.
