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
  must stop with no remaining processes in their cgroups, and must not cause
  kernel OOM or earlyoom kills. Screenshots, OCR, document and result remain
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
