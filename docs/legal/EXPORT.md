# Export controls: a one-page record

Prepared by the AurOS agents without a lawyer (D15). `COMPLIANCE.md` SL-3.

- AurOS's own code is MIT, public, and calls standard cryptography only (Ubuntu's OpenSSL, GnuTLS,
  OpenSSH, NSS; LUKS2 and TPM2 for the disk when D9 ships). Publicly available encryption source
  code is not subject to the US Export Administration Regulations (15 CFR 734.3(b)(3)); the
  notification to BIS applies only to source implementing **non-standard** cryptography (742.15(b)).
  Nothing in AurOS does. `src/aurbridge/format.c` contains a SHA-256 used only to check that a
  download is intact.
- Design gate for D9 (`docs/security/THEFT.md`): standard algorithms from cryptsetup/systemd only;
  no custom primitives. If AurOS-authored non-standard cryptography ever ships, send the free
  BIS/ENC notification email **before the release**.
- If pre-installed computers are ever sold outside the US: self-classify under 5D992.c, or ask for
  a CCATS.
- Sanctions (OFAC) and embargoes apply regardless; GitHub's trade-controls page covers sanctioned
  regions. The Terms say only "You are responsible for following the export laws of your country."
  Do not turn that into a use ban (it conflicts with MIT and GPLv3 s10).
- Do not add a lint that bans `.c` files containing the word "crypto" (the SHA-256 would fail it).
