# Testing AurOS on real PCs

For the owner, rewritten 2026-11-10 for the public release (`v0.1.0-e474f99`).
Nothing in AurOS has run on a physical PC before this; every run so far was a
virtual machine. Each real PC you try finds things no virtual machine can.

**The one rule above all others:** use PCs whose files you do not need. If any
PC loses a file, stop the whole test and tell the lead before touching the next.

---

## 0. The order, for a pile of PCs

1. **Every PC: the report (5 minutes, changes nothing).** Section 2. Do this on
   all of them first. It tells you which ones the installer will take and what it
   predicts will work.
2. **Pick the first install:** the PC you care least about, that the report
   said YES for. Do sections 3 to 6 on it, start to finish, before the second.
3. **Then the rest, one at a time.** Most different first: a laptop, a desktop,
   an old hard-disk PC, a new Windows 11 laptop (BitLocker), an AMD one, a Nvidia
   one, one the report said NO for (to see the refusal on a real screen).
4. **For each PC, fill in one sheet** (`TEMPLATE.txt`, section 7).

A PC the report says NO for is still useful: install nothing on it, keep its
report. That is how we learn how many real PCs are turned away, and why.

---

## 1. Before you touch a PC

1. **Write down the exact model** (sticker underneath, or Settings > System >
   About). Photograph the sticker **with your thumb over the serial number**.
2. **Do not update or tidy Windows first.** We want it as a person has it.
3. **Plug in the charger**, and a printer if you have one (so the report lists it).
4. **Make a canary**, three files we check at the end to prove nothing was lost.
   Windows key, type `powershell`, Enter, paste:

   ```
   $d = "C:\Users\Public\canary"; New-Item -ItemType Directory -Force $d | Out-Null
   1..3 | ForEach-Object { Set-Content "$d\file$_.txt" ("canary " + $_ + " " + (Get-Random)) }
   Get-FileHash "$d\*" | Format-Table Hash, Path
   ```

   Photograph the three hashes. Put one ordinary photo in `Pictures` and one
   document in `Documents` too: those are what **Bring my files across** carries.

---

## 2. The report (5 minutes, any PC, changes nothing)

1. In Edge, open **https://1os.vercel.app** and press **Get AurOS**. Edge says
   the file *isn't commonly downloaded*: press **Keep**, then the small arrow next
   to *Delete*, then **Keep anyway**. Do **not** open it yet.

   The same file directly:
   `https://github.com/aarohkandy/v0.0.1-auros/releases/latest/download/AurOS-Setup.exe`.
   Its SHA-256 for `v0.1.0-e474f99` begins `4fc62944`. To check it (optional):
   `Get-FileHash "$env:USERPROFILE\Downloads\AurOS-Setup.exe"` in PowerShell.

2. Windows key + R, paste exactly this, Enter:

   ```
   "%USERPROFILE%\Downloads\AurOS-Setup.exe" --report
   ```

   (If the download landed somewhere else, put that folder in place of
   `%USERPROFILE%\Downloads`.)
3. If a blue **Windows protected your PC** appears: **More info**, **Run anyway**.
   (No *Run anyway* means Smart App Control is on: this PC cannot run an unsigned
   installer. Write that down; that is a finding.) Then **Yes** to *allow this app
   to make changes*.
4. A box says where the report is: next to the downloaded file (usually
   Downloads), or the Desktop. Two files: `AurOS-report.txt` and
   `AurOS-report.json`. Nothing on the PC was changed.
5. Open `AurOS-report.txt`. The parts that matter: **would the installer accept
   this PC** (yes or no, and why), **what will work** (Wi-Fi, sound, camera...),
   and **the devices Windows sees**.
6. Rename both to the model (`dell-latitude-5420.report.json`, and `.txt`) and
   keep them. The report carries no serial number, user name, PC name or Wi-Fi name.

---

## 3. The install (one PC at a time)

1. Photograph, with your phone, **every screen**, in order. Most important: any
   screen that stops, any that waits a long time, and anything you did not expect.
2. Start a stopwatch when you press **Install AurOS**. Stop it when AurOS asks
   **Does everything work?** Write the minutes.
3. Double-click `AurOS-Setup.exe` (the same Edge / Windows warnings as above).
   - First page: it checks the PC. If it lists problems, press **Fix these for
     me** and write down what it fixed (Fast Startup, space, BitLocker,
     waiting updates).
   - Pick a look. **Next**. **Install AurOS**. One bar and the time left.
   - **Ready to restart**: nothing restarts by itself. Press **Restart now** (Restart later just puts the window away).
4. **After the restart the screen shows text, not a desktop.** That is the
   installer. Do not switch off while it says *Making room on the Windows drive*.
5. **Say out loud anything that confuses you.** If you hesitate more than ten
   seconds, write it down: that is a finding, as important as a crash.

### If it stops after the restart

If the text screen stops with a sentence and `verdict=...`: **photograph it**.
A stop before *Making room* has changed nothing: switch off and on, Windows
starts. Then, in Windows, get what it wrote. Windows key, type `powershell`,
right-click **Run as administrator**, paste:

```
mountvol S: /s
Copy-Item S:\EFI\AurOS\stage.log "$env:USERPROFILE\Desktop\stage.log"
mountvol S: /d
```

(If it says `S:` is in use, put another free letter in all three lines.)
`stage.log` lands on the Desktop. **Copy only `stage.log`**: the same folder
holds `wifi.conf`, which has the Wi-Fi password in it. Never send that.

If the PC starts neither AurOS nor Windows: switch it on and press the start-up
menu key (often **F12**, **F9** or **Esc**; the maker's logo screen usually says)
and choose **Windows Boot Manager**. Photograph the menu.

---

## 4. Inside AurOS (write yes / no / n/a / untested for each)

AurOS first asks **Does everything work?** Do NOT answer yet. Check:

- [ ] **Wi-Fi**: connect to your network, open a website.
- [ ] **Network cable**, if it has a socket.
- [ ] **Sound**: play something; headphones if it has a socket.
- [ ] **Camera and microphone**: Meet on the Internet start page, start a
  meeting; you see yourself and its microphone check moves when you speak.
- [ ] **Bluetooth**: pair a mouse or headphones.
- [ ] **Screen**: sharp (full resolution), brightness keys work.
- [ ] **Laptop lid**: close it, wait a minute, open it: does it come back?
- [ ] **Battery** shown, and goes up on the charger.
- [ ] **Printer**: print a page.
- [ ] **A new program**: Get more programs, install VLC, open it.
- [ ] **Solitaire** from the list: opens, and closing it brings you Home.
- [ ] **Turn off and on**: **Turn off** at the bottom, then switch on: does AurOS start again?
- [ ] **Keyboard and touchpad**: typing, two-finger scroll.

Then press **Yes, it all works**, then **Start AurOS from now on**, then
**Bring my files across**. Check the photo and document from section 1 arrived.

**If something does not work:** photograph the screen, write what you pressed.
There is no way yet to save AurOS's own logs from inside it; a photo of the
screen and the sheet are what we have.

---

## 5. Getting back to Windows, and putting Windows back

1. **Windows still starts:** switch on, press the start-up menu key, choose
   **Windows Boot Manager**. Windows should be as it was. Run the canary
   command again (section 1.4): **the three hashes must be identical**. Look for
   the folder **From AurOS** in your user folder.
2. Back in AurOS: **Settings > Put Windows back**, then **Remove AurOS and put
   Windows back** twice. It restarts, shows text, and **switches itself off**.
   Do not switch it off while the text is still moving. Switch on: Windows.
3. Canary again: **identical**. Photograph next to the first photo.
   Windows' drive should be its old size again (File Explorer > This PC).

If anything is missing: stop, photograph, **do not retry**, tell the lead.

---

## 6. Things only a real PC can show (watch for these)

- After **Start AurOS from now on**, a later **Windows Update or firmware
  update** may put Windows first again, and the PC just starts Windows. If that
  happens, write it down (and use the start-up menu key to reach AurOS).
- The PC starts Windows straight after the restart in section 3 instead of the
  text screen: its firmware ignored the one-time start entry. Big finding.
- BitLocker PCs (new Windows 11 laptops): the installer pauses it for one
  restart. Afterwards Windows must start **without asking for a recovery key**.
  If it asks: the key is at https://account.microsoft.com/devices/recoverykey.
- Nvidia graphics: AurOS uses the open driver; games are slow, the desktop
  should be fine. Note if the screen is black or wrong.
- A long wait anywhere (hard disks): write how long.

## 6a. Pulling the power (only on a PC you would happily reinstall Windows on)

Only after two PCs have gone all the way through, and only one PC. Start an
install; while the text screen says *Making room on the Windows drive*, pull the
power (and the battery if it comes out). Switch on. Expected: the install
carries on, or Windows starts as before. Anything else is a serious finding.

---

## 7. The sheet, and handing over

For each PC, copy `TEMPLATE.txt` to `<maker-model>.txt` (lower case, dashes),
fill it in, and put its `.report.json` next to it. Hand the files and photos to
the lead (or any agent). They commit them and run
`python3 -I tools/hw/results.py`; `README.md` here becomes the table of what the
report predicted against what really happened, with the two bad kinds of miss
in red: promised "works" but did not, and turned away a PC that would have been fine.

## 8. What "good enough for her" means

- No lost file on any PC (canary hashes identical).
- **Put Windows back** worked on every PC that installed.
- Every PC either reached **Does everything work?** or was turned away
  **before anything changed**, with a sentence that says why.
- Wi-Fi and sound worked wherever the report said they would.
- At least one hard-disk PC under an hour.
- Nobody testing was confused for more than ten seconds without it being written down.
