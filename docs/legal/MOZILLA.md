# Firefox in AurOS: what we change, and what Mozilla's rules say

Prepared by the AurOS agents without a lawyer (D15). Rules: Mozilla's Unaltered Software Distribution Policy
(https://www.mozilla.org/en-US/foundation/trademarks/distribution-policy/, read again on 23 October 2026) and MPL 2.0
(`COMPLIANCE.md` LIC-6, TM-5). **Corrected after the skeptic review: this is not "later". AurOS's Firefox is outside the
policy's plain words, so the owner should ask Mozilla, or change the configuration, before the public link.**

## What the image does to Firefox (read from `build/forge` and `rootfs/etc/firefox/policies/policies.json`)

1. **The binary is Mozilla's, unchanged.** Route: Mozilla's own apt repository (`packages.mozilla.org`, key fingerprint
   checked), then the mozillateam PPA, then the hash-checked tarball only as a last resort (a release build should refuse
   the tarball: `COMPLIANCE.md` LIC-6; `build/forge` is stream C's). Source is Mozilla's: `build/sources` puts the address
   and hash in `SOURCES.txt` and a copy of the tarball in the sources release.
2. **`policies.json`** (Mozilla's documented administrator mechanism): HTTPS-only, Safe Browsing on and locked, tracking
   protection, telemetry and studies off, developer tools and about:config off, notifications off, sponsored content off,
   pop-ups blocked; add-ons only from addons.mozilla.org; **uBlock Origin is force-installed** from addons.mozilla.org;
   `DisableAppUpdate` (updates come through apt).
3. **The launcher is called "Internet"** in AurOS's own shell (`build/forge` rename table). The Firefox window and about box
   still say Firefox.
4. `SkipTermsOfUse` is not set.
5. **The version is the one the build installed, and updates depend on the route.** The policy asks for the most recent
   version of an unaltered Firefox. With `DisableAppUpdate`, an image updates Firefox only through an apt source: Mozilla's
   repository or the mozillateam PPA. The hash-checked tarball route (a last resort) leaves a Firefox that never updates, so a
   release build should not use it (`COMPLIANCE.md` LIC-6).
6. The footer, Terms, Disclaimer and Licenses pages say the names are used only "to say what a program is, what AurOS is made
   from, or what it works with". For Firefox that is true of the name, and the two things above (force-installed extension,
   locked defaults) are what Mozilla's page says need its permission; this file is the record of that open question.

## What Mozilla's page says (summarized)

- Without asking: you may distribute **unchanged** Firefox from Mozilla, if it is free, not tied to a purchase, collects no
  personal information, and is the latest version. It lists, as **not** unaltered: adding extensions, changing default
  settings, changing the installation process or installing other software or add-ons with it, and changing any part of the
  software, trademarks included.
- Any change, if you still want to use Mozilla's marks, needs **prior written permission**: for example changing defaults,
  adding extensions, add-ons or plugins. The request address is trademark-permissions@mozilla.com. The page does not say a
  policy file is allowed; it does not mention policy files at all.
- So: a **force-installed extension** and **locked changed defaults** are what the page names as needing permission. The
  renamed launcher alone does not cure it (the marks are inside the program).

## What can be done (owner's choice; the agents' order of preference)

1. **Ask**, free: email trademark-permissions@mozilla.com: "AurOS is a free desktop. It installs Mozilla's Firefox from
   packages.mozilla.org, unchanged, and adds a policies.json (attached) with HTTPS-only, Safe Browsing on, telemetry off, and
   uBlock Origin installed from addons.mozilla.org. We charge nothing and collect no data. May we distribute it like this and
   call it Firefox?" File the reply here. Replies can take weeks or never come.
2. **Change the configuration so it is inside the policy:** no force-installed extension and no locked changed defaults;
   offer uBlock Origin as a first-run choice she presses ("Add the ad blocker"), keep only what Mozilla's page allows. This
   weakens the scam protection the owner asked for (stream S2's design), so it is a product decision.
3. **Do not call it Firefox:** ship the browser under another name and without Mozilla's marks (a rebuilt "Iceweasel"-style
   package: a large project, not a quick fix).
4. Epiphany (GNOME Web) is the documented fallback if Mozilla says no.

**Required before any paid offer that ships Firefox** (a bundle tied to a purchase is outside the policy whatever the
configuration is).

The `COMPLIANCE.md` status row for LIC-6 and the owner's checklist item 17 now say "before the public link, recommended".
