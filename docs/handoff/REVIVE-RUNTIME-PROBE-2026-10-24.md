# Revive real-use probe: implemented, not yet run on an image

The opt-in probe exercises two applications sequentially in the existing
2048 MiB profile VM. It has **no runtime pass yet**. It neither rebuilds nor
changes a customer image; profiletest injects it into its disposable copy.

- Firefox opens a loopback page, displays a unique heading, receives physical
  QMP keyboard input, and submits its exact value with trusted key/focus facts.
- AbiWord opens an initially empty ABW document. Physical keys type a sentinel
  and Ctrl+S saves it. The reducer requires the exact parsed paragraph text
  and unique visible screenshot text, not arbitrary changed pixels. The host
  binds OCR to its screenshot with hashes; reduction checks both hashes and
  the complete PNG pixel stream and checksums.
- Both applications retain exact executable, user, systemd unit/cgroup, PID
  and start-time identity before and after interaction. They run sequentially,
  receive forced systemd service cleanup with no remaining cgroup processes;
  this does **not** prove normal application Quit. They must not cause kernel
  OOM or earlyoom kills. Earlyoom must still be active afterward, and neither
  system nor user failed-unit inventories may gain new entries. The user
  manager display environment must exist and be inherited by each app. Screenshots, OCR, document and result remain
  in the `revive` evidence directory, also published as a small tarball.

For an existing image on a Linux host with the usual profiletest dependencies
and Tesseract, run `sudo env PROFILETEST_REVIVE_FUNCTIONAL=1 sh tools/profiletest.sh boot revive`.
The memory setting is enforced at 2048 MiB. A future approved fresh profile
build can opt in with `[revive-functional]` in the push subject; this
marker selects Revive even without a separate profile marker. A diagnostic-only marker suppresses builds as before; this change
does not dispatch anything. Ordinary profile boots and multilingual probes
remain unchanged.

This first bounded slice does **not** test Gnumeric, simultaneous multitasking,
real internet browsing, speed on old disks, physical PCs, or delivery through
the Windows installer. A passing reducer on fixtures is only harness testing.
A future exact-image run must supply the runtime evidence before any Revive
readiness claim.

For a fresh functional Revive build, Actions retains the exact raw image's
gzip as `profile-revive-runtime-image` for **two days**, including when the
runtime probe fails after image creation. It reuses the size-measurement gzip
and records source SHA, repository, workflow, run ID, profile, raw/compressed
sizes and SHA256 values in `manifest.json`. Creation checks the checkout SHA
and decompresses the gzip to verify the exact raw digest. The large image is
excluded from ordinary evidence artifacts and releases. Before any manual
reuse, compare artifact run metadata with the manifest and verify both sizes
and hashes; do not trust a downloaded filename alone. No generic retained
Revive dispatch workflow is added. These artifacts may contain several GiB
of compressed data and expire automatically, so preserve needed evidence
before expiry.

Firefox identity review: retained production run `40156605186` identifies the
source browser route as `mozilla-apt` and retains display environment and
process disappearance, but does **not** retain Firefox's `/proc/exe` path.
The older hardening harness's `firefox-bin` process-name observation does not
prove a launcher executable transition. This probe therefore invokes
`/usr/lib/firefox/firefox-bin` directly, requires its exact `dpkg-query -S`
owner to be `firefox`, and checks the exact running executable path before
and after input. It has no tarball-path fallback. A packaging layout that
does not satisfy that contract fails explicitly and needs evidence review;
the existing multilingual success is not claimed to prove this new launch.
