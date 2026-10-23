# AurOS roadmap

> Detailed plan with every work package: [PLAN-END-TO-END.md](PLAN-END-TO-END.md).

The bet: the next computer for a billion people who are not "computer
people" is the one they already own, made calm, safe and simple. Windows
fights them (ads, nagging updates, scareware, blue screens); macOS asks
them to buy new hardware. AurOS takes the PC on their desk and turns it
into something an 85-year-old can use without ever calling for help, in
under an hour, with Windows kept safe until she says "it works".

Four promises, in this order, decide every trade-off:

1. **Her things are safe.** Nothing she owns is ever lost. Windows can
   always come back. Refuse first, prove every fence.
2. **She is never confused.** One obvious next step on every screen. No
   terminal, no jargon, nothing to type. (`docs/EASY.md`)
3. **It works on her PC, fast.** Website to using AurOS in under an hour
   on an ordinary PC; about 10 minutes on a good one.
4. **It is beautiful.** One visual language, made by one hand, from the
   website to the last help page. (`docs/DESIGN-DIRECTION.md`)

## v0.1 "It works" (session B, now)

- [ ] The whole pipeline is one chain: a `[release] [latest]` commit builds,
      tests in KVM, publishes a prerelease, runs the real-Windows journey on
      that exact file, and only then promotes it and deploys the website.
- [ ] The real-Windows journey passes end to end: website → Get AurOS →
      Keep anyway / Run anyway / Yes → three screens → restart → AurOS
      "Does everything work?" → Yes → install a program → it opens.
- [ ] Website live at the new repository's Pages address; the button
      downloads the promoted installer.
- [ ] Every data-safety finding rated blocker or high is fixed with a test
      (`docs/safety/FINDINGS.md`).
- [ ] One design language across website, installer, AurOS and help.
- [ ] Measured: wall clock for an SSD PC and an HDD PC (e2e `hdd` variant).

## v0.1+ "Wishes" (AI, in parallel with v0.1)

- [ ] She says what she wants her computer to do; Claude turns it into a
      safe wish she confirms; her computer does it (`docs/AI.md`).
- [ ] A cloud service that holds the key, scales by itself, caps cost and
      abuse, logs no words (`docs/CLOUD.md`).
- [ ] "Ask AurOS": plain answers to "how do I…?", with an offer to do it.

## v0.2 "Every PC, under an hour"

- [ ] The fast path: Windows shrinks itself and AurOS is written during the
      download; staging only verifies and commits (about a minute).
- [ ] BitLocker / Device Encryption PCs install without decrypting (paused
      for one restart, D11); her Wi-Fi comes across (D10).
- [ ] e2e variants green: BitLocker, small disk, pending update, Windows 10, HDD.
- [ ] Her files come across when she says "it works" (Ferry).
- [ ] A small real-PC test campaign the owner runs (a VM cannot prove a
      physical PC): 10-20 machines across vendors, ages and disk types.

## v0.3 "Safe for years"

- [ ] Signed update channel live (security fixes reach her silently).
- [ ] Hardening, browser and scam protection on by default and measured.
- [ ] Encrypted disk + PIN (D9), only once it can never lock her out.
- [ ] Launch-readiness gate (`docs/LAUNCH-READINESS.md`) all green before
      anyone pays.

## v1.0 "Something people pay for"

- [ ] Wishes grow up: sandboxed mini-programs Claude writes for her, and
      "set my computer up for video calls with my grandchildren" at install time.
- [ ] Help that answers in her words, on the computer and on the website.
- [ ] Translations; a screen reader in AurOS; the other editions (office,
      school kiosk, revive, multilingual).
- [ ] Phase 2: official Ubuntu / Mint / Fedora installed the same safe way.
- [ ] The business: support, legal review, payments.

## How the work runs

One lead coordinates; one agent per stream owns disjoint files; adversarial
workflows review everything that lands; the repository is the only memory.
`START-HERE.md` → `docs/handoff/FIELD-MANUAL.md` → `docs/handoff/STREAMS.md`
→ `docs/handoff/streams/<ID>.md`. Progress: `docs/handoff/SESSION-2026-10-23-B.md`.
