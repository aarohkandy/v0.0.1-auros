# AurOS delivery plan

Owner authorization on 24 October 2026: implement, test adversarially, use bounded parallel agents, and push verified checkpoints to `aarohkandy/v0.0.1-auros`. The target is a nontechnical person describing the system they want, receiving it quickly, installing it safely, and using it without a terminal or helper.

## Current checkpoint (25 October, 00:22 UTC)

This section is the current summary; the chronological checkpoints below retain
earlier failures and superseded evidence deliberately.

- Candidate `e0d0aadc`, [build/release run 40138682256](https://github.com/aarohkandy/v0.0.1-auros/actions/runs/40138682256), passed its full image suite and published prerelease `v0.1.0-e0d0aad`. Source publication and notice checks passed. It is not public latest.
- Exact-image [hardening run 40140266950](https://github.com/aarohkandy/v0.0.1-auros/actions/runs/40140266950) passed 72 hardening and 14 clock checks; `hardengate.sh` accepts it for this candidate.
- All four Windows journeys failed: fastpath base clicked the search entry instead of the VLC result; ordinary base installed VLC but Open did not map a program window; ordinary BitLocker refused on its legacy path; fastpath BitLocker failed the harness's pre-install baseline capture. The strict launch check correctly rejected the store preview. App launch, encrypted-drive return and file-preserving recovery remain unproved. The older c1 app-launch result was invalidated and must not be reused.
- The release Windows job failed its fastpath no-scroll fixture. The working branch already has its tested correction and a visible, accessible drive-protection confirmation, but this candidate does not contain them. Do not rerun the unchanged installer expecting a different result.
- Multilingual diagnostic `40144734247` passed both boots of its then-current live checks, then failed evidence persistence. Follow-ups repair persistence, enforce a fresh focus marker and reject ambiguous password evidence. `40145440411` stopped at second-window visibility; `40145726339` attempt 1 stopped at an inconsistent Mozilla package index before testing. Attempt 2 is running. Full diagnostic proof remains open. Production remains IBus, with a stale startup warning visible in retained screenshots; Fcitx is not activated.
- `codex/delivery` retains installer follow-ups, the software startup race repair, store selection/privacy-dialog test repairs, image-integrity checks, and reduced artifact downloads for the next combined candidate. Two focused reproductions launched real VLC from Software, including one byte-exact image; the original Open no-op remains an intermittent unresolved risk. Preserve exact source/artifact identity when using earlier evidence.
- Public delivery remains disabled pending signing/service configuration and release proof. Managed profiles, physical-PC tests, contact/name decisions, held encryption work and unfinished Wishes actions remain open. No zero-vulnerability or end-to-end completion claim is justified.

## Starting evidence

- Remote main is `5b1f28e6`; original Desktop checkout is 783 commits behind. Work is coordinated from `/Users/aaroh/Documents/ChatGPT/auros` with isolated worker worktrees. Original Claude trees are preserved.
- RC7 `89ad43c8`, build run `40054679730`, built successfully but failed unit, Wishes UI and compositor mutation gates. It was not published. RC6 is the newest product prerelease; there is no full release.
- The RC6 Windows 11 VM fast-path journey reaches confirmation, then fails on first-session networking/store. RC7 contains a NetworkManager policy and offline catalogue fix, not complete installed-journey proof.
- Pending local work includes A2 boot-entry safety, B protected bootstrapper, C desktop edits, CIW tests, P profile evidence and 41 S4 encryption commits. Recover selectively; unique commits are not automatically correct or missing patches.
- Homebase live inspection: 62 GiB RAM, 51 GiB available, 23 GB disk free, shared services running, no `hb` command. No large disk allocations or cleanup of others' work. Small isolated memory-capped tests only; image builds go to GitHub.

## Product architecture

Default proposal pending owner response: five existing profiles (desktop, revive, office, school-kiosk, multilingual) share one maintained Ubuntu/systemd base and one boot/install/update stack. Consumer looks are separate from profiles.

Customization is a versioned, bounded declaration, not model-generated privileged shell. Prefer reusing tested image artifacts plus validated configuration over rebuilding a distribution for every request. Unsupported hardware, unsupported actions, missing delivery services and untested combinations produce explicit refusals or honest unavailable states.

The completed path must connect request, validation, consent, artifact identity, download, installer ingestion, installed configuration, and recovery. A JSON recipe download alone is an intermediate developer artifact, not completion of the user's journey. No page may claim personalized installation until the installer actually consumes and verifies that customization.

## Work sequence and ownership

### Stage 1 Restore release correctness

- [x] Unit owner: reproduce and repair RC7 font/hardware/layout/Ferry failures. Integrated unit gates passed at `5d1c50d3` and `d7a5675b`, including the corrected test dependencies.
- [x] Compositor owner: reproduce uncaught mutants, repair relevant code/tests, prove intended breakages are caught. Integrated image CI passes all four mutation shards and checks.
- [x] Wishes UI owner: standalone paint/end-to-end harness seams repaired; the full Wishes UI, paint, end-to-end and mutation job passes on `c1ba6ee5`. Feature remains disabled pending its separate activation requirements.
- [x] Installer safety owner: recover S-04, refuse before table commit when boot-entry creation fails, verify focused fault paths. Exact new-candidate Windows journey remains a separate release gate.
- [x] Bootstrapper owner: recover B's protected Windows loader design and validate Windows runner hostile-DLL/junction cases. Physical Windows 11 proof remains separate.
- [ ] Lead: integrate CIW regression coverage and verify the tests exercised match the shipped path.
- [x] Independent handover review: kernel/modules, network/time, mounts, devices/display, PID1/services reviewed and concrete findings repaired. Runtime installed-session proof remains required.

### Stage 2 Build a reusable customization path

- [x] Contract owner: strict schema, stable profile catalogue, deterministic compilation, policy preservation, hostile-input tests. Cross-Python depth behavior and dirty-CI fixture repair are included in the follow-up checkpoint.
- [x] Website owner: accessible request/review flow and honest artifact status implemented; 548 browser checks pass, including the real local API/compiler/ledger with only GitHub verification mocked. Public build service remains disabled.
- [ ] Delivery adapter: choose and implement authenticated recipe transport to the installer; pin base artifact and configuration digest; reject mismatches, stale/unsupported recipes and downgrade attempts.
- [ ] Installer/first-boot adapter: apply only allowlisted preferences, retain policy constraints and rollback behavior, confirm requested result in the installed system.
- [x] Agent extension contract: permitted extension points and required tests documented in `docs/AGENT-EXTENSIONS.md`.
- [ ] Wishes: finish missing action implementations/configuration/voice dependencies and real-image tests before enabling. No budget or model-spend changes without explicit accounting.

### Stage 3 Prove release and operations

- [ ] A new candidate passes all mandatory gates on its exact commit; no stale artifact substitution.
- [ ] Default and fast-path website-to-installed-app journeys, including BitLocker; same-boot network/time/store results captured.
- [ ] Five profiles build and boot, with policy and requested configuration verified.
- [ ] Power-cut, full-NVRAM, low-space, no-network, refusal and Windows-restore cases.
- [ ] Security review of integrated changes; mutation/failure-path checks demonstrate checks can fail.
- [ ] Protect signing authority and resolve encryption/lock integration; preserve owner decisions and current security gates.
- [ ] Name, monitored contact, domain, support responder, source availability, rollout/incident controls and physical-PC campaign.
- [ ] Public promotion only after exact-build gates and required owner dependencies are satisfied.

## Coordination rules

Each worker owns named files in a separate worktree. Workers commit locally and report evidence; the lead reviews, integrates and pushes coherent checkpoints. No worker pushes a partial shared state, rewrites another's edits, touches physical disks, weakens gates, changes signing secrets, or cancels another's CI.

Adversarial reviewers are read-only. Findings distinguish reproduced defects, code-supported risks and unverified hypotheses. A successful workflow containing skipped work is not test evidence. Real Windows VMs and physical PCs are reported separately.

Frequent Git checkpoints preserve completed work. Public release, cloud budget changes and activation of unfinished features are distinct from ordinary code pushes. The historical Claude watchdog may still exist; do not mechanically execute its resume script or create a competing lead.

## Owner questions pending

- Confirm existing five shared-base profiles versus five underlying distributions.
- Confirm constrained recipes for initial delivery versus arbitrary per-request source changes.
- Public product name and monitored contact email.

These questions do not block repairing current release tests or installer safety.

## Verified checkpoints

- First checkpoint: staging receives the normal kernel hardening on both boot paths; Windows builds cannot inherit arbitrary staging kernel arguments from the launch environment. Seven compiled-helper tests, a deliberately removed hardening option caught by the tests, full host simulation link plus inflater/Secure Boot selftests, C syntax and workflow lint passed. Independent security review caught and resolved the simulation linkage and environment-injection gaps before commit.
- RC7 Wishes UI failure: fake HTTP service startup raced the engine. The harness now waits for a bound port with liveness/deadline checks; Linux end-to-end test passes 19/19 with delayed startup. Old launch ordering fails before readiness; a crashed child is detected promptly. Production Wishes remains disabled.
- Five-base recipe contract: 21 adversarial tests pass. Exact fields/enums, bounded JSON, duplicate rejection, managed-policy preservation and deterministic plans. This is a validated request, not a built/downloadable OS; signed artifact delivery remains pending.
- Website recipe preview: 518 checks pass when combined with the compiler catalogue. Five base choices and allowlisted appearance preferences produce a local recipe, explicitly not an installer. Natural-language translation and delivery are separate work in progress.
- Compositor mutation gates: independent review confirms strengthened assertions and narrowly contained stale-client crash detection. Linux results: X tests 43/43, mutation shards 2 and 3 each 42/42 killed, X lifecycle with sanitizers 30/30, dialogs 32/32. New CI coverage also gates first-session network assumptions and offline store; those image-dependent checks await the integrated build.
- Protected Windows bootstrap branch `codex/installer-bootstrap`, checkpoint `16136efe`, is pushed for Windows workflow `40117670055`. It is not yet merged or declared proven. Inner-payload SignPath signing remains an explicit integration issue.
- RC7 unit repairs: Linux font checks pass, accessibility floors 316/316, hardware checks and mutants 45/45, Ferry 273/273. Ferry rejects a stale auto-discovered block-device path after a prior test detaches its handover volume. Explicit device overrides retain their existing behavior.
- Trusted Forge adapter: raw recipes compile into typed overrides; exact clean source is archived before a recipe build. Missing selected assets refuse, unfinished outputs remain incomplete, and metadata is explicitly unsigned. Adapter tests pass 9/9 after integration; prior security-review blockers are resolved. Full customized image and installer delivery remain unproven.
- Staging safety: boot entry creation precedes partition commit, while boot arming follows it; known pre-write failures clean the exact entry, ambiguous disk-write failures preserve recovery state. Mount moves now roll back on failure, with reboot if recovery itself fails. Normal/fault static Linux builds, 29 NVRAM checks, and injected mount transaction checks pass. Exact-build KVM, same-boot AppArmor and ordinary-path refusal still need runtime evidence.
- Extension map: `docs/AGENT-EXTENSIONS.md` documents the existing schema/engine/device path, including compass-to-brightness, and required extension evidence. Host Wishes checks pass (874 parser, 140 engine per compiler, 116 hook, 142 schema shapes, sound checks); Linux daemon, GTK and text-input integration were skipped on this host.

## Credit efficiency update

The owner explicitly prioritized credit efficiency during implementation. Finish assigned critical-path work without expanding the agent tree, reuse existing reviews and evidence, avoid redundant testing, and batch pushes to reduce repeated image CI builds. No automatic continuation or new paid service has been configured.

## Integrated CI follow-up

Build run `40117947027` at `a9588c78` built the image successfully. All 16 gating VM jobs, four power-cut jobs, four compositor mutation shards, compositor checks, Wishes UI/voice, and theft checks passed. The workflow remains failed because its unit job found the new page's missing shared footer, Python-version-dependent excessive-depth refusal, dirty-checkout recipe test fixtures, and remaining Ferry failures. Footer and recipe fixes are prepared; Ferry is being reproduced from this exact log. None of this is physical-PC proof or a public release.

The Windows bootstrap remains on its own branch. Windows run `40117947925` proved hostile-DLL positive control and protected extraction, then found a junction mutation fixture blocked by a second independent guard. Test-only repair `9e4736ee` is under Windows run `40124061490`; production checks are unchanged. Two-stage SignPath code integration and actual baked installer recipe defaults are the next bounded implementation slices.

Follow-up: Windows run `40124061490` at `9e4736ee` passed all jobs. Its protected bootstrap is integrated as `5cc1b47`; this is Windows Server 2025 runner evidence, not a physical Windows 11 install/restart campaign. Recipe defaults and malformed-recipe refusal are integrated, with 18 combined customization tests passing. The build-script hookup and shared two-stage signing implementation are now integrated; live signing still requires the owner's SignPath configuration. Do not mark the full SEC-003 release journey closed yet.

## Deferred security integration notes

S4's 41 held commits are not activation-ready. Preserve the disabled final runtime/test tree rather than replaying old CI wholesale. Missing setup/recovery/PAM and encrypted handover work, a filesystem-shrink power-cut window, and physical-machine evidence remain blockers. Ferry must not copy personal data while an empty temporary encryption key remains usable. The held workflow also needs pinned actions, minimal permissions and exact-build evidence before reuse.

Protected bootstrap signing requires two stages: verify the signed inner wizard against same-run build bytes, rebuild only the stub from the exact build source with that signed payload, sign the outer, then verify both signatures and extracted payload identity. The shared signer now implements this chain and an independent review confirms same-run/source binding and scoped credentials. Live SignPath policy configuration and approval are still required; keep the issue open.

## Current implementation checkpoint

- Ferry's final detached-loop fix verifies the current on-disk label, not merely a surviving block node. Linux handover 19/19 and Ferry 274/274 pass. Build `40124183660` at `5d1c50d3` is running the integrated gates; do not infer its result from earlier runs.
- Installer recipes are baked into the wizard, fail closed when malformed, and require an explicit matching desktop image plus image hash/size. Generic defaults remain unchanged. Combined Windows cross-build at `60765790` verifies exact embedded wizard bytes, recipe constants, stale-header removal, manifests and ASLR/DEP; it is not an installation test.
- Signing is implemented once in `sign-installer.yml`: signed inner wizard, rebuilt protected outer, signed outer, exact-byte and source/run proof. The signing credential is limited to the relevant steps. No live signature has been obtained; the necessary repository variables/token are absent.
- Configure requests now use deterministic local validation and no model call. Cloud tests pass 53/53; bare Office requests refuse rather than silently selecting an OS base. Cloud deployment/budget is unchanged.
- First-session journal evidence is now produced by an asynchronous service and consumed by the installed-journey harness: AppArmor and loaded profiles, route, DNS, `/run` permissions, and staging scratch cleanup. Missing evidence fails the journey. Runtime proof awaits the next candidate.
- Work in progress: reuse verified desktop base artifacts for small personalized installer builds, complete the independent same-boot handover audit, then run exact-candidate Windows install journeys. Public delivery and the remaining four profile builds are not yet complete.

## Candidate checkpoint after handover audit

The previously lost independent handover review was rerun. Its concrete findings are now addressed: one shared version-sorted kernel selector; checked display-process termination; checked ESP/Windows unmount and owned mountpoint cleanup; and refusal if saved scratch or any staging directory content remains. Failures recover the existing mount/cwd state and use the installed-flow restart path rather than carrying unsafe state into the desktop. Focused handover injection tests pass 22/22; kernel drift fixtures pass 5/5. Initial full static Linux staging link passed. Runtime KVM/Windows proof remains required.

The first-session probe also measures PID1's actual root mount (not the diagnostic service's read-only sandbox), ext4, writable root, absence of staging mounts, and absence of aurscreen. Its producer/parser self-tests pass, and they now run before publication as well as in the Windows journey harness.

Personalized packaging is implemented in `customize.yml`, using validated same-commit green base artifacts and rebuilding only AurBridge. It remains explicitly unsigned/unpublished/not-download-ready until subsequent delivery gates. With SignPath configured it calls the shared signer, preserving the original unsigned package manifest and binding its hash into the signed-byte provenance. Independent review closed the dropped-manifest issue. Combined customization tests pass 32/32 and workflow lint passes all 29 files.

Profile review identified the next bounded work: non-desktop image filenames are now generated safely from the known profile, but revive/multilingual are still refused for delivery until profile-specific installation proof exists. Multilingual input-method session wiring and translations remain incomplete. Office/school management enrollment/update settings remain missing. The four historical raw-image boots are not current Windows installation proof.

The next candidate uses `[release]` without `[latest]`: passing image gates may create a prerelease and run exact-installer Windows base/BitLocker journeys, but cannot make it the public latest. `[e2e:none]` suppresses only the independent push-triggered journey against an old pinned installer; the release's explicit artifact journeys still run.

## Delivery integration follow-up

The e2aceed3 candidate was cancelled after its Windows build caught a missing profile-refusal help row. The row is repaired; generic and personalized installers cross-compile with manifest, mitigation, DLL and embedded-resource checks. This is compile evidence, not a Windows installation result.

The natural-language request preview now runs locally and requires accepting its proposed choices. Multilingual images now wire IBus into the real user session and wait for both Wayland and Xwayland. Wiring tests pass; the profile probe has 34 positive/negative checks and requires live Pinyin engine evidence. Actual Pinyin typing in Mousepad and Firefox remains unproven.

The custom build API and publication workflow are integrated but disabled by default. They use a one-use bounded proof, atomic quotas, secret receipts, verified released-base commits, exact-tag dispatch, two-stage signing, public readback and immutable same-run manifest evidence. Combined cloud tests pass 71/71 and customization tests pass 46/46 before the final asset-replacement regression. Final review identified a missing comparison between the current release EXE digest and the immutable manifest; this is being repaired before the next push. Public promotion now explicitly refuses unsigned installers.

Next: integrate the tested browser service flow and final review fixes, push one coherent prerelease candidate with a multilingual profile build, and inspect its exact image/Windows results. Signing configuration, monitored contact/name decisions, physical-PC testing, managed-profile enrollment and held encryption work remain open. No feature is activated merely because its source tests pass.

## Personalized delivery certification candidate

The final download-integrity findings are fixed: current GitHub asset identity and SHA-256 must match immutable delivery evidence, ready results are rechecked after 15 seconds, and parser numeric tokens cannot be forged by input objects. The build API has 77 passing tests. Original verified delivery evidence persists separately from 24-hour secret receipts, so matching later requests reuse the signed installer without another build, even after Actions evidence expires. Live release/tag/asset integrity is still revalidated. Public privacy text and installed notices now distinguish those retention periods.

The browser flow has bounded local proof work, recoverable receipt polling and explicit failures. A measured 20-bit proof timed out after 30 seconds with roughly 49% expected failure on the tested browser; the disabled build service now defaults to 16 bits while preserving network/global quotas. Wishes limits and its zero-dollar activation switch are unchanged.

Personalization now crosses the Windows restart in a checked choices receipt. A personalized installer refuses if that write or flush fails, including after the person overrides a choice. An override does not retain the original recipe identity. First boot records applied recipe identity only after successful shell/theme rendering and atomic receipt commit; failure remains retryable instead of stamping completion. The ESP receipt is local correlation evidence, not cryptographic attestation. The installed-journey harness verifies files, first-boot completion and the running shell's actual archetype and loaded theme in its first session. New custom deliveries cannot publish before that exact signed executable passes the Windows base journey; repeated matching deliveries reuse that certification.

Generic and personalized Windows cross-builds at b9850c48 passed security/import/resource gates and both manifest suites (15/15 each); recipe and fault tests passed 10/10. This is compile evidence, not a completed installation. The repaired older image run 40124183660 at 5d1c50d3 has passed its full unit job, including Ferry; its remaining image jobs are separate from the new candidate. The local macOS burst load test hit its documented socket-backlog limit; its paced scenario passed, and the Linux cloud CI load test remains the appropriate burst proof.

The quiet-time Wishes adapter now acknowledges the engine's already-applied interruption suppression and rejects malformed events. Parser/engine/hook/schema tests and the Linux real-tree UI gate passed; the broad UI mutation sweep was stopped at its five-minute bound. This does not enable Wishes or claim system-wide notification control.

Work is backed up on codex/delivery. The next main checkpoint requests a prerelease and only the multilingual profile build, with independent stale-installer E2E suppressed. It does not request public latest. Remaining external prerequisites include SignPath/GitHub App configuration and a monitored contact; physical-PC proof, multilingual typing, managed profiles, encryption integration and exact-candidate install results remain open.

Final combined source checks before the candidate push: 77 cloud tests, 49 customization tests, 540 browser checks, all E2E harness selftests, 29 workflow files, and legal generation/checks passed. Public legal launch still refuses the existing contact placeholder.

## Active candidate follow-through

Candidate `ee450fbb` is on main. Its Windows startup/security/download workflow
and cloud tests (including the Linux burst load test) passed. Cloud deployed
with custom builds and Wishes still disabled. The image and multilingual
profile builds are still running; these results do not establish a completed
Windows install journey. Release workflow callers pass the exact published
installer URL/hash to both base and BitLocker journeys automatically.

The word check found one technical term in the privacy notice. Branch checkpoint
`95a9c478` repairs it and the stale privacy-page description; local word/legal
checks pass. The separate scamtest failure is being investigated before the
next main batch.

Remaining active slices are adoption of operator-precertified custom deliveries
(including concurrency review), a real Pinyin composition test across reboot,
and candidate CI follow-through. The shipped browser's 16-bit request proof
completed three fixed local challenges in 9.83, 1.79 and 1.79 seconds with no
timeouts. No live API requests or paid model calls were used for that test.

The older repaired image run `40124183660` at `5d1c50d3` completed successfully,
including its selected unit, boot and power-cut gates. This is not evidence for
newer source. The candidate scamtest failure was an assertion still looking for
the old direct session-key call. Its repaired test passes all 51 Linux checks
and rejects all 19 intentional mutants; production guard ordering was intact.

Operator-precertified delivery adoption is integrated as `31e45443`. It checks
the original run, signed and immutable evidence, current release identity and
asset digest before adoption. A per-recipe discovery lease prevents duplicate
dispatch from simultaneous requests. Security review and all 81 cloud tests
pass. The new browser error wording passes the 543-check site suite and the
plain-word check.

The opt-in Pinyin harness is integrated as `ed5c4812`. It drives physical QMP
keys into Mousepad and Firefox, checks their exact saved/submitted text, and
repeats on the same disk after a reboot. Its 41 self-checks and workflow lint
pass; actual typing remains unproven until the image job runs. The next batch
deliberately requests all four non-desktop profiles, with functional input
enabled only for multilingual. The main build supplies the desktop evidence.

Multilingual run `40126965872` at `ee450fbb` completed successfully. Its guest
report records the rail/sandstone desktop as user auros, French locale, no
failed units, active IBus, GTK/Qt/XMODIFIERS set, Wayland `wayland-0`, X display
`:0`, and an available Pinyin engine. Boot took about nine seconds in that VM.
This establishes wiring and boot, not application composition.

The theme-backed wallpaper adapter is integrated through `29eed4ee`. Independent
review caught and resolved a mismatched Undo field, helper timeouts reported as
success, and invalid previous themes. Tests now replay the actual saved Undo
action, kill/reap timed-out helpers, and use aurora to validate the previous
theme before applying a change. Linux real/inert Wishes UI tests and all 25
theme-helper checks pass. Raw colors remain refused and Wishes stays disabled.

Desktop isolation is covered by `4d3f3795` inside the existing first-boot VM:
IBus must be disabled, and all three IM variables must be absent from both the
live shell and user manager. Missing/query-error evidence fails. Parser
fixtures, shell syntax and ShellCheck pass; runtime awaits this batch's image.
This adds no extra build or VM boot.

## Follow-through after the all-profile checkpoint

The profile workflow now creates its evidence release once, then runs at most
two isolated build lanes. Run `40128386837` was replaced while its first job
was still installing tools; `40128867869` at `1e46ff34` runs all four
non-desktop profiles, with real composition enabled only for multilingual.
The workflow change does not rebuild the desktop image.

The browser now calls the real build API/compiler/ledger in a local integration
test. Only the GitHub verifier boundary is mocked. Proof acceptance, exact
recipe dispatch, opaque receipt/Bearer handling, ready rendering and reload
without redispatch pass; the full site suite passes 548 checks. This is not
signing or installed-OS proof.

The `ee450fbb` image run `40126965912` failed its unit gate because that job
omitted NumPy and Pillow before the E2E evidence selftest imported them. The
other workflow already installed these dependencies. `41a57102` aligns the
unit job; the full evidence selftest and workflow lint pass. The `f1c92a70`
desktop run `40128386534` was cancelled because its immutable workflow revision
has the same omission. Independent profile jobs continue. Neither candidate
is eligible for release on this evidence.

Base discovery now starts with canonical release assets and queries successful
build runs at the exact release SHA. Ordinary gate-only pushes and unrelated
evidence releases no longer hide a usable base within the documented bounded
search. It refuses ambiguous runs and incomplete artifact pagination, uses at
most 26 GitHub requests, and passes all 82 cloud tests and security review.
The exact-SHA GitHub query was also checked against real successful run
`40124183660`. Public build requests and Wishes remain disabled.

## Runtime checkpoint, 24 October 20:36 UTC

- Main candidate `d7a5675b`, build `40129427319`: image and installer built;
  full test jobs are running. Its cloud workflow passed with feature switches
  off. This candidate includes the missing test dependencies.
- Profile run `40128867869` at `1e46ff34`: office, revive and school-kiosk
  build/boot jobs passed. Multilingual built and is running the two-boot Pinyin
  application test. The ordinary desktop has its own image/test run above.
- Office and Revive reports show their expected rail/slate and taskbar/ember
  desktops, non-root user auros, Firefox installed, and no failed units.
- Earlier image run `40126965912` at `ee450fbb` finished with 32 successful jobs
  and only the dependency-broken unit job failing. All 16 selected VM jobs,
  four additional power-cut jobs, compositor checks/mutations, Wishes UI/voice,
  and theft checks passed. This is not a successful release or real-Windows
  restart proof; its release stage was skipped.
- Site run `40129160239` at `16cad481` passed the 548-check suite including the
  browser-to-real-API test with mocked GitHub verification.

No new desktop image is requested by this documentation checkpoint. The
original Claude checkouts and held encryption work remain preserved.

## Follow-through, 24 October after 20:40 UTC

The multilingual functional run `40128867869` failed before typing: Mousepad
opened and was focused, but IBus engine selection failed. The exact installed
engine is `pinyin`; this is not evidence of a libpinyin naming mismatch. The
old helper discarded the setter error. The repaired helper retains engine
inventory, setter/getter status and stderr, retries at most three times, and
requires the exact observed engine. All 42 harness checks and independent
review pass. Application composition still requires another boot.

Candidate `d7a5675b` run `40129427319` passed Wishes UI plus its 36 mutations
and gallery checks, then failed paint and end-to-end Wishes tests. These are
being diagnosed before requesting another desktop candidate. Other image
gates continue; the candidate cannot release with these failures.

To avoid repeatedly rebuilding multilingual for harness-only diagnostics, a
bounded retained-image path is being implemented. Its immutable small
manifest will bind the image bytes; reused-image evidence will name both
image-source and test-source commits and remain diagnostic, never a new
production certification. No extra paid service is introduced.

The full unit job of `40129427319` subsequently passed. Wishes paint and
end-to-end failures were missing linker seams in their standalone C harnesses,
not observed product behavior. `472011e3` provides explicit failing theme
stubs in those two unrelated-action harnesses. Linux paint checks pass, all
eight paint mutants are caught, and real Wishes end-to-end passes 19/19.
The full theme adapter retains its separate real-helper/Undo coverage.

The first-boot VM gate at `d7a5675b` also passed, including the ordinary-desktop
IBus negative control. The next batched candidate contains the Wishes harness
link repairs, bounded IBus diagnostic repair, and reviewed retained-image
workflow. Combined checks: 42 profile selftests, 12 adversarial retention
tests, all 29 workflow files, plus the Linux Wishes results above.

The new push requests only multilingual with real composition and a desktop
prerelease candidate. It does not promote public latest or enable build
requests/Wishes. The prior failed desktop run remains useful evidence and
is allowed to finish its remaining independent tests.

## Candidate checkpoint, 24 October 21:02 UTC

`c1ba6ee5` is pushed to main and codex/delivery. Desktop run `40131473307`
has built its installer and started full tests. Multilingual run `40131473294`
has built its image and started the real two-boot typing test. The retained
image path passed its 12 adversarial fixtures and independent review; all
42 integrated profile selftests and workflow lint passed before the push.

Previous desktop run `40129427319` at `d7a5675b` finished with 32 successful
jobs and only the Wishes UI job failing at the now-repaired standalone
harness links. All selected install, first-boot, network-assumption, offline
store, update/rollback and power-cut gates passed. The release was correctly
skipped. This remains separate from the real Windows restart journey.
The earlier staging KVM run `40126965922` also completed successfully.

## Follow-through after candidate tests

Candidate `c1ba6ee5`, build `40131473307`, completed every selected test job
and entered prerelease publication. However, the restore test took 1,304
seconds and its old harness silently accepted a 1,200-second boot timeout.
Its 24 assertions are insufficient to close restore reliability. A focused
repair will fail immediately at the deadline, retain serial, framebuffer and
guest evidence, and support a clearly labelled single-test diagnostic using
the existing candidate artifacts. This diagnostic is not release certification.

Multilingual production run `40131473294` passed exact Pinyin composition
in Mousepad and Firefox before and after reboot, and retained a validated
image. Screenshots still expose a missing candidate chooser and a technical
IBus warning. Identity diagnostic `40134236489` authenticated the candidate
with server-derived XRes PID and restored normal composition afterward.
Focus lifecycle diagnostic `40135172647` is now running against that same
image; no compositor exception has been implemented. See
`MULTILINGUAL-INPUT-2026-10-24.md` for the trust boundary and stop conditions.

The exact Windows base, BitLocker and fast-path journeys remain required.
Public delivery, signing configuration, monitored contacts and physical-PC
proof remain unresolved; automated prerelease publication does not close them.

## Working-branch verification follow-up

The candidate published as `v0.1.0-c1ba6ee`; its published-executable and
WinHTTP download checks passed. Exact base, BitLocker and fast-path base
installation journeys are running within build `40131473307`. The corrected
restore diagnostic is run `40135313128`, harness `1652e904`, against that
candidate's immutable artifacts. It remains diagnostic, not certification.

Branch-only dispatch works without first adding the dispatch trigger to main.
This avoided a replacement image build for the restore investigation. Future
putback jobs now omit an unused 1.53 GB rootfs download and staging-tree setup
while retaining image assembly and exact checksum verification.

Review of generic appearance assertions exposed a real firstboot failure path.
Generic `aurora set` failure previously allowed a completion stamp after partial
rendering. The working branch now withholds completion, returns failure and
retries next boot, with a terminal marker only after successful rendering and
exact ACTIVE theme readback. A failing-before/fixed-after partial-render test,
successful retry and personalized-render skip all pass. Independent review is
clean. Generic E2E now requires this exact marker plus zero recipe IDs, exact
persisted choices and live first-session appearance. The older c1 image cannot
meet the new marker contract; do not waste a diagnostic run pretending it can.
A fresh candidate must certify this product change.

Fastpath release E2E now selects both base and BitLocker on the working branch.
Fastpath remains candidate-only and outside promotion because the website's
shipped route still uses the normal installer. No promotion policy was weakened
or silently expanded.

The IBus cross-client panel exception was rejected after source review found
that renderer identity does not authenticate candidate contents. A bounded
Fcitx client-side popup experiment is being prepared on the retained disposable
image; no shipped input-method or compositor privilege has changed.

Corrected restore diagnostic `40135313128` has now passed: six boots, all 24
checks, 114 seconds of VM testing, exit 0. Its provenance binds the image to
`c1ba6ee5` and the harness to `1652e904`; no timeout was accepted. Setup took
687 seconds, supporting removal of the unused rootfs transfer in future runs.
The earlier stall remains unexplained rather than claimed fixed. The strict
fail-fast harness is included in the next candidate's ordinary gating suite.

The next desktop candidate includes the reviewed generic-firstboot repair,
explicit default-appearance proof, fastpath BitLocker coverage, strict restore
deadlines/evidence and the reduced restore setup. Its release marker starts
exact-artifact tests; the independent Fcitx experiment remains off-product
and will use a branch-only retained-image dispatch after its own review.

## Manual screenshot review invalidates launch verdicts

Do not treat the green c1 base or fastpath E2E result as end-to-end app proof.
The final base screenshot (`0054-program-open.png`) actually shows the VLC
store listing and a “Sorry, something went wrong” toast, with only Software
on the taskbar. The fastpath final screenshot also remains on the store
listing. Neither proves a running VLC application. Network, catalogue and
installation assertions remain separate evidence; the launch verdict is a
false positive. Product error diagnosis and strict UI launch-proof repair
are active in independent worktrees.

The candidate cannot automatically become latest: its subject has `[release]`
but no `[latest]`, and unsigned releases are independently refused by the
promotion gate. Preserve the running BitLocker evidence instead of cancelling
the workflow. A fresh signed candidate with corrected app-launch proof is
still required for public latest.

New desktop candidate `beddd205` is building in `40136360732`; it contains the
reviewed generic-firstboot fix but not the newly discovered launch repair.
The firstboot retry regression is additionally wired into the working branch's
unit workflow for the next source batch.

The Fcitx branch dispatch `40136687367` correctly refused execution before
boot because retained-image diagnostics are main-only. Reviewed diagnostic
changes were then moved to main as `d8f09685`, excluding the unrelated unit
workflow change to avoid an unnecessary image build. The real experiment is
`40136819107` against retained image source `40131473294`; production input
configuration remains unchanged.

Both base and fastpath journals identify the launch failure: `/usr/bin/bwrap`
enters a user namespace, receives the generic `unprivileged_userns` AppArmor
restriction, and is denied `setpcap` and `net_admin`; loopback setup then fails
with `RTM_NEWADDR: Operation not permitted`. A narrow upstream-derived bwrap
policy, preserving restrictions on executed children, is under independent
review. No global AppArmor or user-namespace setting will be disabled.

Build `40136360732` for `beddd205` was deliberately cancelled by the lead
before repeating the full test suite on an image known to lack this launch
fix. Its reviewed firstboot changes remain committed and will be included in
the next candidate. The c1 BitLocker journey is preserved and has reached
the actual installer/first-session phase.

## Current verification checkpoint

The c1 release run `40131473307` finished failed. Its BitLocker journey used
normal `AurOS-Setup.exe` and stopped at the legacy installer's intentional
locked-drive refusal, before modifying the drive. This is not evidence about
fastpath correctness. Fastpath must still qualify on exact published bytes.

The strict app checker now rejects the recorded base and fastpath false-green
screenshots. It requires a separately attributed VLC task, foreground title
and application controls; the optional VLC privacy dialog is handled once and
does not itself count as launch proof. Integrated E2E selftests pass, including
recorded false-positive fixtures. Dry-run servers use ephemeral ports so
parallel tests cannot collide.

Fcitx diagnostic `40136819107` failed before composition with
`exact_pinyin_engine_unavailable`. No popup or composition result was proved;
raw-trace cleanup and restoration of the shipped IBus configuration passed.
A focused diagnostic repair is underway, without changing production input.

The fastpath audit found a disclosure and verification gap: current code uses
BitLocker `RebootCount 2`, while some text promises a single restart pause.
Protection is suspended with a clear key available until Windows resumes it;
remaining in AurOS does not establish that Windows protection has resumed.
Correct the misleading wording and measure first/second Windows return state.
Do not flip the default before exact-artifact BitLocker return and recovery
proof. The real BitLocker file-preserving put-back journey remains unproven.

The proposed bwrap policy includes upstream's `mediate_deleted` correction on
both profiles and the subsequent stacked application-execution rule. Static
policy checks and mutation tests pass; a bounded homebase VM probe is testing
child capability restrictions and atomic saves before integration. This cached
image is diagnostic evidence only, not a release certificate.

The optional exact-SHA fastpath BitLocker lane now includes a file-preserving
put-back test (`ffc27785`). It retains store/app launch proof, records sanitized
BitLocker state on both Windows returns, and checks original Windows and restored
AurOS file hashes, original partition/firmware state, task/volume cleanup and Fast
Startup. Recovery secrets are excluded. Independent review, integrated E2E
selftests and all 29 workflow lint checks pass. No real run of this extension has
occurred yet; it must use the next fresh candidate, not the invalidated c1 image.

The focused Fcitx ordering repair was pushed to main as `f472874d`; retained-image
run `40137862304` got past engine availability but failed at
`Mousepad_parented-popup_syntax_missing`. Composition/candidate suitability remain
unproven. The next diagnostic must explain this evidence gap before another run;
no production input change has been made.

Queued duplicate old-image hardening run `40137331627` was cancelled to avoid
repeating the still-running `40136360642` check. Neither can certify the upcoming
bwrap-policy candidate, which will require its own exact-image gate.

## Reviewed Flatpak repair and BitLocker wording

`82423122` integrates the narrow upstream-derived bwrap policy. Static hardening
passes 108 checks and catches all 82 mutants. Independent security review passed
all 19 focused retained-image VM predicates: direct bwrap attachment and stacked
child, successful atomic save, zero ordinary child capabilities, and denial of
an explicitly retained NET_ADMIN capability. Removing mediate_deleted produces
the observed errno 2 with deleted-link audits; allowing capabilities makes the
same network operation succeed. Evidence is `/tmp/auros-vlc-runtime-evidence.tar.gz`.
These diagnostic boots do not certify the new product image or actual VLC launch.
Normal release hardening remains full; shortened injected-profile diagnostics
cannot satisfy release certification. Owned remote processes/disks were cleaned.

BitLocker wording is corrected in `8db0e865`, including failure messages and the
unpublished legal draft. The site gate passes 556 checks. Exact generic and
fastpath cross-builds pass, with both manifest suites 15/15 and byte-identical
embedded wizard resources. The Ready layout fixture compiled, but no display was
available for local execution; an explicit same-run fastpath Windows navigation
job is being added so that fixture actually executes.

Fcitx trace parser repair is on main `5f98accc`; diagnostic `40138556854` now uses
the actual libwayland `interface@id` syntax. The preceding screenshot visibly
contains a candidate row, but exact 你好 composition, popup attribution and
connection identity remain unproved pending the bounded diagnostic evidence.

## Candidate e0d0aadc and follow-up diagnostics

Fresh candidate `e0d0aadc` is building/testing in `40138682256`; it is a prerelease
candidate, not a latest promotion. Its Linux image has built and entered the full
suite. Windows workflow `40138682232` passes. Fastpath's new navigation job exposed
a stale no-scroll expectation; test-only follow-up `a4e50572` instead proves footer
visibility and bounded scrolling to the final choice. Branch-only Windows run
`40139104228` passes all jobs, avoiding a redundant OS rebuild.

Manual screenshot review of that passing navigation fixture still found a product
understandability gap: the BitLocker warning can be below the fold while Install is
enabled. Actual suspension occurs later at Restart now, immediately before ab_arm.
A dedicated accessible confirmation at that point is being implemented separately;
no default flip or public promotion is justified by the scrolling test alone.

First-stage Fcitx retained-image run `40139052850` passes. Mousepad and Firefox both
produce exact UTF-8 你好; each trace contains one mapped parented popup with positive
geometry and no grab/layer-shell. Retained screenshots show the candidate row and
committed/submitted text. Owned daemon identity stayed stable, temporary processes
and raw traces were removed, and shipped IBus/config were restored. Evidence is
`/tmp/auros-fcitx-40139052850.7dIc8C`. This does not prove connection identity,
focus revocation, password suppression or reboot behavior. Those are the next
explicitly opt-in diagnostic stage; production input configuration remains IBus.

The restart-time confirmation is integrated on the working branch (`0c89532a`,
with exact UIA/cancellation proof in `c3f71c75`). Windows run `40139923016` passes
its fastpath navigation job; the retained tight screenshot shows the full warning,
all actions, and initial Not yet focus without scrolling. The combined E2E
selftest passes. Later copy-only `ad8446cc` says cancellation leaves protection
unchanged, rather than incorrectly assuming Windows had not already paused it.
The actual new confirmation journey is still pending a future exact installer.
These changes are not yet in candidate `e0d0aadc` or remote main.

Full exact-image hardening for candidate build `40138682256` was dispatched early
as `40140266950` (`only=all`), to overlap the rest of the suite rather than wait
for all Windows journeys. It uses the published Actions image pieces and verifies
their hashes. An automatically triggered duplicate after build completion can be
cancelled before work if this same-image run already supplies the full verdict;
never cancel or disregard a real failing result.

## Verification and CI efficiency follow-through

Candidate `40138682256` has passed 31 jobs; failure-display and kernel rollback
VM tests are still running. Full hardening `40140266950` remains in progress.
Neither status is a completed installation or release verdict.

Second-stage Fcitx diagnostic `40141218804` failed before composition because
the harness required the service MainPID to remain in its service cgroup.
The PAM session moves it into a session scope. The diagnostic owner is repairing
the attribution check while retaining exact service/process/socket identity
checks; production input remains unchanged.

Working-branch `35f8b12e` removes unnecessary CI artifact transfers: nine
image-only jobs skip installer and rootfs downloads, boot-update also skips
rootfs, and staging-module extraction is limited to its actual consumers.
Same-run image pieces and both SHA-256 verification layers are unchanged.
All 29 workflow files and expanded matrix requirements passed validation.
The change will take effect on a future candidate, not the running one.

Follow-through: all candidate image tests passed and prerelease
`v0.1.0-e0d0aad` was published. Its ordinary and fastpath base/BitLocker Windows
journeys are running. The release Windows navigation job failed the same stale
fastpath no-scroll assertion already repaired on the working branch; its later
download checks therefore did not run. Do not call this release green or retry
the unchanged executable to hide that result.

Retained candidate evidence in `/tmp/auros-candidate-firstsession-40138682256`
shows DHCP/route/catalogue success with the network fix and failure without it;
the offline store screenshot visibly finds VLC. Neither proves VLC launch.
The deliberate desktop-failure test also passed. The PAM attribution repair
passed 31 focused tests and review, is on main `fdfa1116`, and is running in
diagnostic `40141791633` against the same retained multilingual image.

Full hardening run `40140266950` now passes: 72 runtime hardening checks and
14 clock checks. `GITHUB_REPOSITORY=aarohkandy/v0.0.1-auros sh
tools/hardengate.sh 40138682256` accepts this exact-image verdict. Reports and
runtime evidence are retained in `/tmp/auros-harden-40140266950`. This establishes
the tested controls, not absence of every possible vulnerability.

The next multilingual diagnostics exposed two further harness assumptions:
`40141791633` counted accepted socket records as listeners; `40142411622` counted
duplicate file descriptors for one listener as distinct listeners. Typed socket
checks and `kcmp(KCMP_FILE)` identity now distinguish these cases while refusing
actual conflicting listeners. Main `bdfe4ae8` contains the reviewed repairs;
41 focused tests pass. Diagnostic `40142892395` is running. Neither failed run
reached the typing, focus, password or reboot proof. Failure evidence now includes
bounded application connection/trace counters as well as compositor identity.

## Installed candidate failures being repaired

Fastpath base job `121014781615` reached the installed desktop in 7m49s from
the test's Windows start, then clicked the search-entry text at `(343,162)`
instead of the visible VLC result. It never reached Install. Evidence:
`/tmp/auros-e0-fastpath-base-40138682256/unpacked`. The installed session proved
network/DNS/time/security/mount checks, but also exposed a real startup race:
NetworkManager sends SIGUSR1 after systemd marks the simple software-preparation
service active and before its shell installs a trap. It dies, waits 60 seconds,
then succeeds. Fix the race; do not whitelist its failed-unit evidence.

Ordinary base job `121014779480` installed VLC, then Open never produced its
window. Screenshot `0057-program-did-not-open.png` is still the Software detail
page with its embedded preview. The strict launch check correctly failed.
Evidence: `/tmp/auros-e0-base-40138682256/unpacked`. This run has the bwrap policy
loaded and no corresponding launch-time bwrap denial in its retained journal;
the remaining launch failure is under independent diagnosis, not assumed fixed.

Owners are separate: CI coverage fixes search-result selection; the service-race
debugger fixes production readiness; the app-launch debugger investigates VLC;
the multilingual owner finishes trace attribution/restoration diagnostics.

The software startup repair is integrated as `96ace8a0`: Type=notify announces
readiness only after the USR1 trap exists, and the dispatcher only signals an
active service. The old source failed seven focused checks; the fixed source
passes 31, the existing mutations, and an actual Ubuntu 24 systemd startup/wake
probe. Independent review passed. Full next-image confirmation remains required.

`84f16361` integrates the result-row selection and BitLocker helper routing
repairs. Guest evidence proved both put-back scripts clobbered helper.ps1's `$c`
command envelope with `Get-Partition`, sending an immediate blank-ID reply.
Renaming the local preserves all original baseline and restoration checks and
the timeout. Integrated E2E selftests and independent review pass.

Duplicate same-image hardening run `40143618949` was cancelled during artifact
download, before tests began, because full verdict `40140266950` already passed.
VLC-focused exact-image parity run `40143621769` is testing the launch failure.
Multilingual diagnostic `40143674648` uses main `7192fbd7` with the probe's own
authenticated trace descriptor accounted for and bounded IBus readiness polling.
No new product image has been built while these diagnoses are in flight.

## Exact-image launch and diagnostic proof follow-up

VLC parity run `40143621769` passed direct exported-desktop launch on candidate
`e0d0aadc`; this does not prove Software Install → Open. The initial Wayland
socket hypothesis was refuted in controlled experiments. A local click initially
reported as Open was invalidated by screenshot review; a later recognized Open
launched VLC. The exact SHA-verified candidate is now being exercised through
the same-process store path with verbose Software logging. No speculative
production workaround has been integrated.

Multilingual run `40144734247` left passing round-two live evidence at the root
but lost the copied round directory on immediate VM shutdown. The post-copy
sync repair is integrated and run `40145440411` checks it. Rechecking old review
findings exposed two unclosed boundaries: a stale earlier focus marker could
satisfy the click proof, and independent line-presence checks do not reject
duplicate/contradictory/unknown password evidence. These are assigned for a
focused repair. Earlier ancestor-symlink and live-process cleanup findings are
closed. Do not claim complete sensitive-field proof from the current run.

A retained password screenshot also contains the shipped IBus Wayland-session
warning. Execution ordering and source conditions support boot-time origin,
but that artifact has no journal export; do not label attribution journal-proven.
Production input architecture and that visible warning remain separate work.

For the next candidate, one main push headed `[release] [e2e:none]` runs the
prerelease build and four exact-installer Windows journeys. Allow its automatic
exact-build hardening workflow to start after completion rather than manually
duplicating it. No `[latest]` promotion is authorized by the present evidence.

## Next-candidate preparation (25 October, 00:20 UTC)

The byte-exact `e0d0aadc` image (raw SHA
`8aa04990bd0e32918fd6cfc77b5c9b5797076ee175d2ce04c88c348294a0aa84`)
launched VLC through Software Install → recognized Open in 11 seconds in both
a verbose diagnostic and an untouched-image repetition. Both reached a real
VLC privacy dialog and main player; the old harness incorrectly rejected the
wide dialog's Continue placement. `4591d00e` integrates the reviewed correction,
using mapped window identity plus nearby client controls and dialog attribution
before one default action. Store-preview-only negative cases remain refused.
Integrated E2E selftests pass, including the new image-integrity regression.
A final live test of the corrected dialog action is running. The original Open
no-op remains an unresolved intermittent issue: shared GNOME Software launch
cancellation during post-install reload is source/log-supported, not yet a
controlled reproduction. No distro package fork or extra settle delay was added.

`5401e288` / `bc3815d0` verify compressed pieces and reconstructed raw hashes in
the standalone AurOS diagnostic lane. The real workflow step passes a valid
fixture and rejects corrupt pieces, wrong raw digest, and missing metadata.
The four Windows release journeys already verify their installer bytes.

Multilingual `40145440411` stopped at the second Mousepad visibility check;
it does not prove whether a surface failed to map or the visual detector missed
it. `40145726339` attempt 1 never reached Fcitx: Mozilla's APT index did not
match signed Release size/hash during publication. Attempt 2 retries that
infrastructure failure on unchanged main `3b2fd5ea`. That source includes the
fresh focus-marker boundary and closed 15-key password evidence schema.
Early unavailable exits also needed an evidence sync before the host stops QEMU;
`a01e2b27` integrates the reviewed three-path repair and ordering regressions
without changing package verification.

## Candidate held: silent Open failure reproduced again

The third untouched-image runtime run, using the corrected dialog harness,
clicked a recognized Open at 00:20:16 and failed its unchanged launch deadline
at 00:22:21. Only the store preview remained. The earlier two successful
launches therefore do not close the real intermittent product defect. No next
full candidate has been dispatched. Evidence is on homebase under
`~/work/auros-vlc-exact/out/patched-diag/aur-journal.txt` and the associated flow.

This recurrence has no canceled PackageKit transaction: resolve work completed,
but no Flatpak application activation appeared. The shared-cancellable theory
is not confirmed and must not drive a speculative package patch. A bounded
diagnostic now traces the management-plugin ownership boundary in stock
GNOME Software; missing ownership is a hypothesis, not a finding yet.
Any eventual downstream package repair must preserve Ubuntu security-update
supersession and exact source publication. A diverted/frozen replacement is
not acceptable. Diagnostic instrumentation is not a production change.

## Combined candidate ready for exact-image verification

`d91e61eb` fixes the demonstrated stale Open-coordinate boundary: the pointer
settles first, then the exact unique control is read again and must contain
the pointer before button-down. Refresh cancels the press within the existing
readiness deadline. Dynamic refresh-during-dwell and exactly-one-press tests,
independent review and integrated E2E selftests pass. UI observation/input still
have an unavoidable scheduling gap; the next exact-image journey is required.
No store package patch has been applied. The no-window failures alone do not
prove a GNOME Software no-op because actual callback activation was unproved.

The attempted ownership interposers were inconclusive: `/usr/local` overrides
the package launcher, and the modified entries did not start the live process.
The earlier purported verbose run likewise must not be cited as a verified
verbose trace. Successful and failed visible journeys remain valid observations,
but neither guessed cancellation nor absent ownership is established. A direct
stock-binary control is being prepared with explicit process/hook validation.

Multilingual diagnostic `40145726339` attempt 2 passed on harness `3b2fd5ea`.
Use artifact **12581007423**, not stale same-name first-attempt **12581236003**;
both round result directories and masked-password screenshots were checked.
This is the bounded retained-image proof described in the multilingual handoff.

`77c69144` and `9c7ab532` implement a multilingual-only Fcitx GTK stack and
separate fresh-production certification, preserving retained IBus diagnostic
semantics. Input wiring tests, 73 profile selfchecks, independent security
review and workflow lint pass. A fresh image has not yet certified it. Other
profiles keep their existing input-method behavior. Public profile delivery
remains gated; Firefox compositor connection identity is still unproved.

Batch one `[release] [e2e:none] [profiles multilingual] [ibus-functional]` push:
one desktop candidate with four Windows journeys, plus one fresh multilingual
image/production functional test. Let exact-build hardening start automatically.
No latest/public promotion. The next results must settle any remaining launch,
BitLocker-return, recovery, and shipped-profile failures.

## Candidate 3629b114 and corrected dispatch-control evidence

Combined main candidate `3629b114d4bcd1ff74a18fbeb3072bc9d5512110` is under
[build run 40148012698](https://github.com/aarohkandy/v0.0.1-auros/actions/runs/40148012698)
and [multilingual run 40148012574](https://github.com/aarohkandy/v0.0.1-auros/actions/runs/40148012574).
The same-commit standalone push E2E is intentionally skipped; release-called
Windows journeys must run against this candidate. Do not manually duplicate
hardening. The legal check passed; no image/installed-journey verdict yet.

The direct installed-VLC control finally verified actual instrumentation in
stock GNOME Software: action=5 launch job, exact Flatpak ownership/object ID,
libflatpak success, and an app scope. Its screenshot **does not show mapped VLC**:
lead inspection found only Software's embedded Big Buck Bunny preview. The
diagnostic driver falsely matched preview text and stopped too early. The
agent's initial mapped-window/root-cause claims were corrected; no production
code uses that diagnostic driver. This control proves dispatch only, not first
install, visible app readiness, or root cause of the earlier failures.

Corrected evidence is `/tmp/auros-vlc-focused-control/focused-control-evidence/`,
with `README.md` and `SCREENSHOT-INTERPRETATION.md`; archive SHA-256
`abaac545801ff47c7aa94a8cbbedc886ee4e0615e5aad6d238b8cc09e905a1a2`.
Remote mirror is `~/work/auros-vlc-exact/out/focused-control-evidence/`.
Owned diagnostic overlays/processes were removed; exact original raw image
remains verified. Earlier `0039-failure.png` genuinely shows VLC privacy/main
windows but belongs to a different run. Keep these claims separate.

The dispatch-control evidence now also contains the exact original interposer
source and driver plus reproduction instructions. Rebuilding at the recorded
path reproduced the shim hash. Its visual predicate defect remains explicitly
documented; the preserved code is diagnostic-only.

Fresh multilingual run `40148012574` passed 48 checks on candidate `3629b114`;
lead inspected the actual Mousepad/Firefox candidate screenshots and exact
`你好` outputs. Raw image SHA-256 is
`42359c4847a1a696a21c5b5350f4574a2970bb1d4d8f7df42c8336157a1696fa`.
This is one production functional boot, not the retained image's two-round
security proof and not a multilingual Windows-install journey.

Default-promotion preparation exposed a separate Wi-Fi persistence issue:
plaintext carry-over data is written before Restart later returns. The next
installer fix must delay that write until Restart now/arming, disclose the
password copy beside the checkbox, and test waiting/abort/failure paths.
Fastpath remains non-default. Transfer-report text now distinguishes Ferry's
no-password behavior from Setup's optional connected-network carry-over.
