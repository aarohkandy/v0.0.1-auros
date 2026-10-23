# Wishes: the Terms paragraph that waits for the Wishes gate (NOT published)

Wishes is **not in the v0.1 image at all** (`tools/wishesoff.sh`, decision D14), so the published Terms say nothing about it:
saying it is "switched off" would claim it is there. When it ships (with `WISHES-PRIVACY-DRAFT.md`'s corrections), this goes into
`TERMS.md` section 5 (COMPLIANCE AI-5):

> **Wishes** uses an AI, which can misunderstand you. Wishes is built to do only a short list of things, each shown to you
> first, and it cannot delete files, send messages, buy anything or change security settings. It is not a safe way to remember
> medicine, appointments or anything where missing it could hurt you. A reminder can fail, for example when the computer is
> off, asleep or has no battery. Use a phone alarm or a person as well. Wishes is free for now and may be limited or
> unavailable at times.

The first sentence says "is built to" because the cannot-delete-files claim is a design goal (`docs/AI.md` 1.3), proven by the
engine's tests, not a promise about every possible wish. Do not ship the sentence "it cannot ..." as a flat promise unless the
tests in `docs/AI.md` section 3.3 still pass at the release.
