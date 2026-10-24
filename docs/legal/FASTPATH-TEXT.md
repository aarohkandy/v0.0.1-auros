# Text that waits for the fast path (NOT published)

Prepared by the AurOS agents without a lawyer (D15). The fast path (`AUROS_FASTPATH`, decisions D1, D10, D11) is off in
the default `AurOS-Setup.exe`, so the published Terms and Privacy say nothing about carrying her Wi-Fi across or pausing
the drive lock: a policy that describes a feature the shipped file does not have is wrong. **The day the default flips
(`build/aurbridge`), these sentences go into `TERMS.md` section 4 and `PRIVACY.md` section 3 in the same commit** (the site
check goes red until the website's two wordings follow). They were rewritten after the skeptic review, which found that
the earlier text ("paused for one restart", "only if you say yes", "never switched off") did not match the code.

Written from the code at 2026-10-24 (`src/aurbridge/phases.c`, `wizard.c`); re-read it before use.

**Terms, section 4 (what Setup does), add:**

> On a computer whose drive Windows has locked (BitLocker), Setup pauses the drive's protection. The files stay encrypted,
> but while protection is paused anyone who has the computer could read the Windows drive. Start Windows again to let
> Windows turn protection back on; until Windows reports that it has, protection remains paused. Setup does not decrypt the
> drive or remove BitLocker. Setup also copies the Wi-Fi network you are connected to (its name and password) to
> a place on the computer's start-up partition, so that AurOS can get online when it starts; it is ticked for you on the
> Ready screen and you can untick it. The copy is not encrypted while it waits there, and AurOS deletes it once it has
> stored the password.

`phases.c` requests `-RebootCount 2`, but that setting alone does not prove which boots Windows counts when the first boot
is AurOS. Do not state a restart count or automatic-resume instant until the real journey records Windows' protection
status after the AurOS handover and after each later Windows start. The start-up partition is never BitLocker-encrypted.
Whether "AurOS deletes it" holds in every failure path is open: see `COMPLIANCE.md` 4.8 item 4; do not publish the last
sentence until `tools/wifitest.sh` proves it for a failed install.

**Privacy, section 3, add:** the same Wi-Fi sentence: read on the same computer, never sent over the internet.

**Ready page:** the pre-ticked Wi-Fi box must say, in its own line, "Copies this Wi-Fi's password to AurOS on this computer.
Not sent anywhere." (COMPLIANCE 4.8 item 4), and the drive-protection line must use the same no-count wording above.
