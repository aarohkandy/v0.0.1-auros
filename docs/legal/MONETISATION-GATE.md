# Before anything is sold or any money is taken: the gate

Prepared by the AurOS agents without a lawyer (D15). "Money" means a price, a donation or sponsor
link, paid help, a paid Wishes quota, an image or a pre-installed computer sold. Nothing in v0.1
takes money. Every line below has a reason in `COMPLIANCE.md`; the item ids are there.

**Before the first price or donation link exists:**

1. The LLC, EIN, business bank account and IP assignment are done; accounts are in the LLC's name
   (LIA-6, BIZ-1/2/9). No donation or sponsor link and no `FUNDING.yml` before that (PRI-8).
2. Insurance is bound (tech E&O and cyber, as the LLC), and the insurer knows what the
   product does to disks (LIA-7).
3. A merchant of record (or a direct processor) has confirmed in writing which products it covers;
   remote human help is often refused (BIZ-8/9).
4. The website leaves GitHub Pages for the pricing and order pages (WEB-3).
5. The wording changes in the same commit: remove "no subscription"; "AurOS itself is free" stays
   (TERMS 1); the money line is by channel, not "unless you asked" (BIZ-7: never by phone, email,
   text, gift card or in person; only by pressing Buy inside AurOS with the price on screen first).
6. Sales terms exist (`SALES-TERMS-OUTLINE.md`): price before any charge, one-click cancel, a 30-day
   refund, no auto-renewal in the first version, receipts.
7. Paid Wishes: `PAID-TERMS.md`, and `docs/security/CRA.md`'s rule (a paid Wishes quota reached from
   the free shell is the EU Cyber Resilience Act trigger: decide manufacturer status first, or sell
   only in the US, UK, Canada and Australia) (SL-1/2, AI-8).
8. EU: no paid offer is shown to EU visitors until the CRA, Product Liability Directive and
   accessibility work is decided (LIA-4, SL-1, WEB-4, BIZ-10).

**Re-check on the day (they change):**

- Canonical has answered in writing about the image (LIC-5, `CANONICAL-LETTER.md`).
- Mozilla's agreement if a bundle ships Firefox (LIC-6, `MOZILLA.md`).
- Codec patents: ask Via LA and Access Advance in writing whether free software distribution needs
  a license, **before** selling an image or machines, hosting transcoding, or passing about 100,000
  units a year; the profile has no `media_codecs` switch yet, so the encumbered decoders (mpv, ffmpeg,
  libheif) cannot be dropped in a day (LIC-10: stream C).
- The name: `NAME-SEARCH.md` was a knockout only; pay for a full search before spending on the name.
- Hardware sales: GPLv3 for GRUB (a way to enroll her own key, or Secure Boot off), the firmware
  vendors' terms, Intel's OEM terms.
- The wave plan and territory (LIA-3).
