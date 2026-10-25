# AurOS delivery plan

Owner authorization on 24 October 2026: implement, test adversarially, use bounded parallel agents, and push verified checkpoints to `aarohkandy/v0.0.1-auros`. The target is a nontechnical person describing the system they want, receiving it quickly, installing it safely, and using it without a terminal or helper.

## Current checkpoint (25 October, 05:22 UTC)

Chronological entries below preserve failures, prior hypotheses and superseded evidence.

- Candidate `3629b114` fastpath base journey is now proven by `40153730622`: exact EXE `b5440e94d2a576e38003a2ff69bb440bc60a496cbe72e405c6ba1fd948a54e26`, website through Windows installation/restart and real installed VLC controls in 9m29s. Lead inspected the final actual-player screenshot. This is Windows 11 VM proof, not physical-PC, personalized signing, or later product changes.
- Exact-image hardening `40151838977` passed 72 hardening and 14 clock checks. `hardengate.sh 40148012698` accepted exact source/image binding. Original candidate build/release `40148012698` remains failed because its earlier Windows harness lanes failed; it must not be represented as an overall green release. Full image suite, prerelease readback, navigation/download and site/source publication passed.
- Exact BitLocker/putback run `40157169797` is CANCELLED/INCONCLUSIVE. FullyEncrypted/On/100%, baseline and exact EXE hash passed; named MSAA controls worked through Install. No visible progress for the final20 minutes; Restart, AurOS and putback were not reached. Artifact SHA `46494186fa27db0989140fa8282c6354420df9bf214799a9234f993cbd4f5543` under `/tmp/auros-40157169797-final.86OnPP/`. Code review found a concrete unbounded serial UIA/MSAA reader risk, not a confirmed root cause of this run. Bounded diagnostics, independent disk counters and per-request child-process accessibility isolation are integrated and pushed on delivery/candidate branches. Native Windows proof passed in full workflow `40161803540` at candidate `6a732d79`; replacement encrypted-drive run `40161971669` proved installation and real VLC but failed boot-menu capture before Windows return; its recovery remains unproven. Default promotion `65f55242` remains held.
- Production multilingual diagnostic `40156605186` passed 71 checks across two real boots of candidate `3629b114`: exact composition, visible candidates, authenticated Mousepad focus transitions, standalone password value/focus/no composition and zero popup events through shutdown, service identity/autostart, unchanged settings and cleanup. Firefox connection identity and native form/navigation/password-manager behavior remain unproven. Artifact `12585600815` retains the evidence.
- Delivery-only cache change `66ea5776` reuses the clean base Windows cache for BitLocker tests. Encryption is enabled and verified only in the disposable disk/firmware/TPM copy. Scoped independent review, E2E selftests on the worker branch and integrated workflow lint passed. It is held out of main for the next batched candidate, leaving the current test unchanged.
- Delivery-only Wi-Fi fix `7210f1ef` / `f1683bd9` defers plaintext carry-over until Restart now, cleans uncertain failures and discloses the copy. Focused Windows `40149373093` passed seven jobs; normal/large-text screenshots were inspected. This is not in candidate `3629b114`.
- Delivery-only cloud/customization/publisher integration passes 125 cloud tests, 56 customization tests, 38 publishing tests, 30 workflow checks and 560 browser/API checks. Public build delivery remains disabled. GitHub App, signing configuration, actual signed personalized delivery and owner setup are still required; no remote secret or service activation occurred.
- Delivery-only night-light action `b1902a24` + `00e97b1e` implements strict boolean input, prior-state Undo, untinted screenshots and nonaccumulating full-frame tint. Lead found and worker fixed a reachable stale-layout sizing defect. Linux validation: 22 pixel/safety checks with sanitizers, 22 real-engine e2e checks, 8 UI checks, 37 mutants killed, both enabled/disabled shell builds, independent review. Wishes remains disabled/zero-budget; no physical-device performance claim and no image rebuild yet.
- Main-based candidate branch `codex/product-batch-prepromotion` in `/tmp/auros-product-batch-prepromotion` is assembled through `a1a8caf5`, pushed for isolated Windows/recovery verification and without the held default. New site activation consistency, deterministic cloud concurrency, dormant profile update isolation/signed-index verification, Revive runtime probe and scoped installer ETA changes are included. Combined cloud108/108, site556/556, update85/85, profile reducer23, existing profile105 and workflow29 checks passed; native ETA build/nav/selftest passed under Wine. These are bounded local/integration results, not a green new image release.
- Revive functional probe covers Firefox and AbiWord physical-input/document use under2GiB with exact process/display identity and post-use health checks; runtime remains unproven and Gnumeric remains untested. `[revive-functional]` selects its next image run and two-day exact-image retention. Personal profile update tooling is dormant; no non-desktop image channel or publishing was enabled.
- Five profiles share the Ubuntu base; managed enrollment, remaining Wishes actions, held encryption integration, physical-PC campaign, product name/contact and public release conditions remain open. No full-product completion or zero-vulnerability claim is justified.

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
- [x] CIW regression coverage is integrated in the unit and shipped-image suites; the candidate image suite passed. Exact Windows and physical-machine proof remain Stage 3 gates.
- [x] Independent handover review: kernel/modules, network/time, mounts, devices/display, PID1/services reviewed and concrete findings repaired. Runtime installed-session proof remains required.

### Stage 2 Build a reusable customization path

- [x] Contract owner: strict schema, stable profile catalogue, deterministic compilation, policy preservation, hostile-input tests. Cross-Python depth behavior and dirty-CI fixture repair are included in the follow-up checkpoint.
- [x] Website owner: accessible request/review flow and honest artifact status implemented; 548 browser checks pass, including the real local API/compiler/ledger with only GitHub verification mocked. Public build service remains disabled.
- [x] Delivery adapter implemented: bounded authenticated requests, fixed build workflow, exact base/artifact/digest binding, immutable signed evidence validation and stale/mismatch refusal. Service activation and one real signed personalized journey remain Stage 3 gates.
- [x] Installer/first-boot adapter implemented: validated baked desktop recipes, checked restart handover, allowlisted atomic application/receipt and exact installed-state certification checks. A real signed personalized Windows installation remains unproved.
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

## Disabled delivery configuration checkpoint

The workflow now stages the three custom-delivery secrets via stdin and passes
the two App IDs as variables. Explicit enabled deployments require complete
validated settings and pre-staged secret names. Runtime admission independently
requires positive App IDs and an importable RSA key, covering manual deployments
that bypass the workflow. Missing/invalid settings return opaque disabled
responses. The missing-App-ID regression failed before the fix.

Worker commits `409a0b75`, `d0eca3cb`, `09f81175`, and `e3992253` were reviewed
and integrated as `b6091c96`, `d1839444`, `f5c5c218`, and `21b936bd`. Independent
reviewers were `installed_vlc_launch/config_gate_review` and
`delivery_deployment_wiring/wiring_final_review`. Lead caught the browser
fixture integration dependency and corrected an unsupported assumption that
the old Worker is disabled on every future enabled deployment.

Combined cloud tests: 100/100; workflows: 29 valid; site fixture: 556/556.
Owner App/signing configuration and disabled live-route proof remain required.
No activation, real-model call, remote secret read or deployment was performed.
A future main push touching cloud files can run the existing cloud deployment
workflow; its two service switches must remain off until activation requirements
are met.

## Shipped multilingual two-boot certification dispatched

Worker `703709d1` adds the explicit, mutually exclusive retained
`fcitx_production_security_probe` mode. It distinguishes shipped Fcitx from
legacy IBus conversion and judges exact service/configuration/password
environment records, real distinct boot IDs, and unchanged configuration.
The password Firefox test inherits the production input environment instead
of forcing the test module/configuration. Firefox connection identity remains
unproved. Harness selfchecks: 80/80; gate and workflow checks passed.

Only these four diagnostic files were pushed to main as `8a0d002d`; the cloud
and Wi-Fi follow-ups remain on delivery, and default promotion `65f55242`
remains held. Runtime result is pending in
[run 40150677765](https://github.com/aarohkandy/v0.0.1-auros/actions/runs/40150677765).
Do not dispatch a duplicate or cite the push-only gate as runtime evidence.

## Candidate Windows failure diagnosis and focused rerun

Both base lanes in `40148012698` reached AurOS and a populated store, but the
whole-screen OCR omitted the search query/result title. The old locator
required that query anchor. Lead inspected fastpath `0050-failure.png`; actual
VLC row is visible. Journals prove DHCP/DNS/catalogue ready before Software
opened. Worker reproduced on ordinary and fastpath screenshots with Tesseract.
`ebc553b7` crops the observed store body and requires an exact unique
title/summary pair; actual-image and adverse fixtures plus independent review
passed. This does not yet prove install or app launch.

Fastpath BitLocker failed before installation: `function H` was shadowed by
PowerShell alias `h=Get-History`, producing an Id conversion error in <1s, not
a timeout or evaluated Windows-state mismatch. The same alias occurred in the
final recovery snapshot. `38b741be` renames both helpers and all four calls;
local source regressions pass, real Windows execution remains pending.

Integrated delivery `0034a3ab`/`fdb2ee51`, main diagnostic `3df6181e`. Full
E2E selftest passed after integration. One focused rerun:
[40152103698](https://github.com/aarohkandy/v0.0.1-auros/actions/runs/40152103698),
fastpath base + BitLocker with put-back enabled. Exact EXE SHA-256:
`b5440e94d2a576e38003a2ff69bb440bc60a496cbe72e405c6ba1fd948a54e26`.
No ordinary legacy rerun or full rebuild was started. Default promotion
`65f55242` stays held; Wi-Fi/cloud follow-ups stay on delivery.

## Second-editor proof repair rerun

The generic 3% pixel-change predicate could miss nearly identical editor
windows, and older apparent passes could be satisfied by unrelated
notifications. New test content uses a fixed 24-row block staircase, with
marker-specific visibility, exact second-process/argv/input environment,
authenticated AurWL connection and mapped ordinary toplevel proof. Only an
observed changed marker pixel authorizes the click; exact saved sentinel,
unchanged first editor and full A-to-B-to-A lifecycle remain mandatory.
Baseline/after frames and reduced failure facts are retained.

Lead review additionally caught an EOF loop in the new frame parser and a
font-rendering assumption about gaps between full-block text rows. Follow-up
`8377624b` bounds/validates frames and recognizes touching-row width changes.
Verification: 55 Python tests, 95 harness checks, gate/workflow checks, and
independent re-review. These are harness results, not runtime proof.

One retained fresh-image rerun:
[40152195863](https://github.com/aarohkandy/v0.0.1-auros/actions/runs/40152195863),
harness `d33b9994`, image from `40148012574`. No new OS image was built.

## Exact-image follow-up, 25 October 02:16 UTC

Focused Windows run `40152103698` base lane now passed store result recognition, installation and Open dispatch. Screenshot `0052-auto-auros-open-vlc.png` shows VLC's actual foreground Privacy and Network Access Policy window, with a separate VLC media player task. The lane FAILED because whole-screen OCR missed Continue and the harness did not permit its focused-default action from this modal/task identity. This is stronger launch evidence but not a complete app-use pass: a narrow, independently reviewed harness repair must still require real VLC controls after dismissal. BitLocker remains in progress; default-fastpath promotion remains held. Exact-candidate hardening run `40151838977` is also still in progress.

VLC privacy-modal harness correction `fb0494ef` is integrated as delivery `c87dce98`, main `750fa4d5`. Exact unique dialog/body/full-task attribution and absence of Continue OCR permit only one focused-default action; final real VLC controls are still mandatory. Actual screenshot fixture plus missing/foreign/duplicate/preview negatives passed full local and Linux/Tesseract selftests and independent re-review. Base-only exact-installer run `40153730622` is in progress. The existing BitLocker lane `40152103698` remains separate and was not duplicated; it is still provisioning clean Windows before baseline/install proof.

## Exact hardening and two-boot diagnostic, 25 October 02:21 UTC

Hardening `40151838977` is green: 72 hardening checks (control-tied checks fail without controls), 14 clock checks, and exact-build release-gate verification. This is evidence for candidate `3629b114`, not its later follow-up changes or physical machines.

Production multilingual diagnostic `40153523144` FAILED only the raw profile-hash equality check (1 of 69). Both complete guest rounds passed composition, authenticated Mousepad focus, password server result/no composition events, zero Firefox-popup activity in all observed phases, exact Fcitx process/service identity, cleanup and real reboot/autostart. Distinct boot IDs were verified. Lead inspected round2 masked screenshot: three bullets, no candidate popup; exact input is established by server evidence, not screenshot count. Firefox connection identity and popup attribution remain unproven. This successful popup observation does not explain the prior intermittent failure.

User-profile hashes differ across boot. Runtime serialization is a hypothesis; exact changed bytes were not retained, so benign change is NOT established. The next diagnostic retains bounded profile-specific structure while preserving the failing raw-hash gate. Do not replace it with a looser semantic gate before identifying the actual difference. Evidence: `/tmp/auros-fcitx-production-security-40153523144`, artifact `12584070668`.

## Remaining exact-runtime blockers, 25 October 02:34 UTC

BitLocker run `40152103698` FAILED before restart: the PowerShell baseline repair is proven by complete `putback-before.json` and exact protector/partition/file/firmware evidence, but the harness could not obtain the visible Restart now button through UI Automation. It refused before suspension/restart. Exact foreground HWND/process and elevated helper were correct; the product exposes an MSAA accessibility tree, while the harness queried an empty UIA tree. A scoped cross-process MSAA lookup/probe is being prepared, preserving exact button identity, enabled state and geometry. No Windows-return or Put Windows back pass; default promotion remains held. Evidence `/tmp/auros-40152103698-bitlocker.63qxlo/extracted`.

Multilingual diagnostic `40154456991` FAILED with useful exact evidence. Runtime profiles were unchanged within each round; immutable template unchanged at all four boundaries. Across reboot the runtime profile added seven comment records (77 bytes total) and a one-byte blank line. Every original non-comment record remained byte-identical and ordered; no setting changed. Cause is not established. Round2 additionally recorded exactly one unattributed, unmapped/unconfigured/ungrabbed popup creation after submit, with zero popup events during focus/input/screenshot phases. Exact password server input and zero composition events passed. These findings support reviewing a comments-only profile comparison and separating sensitive-focus observations from submission/navigation observations; no gate correction is accepted without adversarial tests/review. Artifact `12583823011`, local `/tmp/auros-fcitx-profile-drift-40154456991-v1`.

## Exact fastpath base journey PASS, 25 October 02:36 UTC

Run `40153730622` is SUCCESS with harness `750fa4d5`, product candidate `3629b114`, exact fastpath EXE SHA-256 `b5440e94d2a576e38003a2ff69bb440bc60a496cbe72e405c6ba1fd948a54e26`. Website → Windows installer → restart → AurOS onboarding → store → installed VLC → real foreground VLC player passed in 9m29s. Lead inspected `0052-program-open.png`: actual VLC title, menus, cone canvas, and player controls with a separate task; not the store preview. Scope start journal and installed-disk evidence agree. This proves the store locator and first-launch handling on those exact bytes.

Sealed artifact `/tmp/auros-40153730622-base.Adop8g/unpacked/e2e-evidence-base-fastpath.zip`, SHA-256 `0b82935b52c17b0ce5a7c72050dcf5f59f1abe69501d05c6d556bbccf7a0d1b3`. Unpacked `/tmp/auros-40153730622-base.Adop8g/evidence`. This is Windows 11 VM proof for the fastpath base lane, not encrypted-drive return/putback, a personalized signed download, a physical PC, the later Wi-Fi follow-up, or the still-held default promotion.

## Accessibility and input evidence follow-up

The native accessibility probe has not passed yet. Runs `40155209229`, `40155547697`, `40155842399`, `40156128431`, and `40156524305` exposed, respectively, startup timing, PowerShell dynamic COM dispatch, passing RCWs through the PowerShell binder, then mutable/short-lived selftest snapshots. The current helper keeps COM entirely in C#, returns plain records, preserves exact HWND/PID/class/role/action identity, and keeps production reads explicitly fail-closed. Lead added the helper to Windows gate inputs and removed use of the automatic PowerShell `$Error` variable. Current main helper is `5cfaf8d5` (included in later main `0be3a3bc`); delivery equivalent through `8d2c630c`. No new BitLocker run was spent on these failed native probes.

A truly stable, read-only `--accessibility-test` fixture is being prepared in the isolated accessibility branch. It must suppress manual/COM mutating actions as well as selftest auto-navigation. Source changes on main would trigger an expensive full image build, so this fixture must first be pushed only to its isolated branch and run with one explicit Windows workflow dispatch. Hold it for the next batched candidate; do not weaken the build gate or prematurely promote fastpath.

Multilingual `40156051878` failed round1 on one unmapped popup creation after the form event despite prevented navigation. Exact input/focus/composition and closed-trace cleanup passed; reboot was not reached. Lead found and fixed false cgroup emptiness from stat size and silently ignored /proc inspection errors before this run. A standalone password-input fixture (`83284c3e`, delivery `5f04e7d0`, main `0be3a3bc`) avoids native form submission, verifies token-bound post-response input/focus, preserves strict zero-popup creation/mapping/configuration/grab through trace closure, and separates shutdown events. Run `40156605186` is pending on the same retained image `40148012574`; no image rebuild. Native form submission/navigation/password-manager behavior and Firefox connection identity remain unproven.

## Production multilingual diagnostic PASS and encrypted-drive rerun

`40156605186` PASSED 71/71 across two distinct boots of exact production image `3629b114` (retained `40148012574`), harness `0be3a3bc`. Composition/candidates, authenticated Mousepad focus revocation/remapping, standalone password exact value/no composition/continuous focus/zero popup events through shutdown, exact service identity/autostart, comment-only cross-boot profile differences and cleanup all passed. Lead visually inspected the Firefox candidate list. Artifact `12585600815`, local `/tmp/auros-fcitx-40156605186.H0VP3e`. Firefox connection identity and native form/navigation/password-manager scope remain unproven; masked screenshots do not independently prove the exact entered value. See the current section of MULTILINGUAL-INPUT-2026-10-24.md.

Stable native accessibility branch run `40156916406` executed all exact positive and wrong/stale negative checks without exceptions, then failed solely because the last expected-negative helper exit code propagated out of the PowerShell step. A reporting-only fix is being prepared on that isolated branch. Based on the actual completed native assertions, ONE exact-candidate BitLocker/putback run `40157169797` was dispatched on main `0be3a3bc`; no installer/image rebuild or default promotion. The fixture source remains held outside main.

Native accessibility proof is now PASS: branch run `40157223798`, Windows-source job `121061302872`, exact cross-process MSAA step SUCCESS with explicit `PASS: the exact cross-process MSAA controls were readable; wrong and stale HWNDs were refused`. Stable read-only fixture and checked-negative reporting commits `23eb5101` / `497a2175` are integrated on delivery as `87d70ba3` / `ebefe7be`, not on main; they join the next batched candidate. Normal product behavior and provider are unchanged outside the explicit noninteractive diagnostic mode. Overall branch workflow completion is separate from this verified native step. Exact BitLocker/putback `40157169797` remains running; default promotion stays held.

## Native proof and cache checkpoint, 25 October 03:23 UTC

The entire Windows workflow `40157223798` finished SUCCESS. Stable fixture/reporting changes are on delivery only. Cache reuse worker commit `08c42158` is integrated as `66ea5776`: base and BitLocker select identical clean M1 hardware/cache inputs; runtime fully encrypted/protection-on checks remain mandatory in an isolated copy. Independent review and worker E2E selftests passed, as did integrated workflow lint. No new image build, default promotion, or duplicate recovery run was dispatched.

## Continued product integration, 25 October 03:33 UTC

Prepared clean main-based branch `codex/product-batch-prepromotion` in `/tmp/auros-product-batch-prepromotion`, currently `64e6edba`. It combines the held Wi-Fi/Ferry, cloud, night-light/hardening, stable accessibility fixture and cache changes without default promotion. Existing local-compatible integration checks passed. It is not pushed and does not include `65f55242`; current recovery run `40157169797` remains active. Latest inspected M1 screenshot showed native Windows setup at 77%, so it was progressing, not stalled.

Site activation consistency worker `9dc03ae2` / `8b8caf89` is integrated on delivery `2bb24a83` / `d7f48ae4` and candidate `4f66b2d1` / `52a3edd8`. Both Pages and release publishers now generate staged config and verify exact live CORS readiness with bounded read-only OPTIONS when enabled; disabled probes refuse before storage. Root caught the initially omitted release publisher and inconsistent origin canonicalization, both fixed and re-reviewed. Checked-in switches remain off. Worker: 107 cloud, 8 Python and 556 browser checks passed. Combined candidate cloud run exposed a timing-sensitive existing same-recipe concurrency test (actual 200/201 versus expected 201/503); diagnosis is active, not waived. Integrated browser rerun passed 556/556; integrated Python config and all 29 workflow checks passed.

Dormant personal-profile update support worker `a883d97e` / `47318acd` is integrated on delivery `997dab11` / `d903613a` and candidate `8f90acdc` / `64e6edba`. Explicit desktop/revive/multilingual descriptors bind package/repository identity, with real apt cross-profile refusal; desktop's existing tag/label/suite/pin remain compatible. Non-desktop image activation and publication remain disabled. Root found a pre-existing verifier gap: signed Release missing mandatory index hashes was accepted. Six failures were reproduced; mandatory exact safe hashes for both indexes are now required before index reads. Final real Ubuntu update/key-rotation/rollback suite passed 85/85, including 14 signed hash-record negatives, with independent review. No live channel, signing key, or publication was changed.

Build-request race closed: worker `9bca9d84`, delivery `a921c8d3`, candidate `723988e2`. The product correctly returns 200 for a late reuse; the old test's Promise.all did not guarantee overlapping discovery. Deterministic barriers now prove true overlap refuses the competing discovery, atomic quota admission and one dispatch; a separate late-reuse case verifies both receipts. Forty focused repetitions and independent review passed. Combined candidate cloud suite now passes 108/108; no production behavior was changed.

## Revive real-use probe checkpoint

Worker `e2a5438b` / `2e988cb5` / `ee460568` integrated on delivery as `976bea0d` / `5a075e32` / `c9295846`, candidate as `cce969fa` / `79b3147e` / `8d056667`. `[revive-functional]` selects a new opt-in 2048 MiB runtime probe for Firefox physical-key input and AbiWord saved-document use. Exact package executable/process/unit/display identity, screenshot/OCR binding, cleanup, OOM counters and post-use system health are required. Root found an unsupported Firefox launcher assumption and missing post-use health checks; follow-up invokes only the exact package-owned binary, records manager/app environments, and requires earlyoom active with no new failed units. Forced systemd cleanup is explicitly not normal Quit proof. Gnumeric remains untested.

The exact tested compressed Revive image and source/profile/run/hash/size manifest are retained for two days only in functional runs, including runtime failures after successful build. No ordinary artifact/release or activation changes. Integrated validation passed 23 focused tests, 105 existing harness checks, profile gate selftest, all 29 workflow files and diff checks. No Revive image was built or run yet; runtime remains unproven. Main-based candidate head is `8d056667` and remains unpushed, without held default promotion. Exact BitLocker run `40157169797` has passed M1 and is in the installer journey; its latest recorded encrypted-drive preparation is still not full recovery proof.

## Installer estimate clarity

Live encrypted-drive setup exposed an inflated running estimate: it added a fixed 20-minute SSD/75-minute HDD post-restart allowance to measured remaining Windows work. Worker `0320ffa1`, delivery `13ae8236`, candidate `f0c35ff0` now explicitly estimates only time until Restart now, with matching stable accessibility wording and stale-estimate withdrawal on sustained stalls. Ready retains the deliberately broad whole-journey allowance, labeled total; no physical-PC timing claim was introduced. MinGW build, Wine native navtest/selftest including real calculation/rounding/stall/path/drive/wording tests, wordlint and independent review passed. Default promotion remains held.

## Encrypted-drive run stopped as inconclusive, 25 October 04:09 UTC

`40157169797` passed fresh Windows provisioning, FullyEncrypted/On/100% with TPM and RecoveryPassword protectors, exact installer hash verification, and named MSAA Next/Install controls. Install was clicked 03:43:03Z. Progress then remained at about 57% (engine60), with the last named-control log at03:45:04Z, unchanged through04:07:50Z. Root requested cancellation after preserving live evidence, rather than letting the default300-minute E2E deadline consume hours. This is an inconclusive run/harness stall, not a proven product failure. Restart warning, suspension, AurOS and Windows return/putback were NOT reached; default promotion remains held.

Evidence `/tmp/auros-40157169797-live-040750/`; PNG SHA256 `c932bbb03f9b84c4be721965d3ff5075a4c517629a1da84beee8f9ffaa9c9079`, TXT `a2a903beb2314804abea1a8dcf8beb6c9abd625072e1696a9ba1ca57168571a9`, release `e2e-40157169797`, asset IDs674050914/674050915. Final cancellation artifacts are still being collected.

Code audit found no worker/UI I/O lock cycle in normal installation. Shared early-download progress makes engine60 ambiguous between shrink, downloader wait, planning or raw-write start. Separately, the serial helper's UIA/MSAA calls have no in-guest deadline: a blocked provider can outlive the host timeout and prevent all later commands, including diagnostics. A bounded child-process accessibility reader and independent host QMP disk counters are being prepared and will receive Windows-native proof before a single replacement exact-installer run. No default promotion or new product image was dispatched.

Cancelled run final archive `/tmp/auros-40157169797-final.86OnPP/raw/e2e-evidence-bitlocker-fastpath.zip`, SHA256 `46494186fa27db0989140fa8282c6354420df9bf214799a9234f993cbd4f5543`. Shots0025–0029 at03:48/03:53/03:58/04:03/04:08 are byte-identical. Final always-evidence upload succeeded; total run59m23s. Bounded diagnostics worker1510c457/2ae2422a are local delivery7bd52773/7a26488e; Windows-native proof still pending alongside accessibility isolation.

## Bounded accessibility native verification, 25 October 04:20 UTC

Worker `11bd6a6b` integrated as delivery `2ad92746`, candidate `6d677f8f`. Each windows/elements read executes in a fresh bounded PowerShell process; closed bounded requests, single-record JSON, output truncation refusal and verified termination prevent a blocked provider from permanently occupying the serial helper. Native coverage deliberately blocks a child, requires its readiness marker, verifies timeout/termination and immediately reads the exact live wizard PID/HWND through a fresh child. Existing positive/wrong/stale identity checks remain. E2E selftests, YAML and independent review passed; Windows-native proof remains pending.

Both delivery and `codex/product-batch-prepromotion` are pushed. Exactly one Windows workflow `40161196147` was dispatched on candidate `6d677f8f`, pinned published release `v0.1.0-3629b11`. No main push, image rebuild, public activation or default promotion. Only after native proof passes, dispatch one replacement exact old fastpath BitLocker/putback journey with the cache/diagnostic fixes. Integration review and dormant personal-profile packaging design proceed independently.

## Native failure evidence and publication boundary review

Windows `40161196147` passed build, published installer, known-bad rejection, download, bounded diagnostic success/partial-timeout/truncation, initial exact MSAA read and forced-child termination. Recovery child exited before JSON; stderr had been discarded, so cause remains unknown. Worker `94828354` (delivery `45c97eec`, candidate `c998bf92`) preserves bounded native-test-only child error/phase evidence. Exactly one diagnostic rerun `40161496888` is active. No recovery rerun or main push yet. Persisting helper via changed boot.ps1 correctly invalidates the old golden cache; first new recovery run must provision Windows once, then the shared base cache can be reused.

Read-only integration review found customization incorrectly treats the release tag target as exact built-source authority, despite documented release tagging of a newer main tip. A two-identity correction is being implemented in isolated worktree `/Users/aaroh/.codex/worktrees/customize-base-identity/auros`: current main workflow/signing authority remains distinct from exact reused base build SHA/run/artifacts; release tag is only the asset namespace. Exact signed/resource/manifest boundaries must remain strict, and public delivery remains disabled.

The same review found both site publishers check before writing final served config and can overwrite newer activation/disable settings. A separate worker is consolidating deployment into one reusable publisher, staging/testing current authoritative main inside a shared queue and rechecking before deployment. Versioned nonsecret publishing settings replace repository-variable authority for these paths because the workflow token cannot be assumed to have Variables:read. No deployment or public activation is authorized by these implementation changes. These fixes remain under implementation/review, not proven complete.

## Native isolation PASS, replacement recovery and current parallel fixes

`40161496888` exposed contradictory status `child-exit` with final exit code0 after all child phases completed. Worker `c199b209` (candidate `6a732d79`, delivery `689aaea0`) pins/disposes the process handle, reads a refreshed exit code once, fails closed on missing exit status, and tests five fast zero exits plus deliberate exit7. Full Windows workflow `40161803540` PASSED, including source job121074920541: bounded diagnostic PASS04:27:10Z, isolation timeout/recovery PASS04:27:22Z, exact/wrong/stale HWND PASS04:27:25Z. This is hosted Windows evidence, not physical-PC proof.

Exactly one replacement E2E `40161971669` runs on candidate `6a732d79` with old exact fastpath URL/hash, variants=bitlocker and bitlocker_putback=true. Main/default remain unchanged. Agent fix_wish_ui monitors; fresh M1 is expected due correct boot-script cache invalidation.

Root replaced stale Ready screenshot using that successful native run's `wizard-shots-windows/hidpi-4a-ready.png`; PNG SHA a8ec0857bb8bb736590e7b22223533c93aaa9458f39f3b0587e8ac2919111284. Candidate `ac7eea21`, delivery `90000311`, both pushed. Actual PNG/JPEG inspected; full browser556/556 PASS. Other screenshot sources remain Wine and public physical-PC claims remain unchanged.

Publisher worker local `705622a4` in `/tmp/auros-publish-ordering` is NOT integrated. Single authoritative reusable publisher/settings migration implemented and focused tests passed, but final review requires deployed-Worker identity/correlated smoke proof and resolution of exact-main cloud evidence versus path-filtered runs. Browser reruns incorporate the root screenshot fix. Customization worker `/Users/aaroh/.codex/worktrees/customize-base-identity/auros` continues separate authority/base provenance correction, bounded pieces-header discovery, immutable full-authority-SHA asset names, browser filename validation and race/collision tests. Neither stream may be called complete until final review and combined checks pass.

## Customization identity integration verified

Worker series `f14b2d55` through `4447c96c` is integrated on candidate through `2a2490cc`, delivery through `ad886a6c`; both pushed. Current audited workflow/signing SHA is distinct from exact reused base build SHA/run/tag. Bounded pieces.txt is discovery only; Actions run and run-bound artifacts establish base identity. Release tag pointer is not source authority. Strict v2 metadata and full40-character authority-qualified custom asset filenames prevent routine-main filename collisions; browser matches authority shape and submitted recipe. Cache keys bind authority/baseSHA/run/tag/recipe; exact tag is required on polling/cached evidence.

Selection passes expected authority_sha and workflow checks it against actual GITHUB_SHA before packaging, catching dispatch races. Later routine main movement does not revoke an already trusted build or signed cached output. Lookup outage may only reuse a known canonical delivery with strict revalidation, never authorize a fresh dispatch. Root caught a new cross-step BASE_BUILD_TAG scope failure; regression failed before explicit step env binding and passed afterward.

Combined candidate validation: cloud110/110, customize56/56, browser558/558 PASS, diff clean; workflow29 and independent review passed on the slice. Logs /tmp/auros-identity-{cloud,python,site}-integrated.log. No actual signed personalized journey or public activation yet; owner credentials remain required.

Publisher `705622a4` remains held with active followups in /tmp/auros-publish-ordering. Final design binds health to deployed source/nonce plus separate masked smoke correlation, and reuses immutable successful cloud proof only for independently recomputed identical cloud dependency digests. Complete rolling31-day run inventory handles GitHub's documented30-day rerun window, bounded1000; failed latest matching attempts cannot fall back to stale success. This design is under implementation/testing, not yet integrated or claimed proven.

## Publisher integration verified; recovery remains active

Publisher worker705622a4 +6bce1edf integrated as candidate219f6691/a1a8caf5, delivery0f996bad/ed0ec9ed; both pushed. One reusable publisher stages/tests current main, hashes exact site bytes, rechecks source/settings/latest installer and serializes both callers. Nonsecret versioned publish-settings.json is authority for site/cloud/monitor URLs; no Variables:read credential assumption. Present CNAME must match; documented edge proxy may omit Pages CNAME. Existing installer-domain variable must align before enabled publishing.

Worker health binds deployed sourceSHA/publicnonce; a separate masked per-attempt smoke token correlates service logs. Endpoint preflight checks exact configured default or custom-origin identity. Immutable exact-attempt proof binds GitHub metadata, successful real deploy/preflight/smoke steps, independently recomputed Git-object cloud dependency digest, and live source/nonce. Complete rolling31-day bounded inventory and final full inventory/attempt revalidation prevent old-run rerun or pagination races; irrelevant source digests are filtered before detailed attempt reads. A failed latest matching attempt cannot reuse older success. Unrelated site/docs/main changes can reuse an identical verified cloud dependency set, so no needless Worker redeployment. Cloud recipe-schema trigger omission is fixed. Off switches/budget remain unchanged; no live deployment or paid calls.

Combined candidate: cloud125/125, publishing Python38/38, browser560/560, workflow30 PASS. Logs /tmp/auros-publisher-{cloud,python,site,workflow}-integrated.log. Worker also verified both staged enabled/disabled browser modes558/558 before identity integration.

Helddefault65f55242 still NOT integrated. Apply-check now conflicts only structurally in tools/customize/test_base_build.py: retain v2 first WorkflowContractTests method, insert held test_personalized_installer_inherits_fastpath_default before test_extraction_creates_output_directory_before_all_five_resources. Its assertions stay unchanged: build/aurbridge defaults AUROS_FASTPATH:-1 and customized rebuild inherits it without override. Release/publication changes otherwise compose. Run exact workflow contract/customize/workflow checks after integration, only following recoveryPASS. Recovery40161971669 still healthy freshM1 at last observation; no restarted proof or main push.

## Continued recovery and integration audit

Recovery40161971669 completed fresh M1 and cache persistence and is now in M2/M3. Evidence at04:55:51Z shows the persisted helper running, SecureBoot on and BitLocker fixture encryption advancing86%→90% with TPM and RecoveryPassword protectors. Encryption, restart and recovery are not yet claimed. Focused independent audit of a1a8caf5 confirmed the earlier generic-default/personalized receipt confusion and exact-base-tag verification omissions are both fixed with regressions. Main and held default remain unchanged.

## Profile runtime evidence records and exact installer progress

Worker b0454074 integrated as candidate5725a6e9 and deliverya02e979b; both pushed. Successful opt-in Revive/Multilingual functional runs now emit small strict runtime-image evidence records tied to repo/source/profile/run/attempt and existing raw/compressed byte identities. Machine contract explicitly forbids delivery authorization and requires live backing validation; metadata30days does not extend Revive backing2days or prunable Multilingual pieces. Generic evidence excludes these records; dedicated metadata uploads only after backing upload succeeds. Root/reviewer caught and fixed premature artifact inclusion, ambiguous attempts, bool/float identity confusion and unrelated source bytes. Worker42 focused tests plus profile selftests passed; integrated19 profile-image tests and30 workflows passed; independent re-review clean. No image rebuild or public profile activation.

Recovery40161971669: FullyEncrypted/On/100% at04:57:48Z with TPM+RecoveryPassword. Exact browser UIA Keep/Open/Keep anyway worked through persisted bounded helper, exact installer SHA verified, SmartScreen/UAC and all wizard MSAA pages passed. Install clicked05:00:46Z;05:01:04Z controls showed Setting up AurOS. Recovery/return/putback still pending. Planned final subject remains [release] [revive-functional] [e2e:none] without [latest]; independent workflow audit confirms full exact ordinary base+BitLocker/putback still run through release workflow and disabled public configuration does not block prerelease.

## Recovery boot-menu capture failure and repaired harness

40161971669 FAILED after proving FullyEncrypted/On baseline, exact installer, safe Not yet confirmation, install/restart/AurOS/store and real VLC controls (11m32s machine journey). It did not prove Windows return or putback. Archive /tmp/auros-run-40161971669.XujCLr/release/e2e-evidence-bitlocker-fastpath.zip SHA2566b89c42eb820689e98154320020e797e97944a56da1c63d9eb99c69fd25afbbc. Root inspected actual GRUB0066 and desktop failure0070: modifier-only Shift did not stop the boot countdown, and the test later mistook the desktop launcher for an opportunity to find GRUB, finally failing closed.

Menu workers9f2957cc/2c36f05e/2d63931f integrated candidatee1dc3b4d/371178a6/ffdb9016. Actual Up pulses replace Shift, strict shipped-menu detection remains, and only exact desktop evidence permits one confirmed graceful ACPI shutdown/retry with fresh event timestamp. Root caught Machine.stop's forced-quit fallback and required explicit shutdown plus confirmed exit instead. Recovery/error/rescue or unknown screens cannot retry. Root also reproduced a second inevitable false failure: selected GRUB cyan has whole-row mean57.7, below old110 threshold. Exact observed-theme row coverage now selects only the named highlighted row; retained real screenshot plus positive/negative tests cover it.

Read-only restore audit found previous partition fingerprint proved only geometry/types. Worker97953372 integrated candidate3188f47a: shared v1 serializer binds Windows disk/partition GUIDs, GPT type, geometry and stable flags. Real injected PowerShell now runs positive/mutation/malformed mocked-object checks before accepting baseline; unlike the rejected synthetic Python hash test, these exercise the actual serializer. Local E2E selftests pass; first native fixture proof is pending the next single cached recovery run. Held default65f55242 still not integrated; main unchanged0be3a3bc. No image build or public activation.

## Real OCR followup before replacement installation

Run40165289386 (harnessc0f10a36) was CANCELLED during runner package setup, before Windows/cache/installer work or native GPT fixture proof. Root used existing homebase Tesseract with the retained0066 screenshot and found full-screen OCR omitted the selected cyan row's black text, so the first color-only fix would still fail selection. This was caught without another full installation. Tiny isolated files under /tmp/auros-grub-ocr-40161971669; no service/package changes or heavy job on homebase.

Workera6bcc296 integrated candidate72faed34: detect exactly one observed cyan band, derive horizontal bounds, whiten only its cyan background, trim to glyph bounds, OCR that selected row with psm7, require exact requested label before Enter. Real Linux Tesseract reads actual selectedAurOS as non-Windows and a glyph-preserving derived selectedWindows as exactWindows. No-cyan, two-band and blank-band frames fail closed. Mac selftest explicitly skips only realOCR whenTesseract absent; workflow now installs/asserts Tesseract before both early harness selftests so CI must exercise actualOCR before VM work. Main/default remain held; nextsingleexactoldinstaller recovery run follows combinedchecks.


## Native GPT diagnostic isolates nullable Windows fields

Recovery40165655232 FAILED before website/install because live storage fingerprint capture threw missing storage field IsReadOnly. Real Linux OCR regression passed and encrypted Windows cache was reused; no return/putback claim. Evidence archive /tmp/auros-run-40165655232.wHrS3n/e2e-evidence-bitlocker-fastpath.zip SHA2563e73e06bebabd5a2fefa9528e9eace2060c9d26d4f7f0673780f67c6a18fc178. Exact mock serializer separately passed under existing network-disabled, bounded homebase PowerShell Docker.

Native read-only diagnostic40166608308, sourceeabd6cb6, reproduced the cause without installing/building: exact mock PASS under Windows PowerShell5.1; disk fields all typed; partition0/1 IsReadOnly and NoDefaultDriveLetter present-null; partition2 all typed. Live strict capture fails on null. Fix under implementation records explicit unreported state for only these two partition CIM fields, never silently false; missing fields remain errors and state transitions must change the fingerprint. This proof covers GUIDs/geometry/exposed CIM states, not every raw GPT attribute bit. Held default/main remain unchanged.

Revive retained-image diagnostic0c346180 integrated as candidate86cb98c9 and delivery23a7e0c5, both pushed. Exact latest trusted main attempt, successful build/size/retention steps, live artifact creation window/digest/size, strict archive membership and bounded sparse restore are validated. Diagnostic boot cannot authorize delivery or emit runtime certification. Independent review clean; integrated26 profile-image tests and30 workflows passed. Real retained-image download/boot remains pending an eligible Revive image.


## Nullable CIM correction passes native Windows; recovery resumed

Worker9d1082b6 integrated candidate0af80a3c/delivery5aac2d1d. Version2 fingerprint uses dedicated two-name allowlist for partition IsReadOnly/NoDefaultDriveLetter, preserving present-null as unreported; missing/nonboolean and every other null still refuse. Real serializer mocks distinguish all three states and retain GUID/type/geometry mutations. E2E selftests,30 workflows, exact PowerShell Docker fixture and independent review passed.

Focused native Windows40166824946 SUCCESS at exact0af80a3c5d671445a79890bcf8155d577c5c9b95: one exact mock PASS and live-capture-pass with actual null partition fields. Artifact /tmp/auros-storage-diag-40166824946.7Gg5Zm/storage-diagnostic.txt SHA2569b60d41209607d9968cf7f3a154ed929049c047f71f9e753b9bea583eff25e59. Exactly one full cached BitLocker/putback replacement40166907480 dispatched against old3629fastpath, unchanged exactSHA b5440e94d2a576e38003a2ff69bb440bc60a496cbe72e405c6ba1fd948a54e26. Return/restore not yet claimed. Main and default still held.

Independent ledger check found remaining launch highs: held encryption/lock, signing-key custody/main protection, and immutable release publication. These do not prevent generating an experimental unpromoted prerelease; such assets in this public repository remain publicly downloadable, so never call them private. Public site switches remain off. Next bounded independent safety slice is dormant-only Ferry import refusal until armed S4 reports exact state=pin; no S4 activation or held41commit merge.
