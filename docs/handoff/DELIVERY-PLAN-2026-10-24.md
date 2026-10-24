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

- [ ] Unit owner: reproduce RC7 font/hardware/layout/Ferry failures; fix product or test root causes without weakening gates.
- [ ] Compositor owner: reproduce uncaught mutants, repair relevant code/tests, prove intended breakages are caught.
- [ ] Wishes UI owner: reproduce failed UI cases; fix without enabling the disabled production feature.
- [ ] Installer safety owner: recover S-04, refuse before table commit when boot-entry creation fails, verify byte preservation and fault paths.
- [ ] Bootstrapper owner: recover B's protected Windows loader design and validate real Windows hostile-DLL/junction cases.
- [ ] Lead: integrate CIW regression coverage and verify the tests exercised match the shipped path.
- [ ] Independent handover review: kernel/modules, network/time, mounts, devices/display, PID1/services and installed-vs-bare journal differences.

### Stage 2 Build a reusable customization path

- [ ] Contract owner: strict schema, stable profile catalogue, deterministic compilation, policy preservation, hostile-input tests.
- [ ] Website owner: accessible request/review flow, honest artifact status, no fake queue or AI result; keep public download gating intact.
- [ ] Delivery adapter: choose and implement authenticated recipe transport to the installer; pin base artifact and configuration digest; reject mismatches, stale/unsupported recipes and downgrade attempts.
- [ ] Installer/first-boot adapter: apply only allowlisted preferences, retain policy constraints and rollback behavior, confirm requested result in the installed system.
- [ ] Agent extension contract: document permitted extension points and tests so new sensors/actions require small changes instead of another OS fork.
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
- Extension map: `docs/AGENT-EXTENSIONS.md` documents the existing schema/engine/device path, including compass-to-brightness, and required extension evidence. Host Wishes checks pass (874 parser, 140 engine per compiler, 116 hook, 142 schema shapes, sound checks); Linux daemon, GTK and text-input integration were skipped on this host.

## Credit efficiency update

The owner explicitly prioritized credit efficiency during implementation. Finish assigned critical-path work without expanding the agent tree, reuse existing reviews and evidence, avoid redundant testing, and batch pushes to reduce repeated image CI builds. No automatic continuation or new paid service has been configured.

## Deferred security integration notes

S4's 41 held commits are not activation-ready. Preserve the disabled final runtime/test tree rather than replaying old CI wholesale. Missing setup/recovery/PAM and encrypted handover work, a filesystem-shrink power-cut window, and physical-machine evidence remain blockers. Ferry must not copy personal data while an empty temporary encryption key remains usable. The held workflow also needs pinned actions, minimal permissions and exact-build evidence before reuse.

Protected bootstrap signing requires two stages: verify the signed inner wizard against same-run build bytes, rebuild only the stub from the exact build source with that signed payload, sign the outer, then verify both signatures and extracted payload identity. Current outer-only SignPath signing does not prove this. Owner SignPath policy configuration is still required; keep the issue open.
