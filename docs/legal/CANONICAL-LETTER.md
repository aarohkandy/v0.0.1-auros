# A notice to Canonical (optional; send it, or don't, as the owner chooses)

Prepared by the AurOS agents without a lawyer (D15). `COMPLIANCE.md` LIC-5 and TM-2 say why and how:
this is a **notice**, framed as information, not a request for permission. It does not block launch
of a free AurOS and it must not be sent as "do you consider us a modified Ubuntu that needs a
license?" (that invites the answer Linux Mint got in 2013-14). Send it through
https://canonical.com/legal/terms-and-policies/contact-us (a reply may take a week). File the
reply in this folder. **Do not charge money for anything that ships this image until Canonical has
answered in writing**, or accept that risk on purpose (`MONETISATION-GATE.md`).

**Send it only when `python3 -I tools/legal/check.py image` is green on the release being offered** (the published
`v0.1.0-618726b` carries `ubuntu-mono`, an Ubuntu icon package, which makes the sentence below untrue until stream C
purges it). Check each sentence is still true on the day (the first two sentences are what the image is today;
`tools/legal/check.py image` checks the branding packages and the name):

---

Subject: Notice: a free Linux desktop for Windows computers that includes Ubuntu 24.04 LTS software

Hello,

We make AurOS, a free desktop that a person installs beside Windows with one download. It includes
software from Ubuntu 24.04 LTS (noble), put together and changed by us. We write to say so plainly, and to
tell you what we do.

- The programs are Canonical's and the Ubuntu community's, installed from the Ubuntu archive
  (archive.ubuntu.com and security.ubuntu.com) as published, and updated from the same places
  (noble-updates and noble-security). We do not recompile them. Our changes are configuration and
  our own programs, which are MIT licensed.
- AurOS's own screens, installer and website use no Ubuntu logo or artwork and never call AurOS
  "Ubuntu"; the name appears only in sentences that say what AurOS is built from. Some Ubuntu strings remain where they come from Ubuntu's own packages and
  files (the kernel version string, package copyright files, the boot file name of Canonical's
  signed GRUB). The website says: "AurOS includes software from Ubuntu 24.04 LTS, put together and changed by its maker. It is not made,
  endorsed or supported by Canonical. Ubuntu is a registered trademark of Canonical Ltd."
- The source code for every release is published beside it (a release named sources-<release>
  on our GitHub project, with the Ubuntu source packages in the exact versions).
- AurOS is free of charge and no money is taken for it.

If anything in this is a problem for you, please tell us and we will change it.

Thank you for Ubuntu.

[owner's name], AurOS, CONTACT_EMAIL
