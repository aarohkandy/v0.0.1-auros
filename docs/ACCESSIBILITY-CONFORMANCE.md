# Accessibility conformance (a stub, honest about what is not done)

Prepared by the AurOS agents without a lawyer (D15). `docs/legal/COMPLIANCE.md` WEB-2, WEB-4.

| Surface | Target | State on 23 Oct 2026 |
|---|---|---|
| Website | WCAG 2.2 AA | Built for it (Atkinson Hyperlegible Next, 21 px text, focus ring, skip link, reduced motion, 4.5:1 and 44 px gates in `tools/site/check.mjs`). **Not yet:** a WCAG 2.2 automated scan (axe-core) in `pages.yml`, `forced-colors` styles, a manual NVDA/Narrator, 200% zoom and 400% reflow pass. |
| AurOS Setup | UI Automation names, keyboard | Names exposed to Narrator (`windows.yml`); nothing else claimed. |
| AurOS desktop | screen reader (Orca/AT-SPI) | **Not available yet.** The website says "AurOS can't read the screen aloud yet", so a person who needs a screen reader should not install it yet. |

Scope: no EU-directed marketing, no sale to EU consumers and no EU pre-installed computers until the
EN 301 549 / EAA work (WP-66, WP-67) is done. A site accessibility statement page follows the scan:
"We aim for WCAG 2.2 level AA. What AurOS cannot do yet: ... If you cannot use any part of this
site or AurOS Setup, write to CONTACT_EMAIL and we will reply as soon as we can, usually within a
week." (dated; not a "5 working days" promise one person cannot keep).
