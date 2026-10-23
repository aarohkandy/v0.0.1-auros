# Firefox in AurOS: what we change, and what Mozilla's rules allow

Prepared by the AurOS agents without a lawyer (D15). Rules: Mozilla's Unaltered Software
Distribution Policy (https://www.mozilla.org/en-US/foundation/trademarks/distribution-policy/) and
MPL 2.0 (`COMPLIANCE.md` LIC-6, TM-5). Severity: later; a **paid bundle that ships Firefox is a
different matter** (before charging money).

## What the image does to Firefox (read from `build/forge` and `rootfs/etc/firefox/policies/policies.json`)

1. **The binary is Mozilla's, unchanged.** Route: Mozilla's own apt repository
   (`packages.mozilla.org`, key fingerprint checked), then the mozillateam PPA, then the hash-checked
   tarball only as a last resort (a release build should refuse the tarball: `COMPLIANCE.md` LIC-6;
   `build/forge` is stream C's). Source is Mozilla's: `build/sources` puts the address and hash in
   `SOURCES.txt`.
2. **`policies.json`** (Mozilla's documented administrator mechanism): HTTPS-only, Safe Browsing on
   and locked, tracking protection, telemetry and studies off, developer tools and about:config off,
   notifications off, sponsored content off, pop-ups blocked; add-ons only from addons.mozilla.org;
   **uBlock Origin is force-installed** from addons.mozilla.org; `DisableAppUpdate` (updates come
   through apt).
3. **The launcher is called "Internet"** in AurOS's own shell (`build/forge` rename table). The Firefox
   window and about box still say Firefox.
4. `SkipTermsOfUse` is not set.

## What this means

- Mozilla's policy allows redistributing the unmodified program with an enterprise policy file; a
  force-installed extension and changed defaults are a gray area that Mozilla could object to. The
  worst realistic outcome is a request to stop using the Firefox name for this configuration; no
  enforcement against a small distribution was found.
- Moving uBlock Origin off force-install would not bring AurOS inside the plainest reading of the
  policy and would weaken the scam protection the owner asked for. Keep it.
- Epiphany (GNOME Web) is the documented fallback if Mozilla ever says no.

## What the owner may do (free, optional)

Email trademark-permissions@mozilla.com: "AurOS is a free desktop. It installs Mozilla's Firefox
from packages.mozilla.org, unchanged, and adds a policies.json (list attached). We do not charge, we
collect no data, and we tell users Firefox is Mozilla's. Is anything here a problem?" File the reply
here. Replies can take weeks or never come. **Required before any paid offer that ships Firefox.**
