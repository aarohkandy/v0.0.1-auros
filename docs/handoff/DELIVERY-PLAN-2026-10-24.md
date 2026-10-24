# AurOS delivery plan

Owner authorization on 24 October 2026: implement, test adversarially, use bounded parallel agents, and push verified checkpoints to `aarohkandy/v0.0.1-auros`. The target is a nontechnical person describing the system they want, receiving it quickly, installing it safely, and using it without a terminal or helper.

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
- [ ] Wishes UI owner: original UI failures repaired; new theme adapter passes UI mutations but `d7a5675b` exposes paint/end-to-end regressions under investigation. Feature remains disabled.
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
