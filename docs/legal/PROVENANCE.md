# Who wrote AurOS, and under what license

Prepared by the AurOS agents without a lawyer (D15). `COMPLIANCE.md` LIC-11, BIZ-3/4.

- AurOS's own code, documents and designs are written by the owner **with AI assistance** (most
  commits are made by AI agents working on the owner's behalf and carry a `Co-Authored-By` line).
  They are published under the MIT license, `Copyright (c) 2026 AurOS contributors`. Components by
  other people keep their own licenses (`rootfs/usr/share/doc/auros/THIRD-PARTY-NOTICES`, the
  `NOTICE` files in `tools/`).
- The history is direct commits to `main`: there is **no pull-request review trail**, and nothing
  here claims one.
- Under US law, prompts alone do not make a person the author of AI output; human selection,
  arrangement and modification are protected (US Copyright Office, AI report part 2, January 2025).
  MIT lets anyone, including the owner, use, change, sell and sublicense the code, so this does not
  stand in the way of a later paid service.
- The LICENSE line changes (to the owner's or the company's name) only **after** a signed assignment
  exists (`templates/IP-ASSIGNMENT.md`); the Windows installer's `LegalCopyright` string and the
  `CompanyName "AurOS"` check in `windows.yml` change in the same commit.
- Any closed paid layer (billing, quota logic, support tooling) lives in a **separate private
  repository** owned by the LLC, never in the signed MIT tree (SignPath's terms forbid proprietary
  components in what it signs).
