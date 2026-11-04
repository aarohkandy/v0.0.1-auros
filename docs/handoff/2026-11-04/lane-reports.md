# Lane reports, verbatim from the workflow journal (2026-11-04)

Each entry is one agent's structured report: implementers (impl:*), adversarial reviewers (attack:*).
The workflow was stopped at the handover; this is everything it produced.

## impl:othersys-cinnamon

```json
{
  "branch": "agent/othersys-cinnamon",
  "commits": [
    "cbadd44b othersys: boot and answer take turns, so a waiting \"yes\" is not undone"
  ],
  "root_cause": "Two separate things in run 41539330303. (1) FIXED, a real product race, not Cinnamon-specific: auros-othersys-boot.service (cmd_boot) and auros-othersys-answer.service (cmd_answer) both load state.json, act on the firmware and write state.json back, and systemd starts them together whenever an answer is already waiting (the VM's confirm phase pre-seeds inbox/answer). In the Cinnamon confirm boot, cmd_boot was the slow one (first time it ran remove_iso + sfdisk + resize2fs, because the firstboot phase had hung and never ran it). The serial log shows the answer service finishing first (\"answer: yes: BootOrder 0004,0002,0000,0001\"), then boot's hold() working from its stale in-memory state (no answer) saw Ubuntu ahead of Windows and put Windows first again (\"something put Ubuntu ahead of Windows; BootOrder 0002,0000,0001,0004 again\"), re-armed BootNext 0004, and saved a state.json with no \"answer\" key (the status dump at the end of the confirm serial log has none, boots:1). That explains \"confirm: BootOrder not Ubuntu-first (0002,0000,0001,0004)\" and \"afteryes: no session\" (BootOrder Windows-first, so the Windows stand-in booted; afteryes-003-windows.png). It was visible only on Cinnamon because the other variants' boot service had nothing slow to do in confirm (their ISO was removed in firstboot). (2) NOT a product bug I can find, and NOT fixed: the firstboot hang. The serial log and the final screenshot stop at \"Starting initrd-switch-root.service - Switch Root...\" for 1200 s with no output from the new root's systemd at all (no OSC 3008 line, no 'Welcome', none of our units ran: the ISO partition was untouched). Same disk, same initrd booted fine 8 minutes later in the confirm phase (session up at 66 s), and Cinnamon's firstboot passed in the two earlier othersys runs (41538432102, 41538963268: firstboot 55 s, all 36 checks yes) -- so it looks like a one-off guest/KVM hang before PID 1 of the installed system. BootNext 0004 was consumed (firmware did start Ubuntu). The hypotheses are refuted by evidence: LightDM autologin worked (state.autologin = ['lightdm'], \"grandma seat0\" session and OTHERSYS-TEST SESSION-UP at uptime 32 s in confirm); the boot unit and answer path were enabled and ran; the write path for yes worked (it wrote BootOrder Ubuntu-first and recorded AurOSOtherSysAnswer). The BootNext-None and ISO/root-not-grown firstboot failures are consequences of the hang. The moknegative 'screen check (must fail)' failure is a downstream effect: that VM boot hung/was cut too early to show the MokManager screen (moknegative serial log is 595 bytes, same stuck-at-start pattern as afteryes/windowsfirst), not a Cinnamon product issue.",
  "changed_files": [
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-5/src/othersys/ubuntu/othersys.py",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-5/tools/othersys/selftest.py"
  ],
  "tests_run": [
    "python3 tools/othersys/selftest.py on the Mac -> cannot run (no sgdisk/sfdisk/mkfs.vfat on macOS); ran on homebase instead",
    "homebase: docker run --rm --memory 8g ubuntu:24.04 with the CI apt packages, python3 -I tools/othersys/selftest.py, WITH the fix -> '87 checked, 0 not as expected' (new cases 'boot and answer at once: yes puts Ubuntu first...' and '...state.json keeps the answer' both ok)",
    "same container, same tree with the @one_at_a_time decorators sed-removed (fix off) -> '87 checked, 2 not as expected': both new cases FAIL, with BootOrder ['0002','0000','0001','0004','0005'] and state {'system','ubuntu_entry','boots':1} (no answer) -- exactly the VM evidence",
    "homebase temp files and container removed afterwards (--rm; /tmp/cinn-in, tarball, script deleted); free -h showed 22 GiB free before"
  ],
  "expected_ci_flips": [
    "othersys ubuntucinnamon confirm: 'BootOrder starts Ubuntu first, Windows kept' should flip to yes, provided the guest reaches confirm normally (the fix makes boot and the answer service serialise, so yes can no longer be undone)",
    "othersys ubuntucinnamon afteryes: 'without BootNext, the firmware now starts Ubuntu' (SESSION-UP) should flip to yes, as a direct consequence of the confirm fix",
    "NOT expected to change by this commit: firstboot 'SESSION-UP', 'ISO partition gone', 'Ubuntu got the ISO space', 'BootNext is Ubuntu' -- these failed because of the one-off hang at switch-root; they should pass on a re-run only if the hang does not recur (they passed in runs 41538432102 and 41538963268 on the same code). The moknegative 'screen check sees MokManager (must fail)' likely follows once the guest boots normally in that phase, also unconfirmed",
    "ntfs, xubuntu, bitlocker, powercut should be unchanged; this was a latent race for them too and the lock only removes it"
  ],
  "unverified": [
    "The fix was not run in a VM (no KVM here); only selftest.py's unit-level reproduction, which drives cmd_boot with a slowed remove_iso and cmd_answer in a thread against the stand-in efivarfs.",
    "The firstboot hang at switch-root is unexplained: I could only see the serial log and one screenshot (evidence zip for cinnamon downloaded partially, unreadable; I did not retry). It may be a nested-KVM flake, or something about the first start of the installed system I cannot see; if it recurs on the lead's VM run, grab the full 1200 s serial and screenshots from that run, and consider checking whether it is cinnamon-specific (e.g. snapd/firefox first-boot or plymouth with 'quiet splash' on the VGA console) rather than assuming it is a flake.",
    "Whether flock on /var/lib/auros-othersys/lock behaves on the real installed system (root-owned dir, ext4, services run as root): standard, but only exercised on a container tmpdir. The answer service has no hardening (no ProtectSystem), so the lock file can be created; the boot unit has ConditionPathExists on state.json so the dir exists.",
    "I did not touch docs/OTHER-SYSTEMS.md or vmtest.py; the commit is the two files only. No push, no workflow triggered."
  ],
  "notes": "The lane's three hypotheses were tested against the serial logs: LightDM/autologin/session-up marker/zenity ask are fine on Cinnamon (confirm and the earlier runs reached the session via lightdm); the boot unit is wired correctly (it ran in confirm and did everything including ISO removal and root growth: 'Ubuntu now also has the ISO's 6652 MiB'); the 'yes' path did write BootOrder, but a concurrent boot service overwrote it. The fix is a small decorator (one_at_a_time, flock) on cmd_boot and cmd_answer in src/othersys/ubuntu/othersys.py; fcntl was already imported, functools added. Worktree: /Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-5, branch agent/othersys-cinnamon, commit cbadd44b on top of bdec9638. The lead's single VM run should be read with the firstboot hang in mind: if the Cinnamon firstboot fails again at 'Starting initrd-switch-root.service' with a silent console, that is a different (still open) problem.\""
}
```

## impl:retire-fastpath-lane

```json
{
  "branch": "agent/retire-fastpath-lane",
  "commits": [
    "680658ed ci: retire the fast-path twin installer build and its e2e lane"
  ],
  "root_cause": "The twin differs on every build because the PE linker stamps the link time into the header. build/cflags HARDEN_PE (and build/aurbridge-stub's STUB_FLAGS) has no -Wl,--no-insert-timestamp. I checked this on homebase with mingw-w64 gcc 13 / binutils 2.41 in ubuntu:24.04. Two links of the same trivial source with the HARDEN_PE linker flags, 3 s apart, differ at byte 137 (PE header TimeDateStamp) and byte 217. Adding -Wl,--no-insert-timestamp made them byte-identical. The inner installer (wizard) and the outer stub both link this way. So the twin was the same configuration and was never a different product. I did not apply the one-flag fix: it changes shipped bytes, needs the flag on the wizard, console tool and stub links, and I cannot build the Windows binaries on this Mac. Applying it to build/cflags HARDEN_PE and aurbridge-stub's STUB_FLAGS would make AurOS-Setup.exe reproducible for a given source, which is a separate lane. windres resource objects and the version string were not checked; I saw no __DATE__/__TIME__ in src/aurbridge.",
  "changed_files": [
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/.github/workflows/build.yml",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/.github/workflows/release.yml",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/.github/workflows/e2e.yml",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/tools/e2e/e2e.py",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/tools/e2e/publish.sh",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/tools/e2e/selftest.py",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/tools/e2e/README.md",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/tools/releasegate.py",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/tools/workflowlint.py",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/docs/AURBRIDGE.md",
    "/Users/aaroh/auros/.claude/worktrees/wf_051185c6-7ad-2/docs/handoff/SESSION-2026-11-03.md"
  ],
  "tests_run": [
    "python3 tools/e2e/selftest.py (new tree) -> 'all passed', 0 FAIL",
    "Same new tools/e2e/selftest.py run in a scratch copy of the tree with the old build.yml, release.yml, e2e.yml, e2e.py and publish.sh (AUROS_REPO set, because e2e.py needs it): 4 FAILs. The failing checks are 'release: the retired fast-path twin lane and asset are gone', 'pause warning: the retired AurOS-Setup-fastpath.exe asset may not drive it', 'put-back: workflow binds an exact capable name...', 'put-back: build.yml no longer rebuilds a twin installer'. Without the AUROS_REPO variable the same run crashes in import e2e instead.",
    "python3 tools/workflowlint.py -> 'workflowlint: 30 workflow files, ok'",
    "python3 tools/releasegate.py selftest -> ok",
    "venv pyyaml: yaml.safe_load of build.yml, release.yml, e2e.yml OK. Every needs: entry names an existing job. Every needs.<job> and needs.publish.outputs.* / needs.build.outputs.* reference resolves. Every steps.<id>.outputs reference has a step id. No fastpath_sha256, steps.fastpath or present reference is left (grep).",
    "bash -n on every bash run: block of build.yml and release.yml (extracted via yaml) -> 0 failures",
    "homebase: docker run ubuntu:24.04 with mingw-w64, linking the same trivial source twice with and without -Wl,--no-insert-timestamp -> differ / identical (root-cause evidence). Memory was checked first (62 GB total) and the container was --memory 2g and --rm."
  ],
  "unverified": [
    "No GitHub Actions run of the edited workflows. actionlint is not installed, so the only workflow validation is the yaml/needs/output/bash -n scripts and tools/workflowlint.py. A run of the next build/release is the real test.",
    "The mingw build was not run on this Mac. The root-cause test used a trivial program with the same linker flags, not the real wizard or stub link. I did not rebuild the AurOS installer to confirm the PE timestamp is the only diff, though the first diff in the trivial test was the header timestamp at byte 137.",
    "The default-is-fastpath grep from build.yml was dropped. It only ran when the twin matched. The same guard remains in e2e.yml (an AurOS-Setup.exe put-back run requires its source's build/aurbridge to default to 1), in e2e.py source_default_fastpath, and in tools/site/check.mjs DEFAULT_FAST for the website build attribute. Note that e2e.yml's guard runs only in put-back runs, so a build whose default was flipped would now be caught by those and not by build.yml itself.",
    "Historical records were left untouched: docs/handoff/streams/E.md, streams/F.md, SESSION-2026-10-23-B.md, docs/reviews/CI-TRUTH-2026-10-24.md and ci-truth.json still describe e2e-fastpath and AurOS-Setup-fastpath.exe as they were. tools/e2e/fixtures/program-open-false-fastpath.json keeps its name and its source note, since it is a captured screenshot fixture. Old releases such as v0.1.0-0e5cf01 still carry the asset.",
    "The separate .github/workflows/fastpath.yml (gapselftest, handovertest, mutants, its own exe-fast artifact build) is a different feature-test workflow. I did not touch it."
  ],
  "notes": "The lane is done and committed on agent/retire-fastpath-lane (commit 680658ed). Nothing was pushed and no workflow was triggered.\n\nWhat I deleted:\n- **build.yml:** the 'the same installer, built with AUROS_FASTPATH=1' step, the fastpath_sha256 job output, and the dead [fastpath] subject-marker case with its COMMIT_MSG env. The marker only exported the default value and appended a build-info line that nothing reads.\n- **release.yml:**\n  - The e2e-fastpath job.\n  - The header comment block entry.\n  - The publish step: cp/sha, the fastpath_sha256 output, the SHA256SUMS entry, the notes table row, and the gh release upload/create entries.\n  - The read-back loop entry.\n  - The windows job's FASTPATH_SHA256 env and its 'fast-path installer, if published' download and navtest step.\n- **e2e.yml:** the 'AurOS-Setup-fastpath.exe' name allowance (the guard now asserts name == 'AurOS-Setup.exe'), E2E_LABEL, and the '-fastpath' evidence artifact name suffix.\n- **tools/e2e/publish.sh:** E2E_LABEL.\n- **tools/e2e/e2e.py:** the AurOS-Setup-fastpath.exe early return in bitlocker_pause_lane_error. That is the behaviour change: the retired asset name is now refused. The error message was adjusted.\n- **tools/e2e/selftest.py:** tests updated. It now checks the retired asset is refused, the release has no e2e-fastpath or fastpath asset, and build.yml has no twin build. The 'label' publish mode and the fastpath URL cases were converted.\n- **tools/releasegate.py and tools/workflowlint.py:** their e2e-fastpath fixtures and comments removed.\n- **Docs:** tools/e2e/README.md, docs/AURBRIDGE.md ([fastpath] marker clause) and the 11-03 handoff bullet.\n\nThe ordinary release e2e job already runs base,bitlocker with bitlocker_putback: true, so the put-back proof is unchanged and promote's needs list did not change. hardengate has no fastpath references.\n\n`SESSION-2026-11-03.md` says the twin 'should now be byte-identical and is not'. That is explained by the missing -Wl,--no-insert-timestamp."
}
```

## impl:putback-wording

```json
{
  "branch": "agent/putback-wording",
  "commits": [
    "4a3fcf40 put windows back: say her files are copied into Windows first, not lost with AurOS"
  ],
  "root_cause": "The wording (\"Anything you saved in AurOS goes with it\", plus 'copy to a memory stick first') predates the way-back file copy and is false for BOTH install kinds, not just the fast path. answer.sh back_files runs before the restore is armed. On a fast-path install (/etc/auros/installer-choices.conf has via=gap) it uses handover.sh and a Windows task (putback.ps1) that copies the files into the user's Windows profile; on a legacy install it runs ferry-back straight into C:. Both copy Desktop, Documents, Downloads, Music, Pictures and Videos into C:\\Users\\<her>\\From AurOS. If the copy cannot be done, nothing is armed and nothing is removed.\n\nWhat is carried:\n- The six folders only. The copier skips .~lock.* files, .directory, .Trash-* and .thumbnails.\n- Programs and settings are not carried. Neither are files outside those six folders.\n- A folder that is a symlink is not followed. The copy then ends 'some could not be saved' and Windows is not put back.\n- Names are made Windows-safe. A name already taken becomes 'name (from AurOS)', and nothing in Windows is overwritten.\n\nWhen it fails (nothing removed, she is told to use the existing 'Save my files to a USB stick first' row):\n- Fast path, too many files: handover.sh needs the handover file in ONE run of blocks, and the docs say that is about 1-2 GiB at most. It also needs size plus 2 GiB free in AurOS. Otherwise exit 3 or 4 gives why=toomany.\n- Fast path, encrypted AurOS root: exit 5, why=locked.\n- Legacy path: not enough room in Windows gives why=noroom. A locked or BitLocker C: gives why=locked. A dirty drive gives why=dirty.\n- Fast path, after the restart: if C: lacks room, putback.ps1 shows a message and waits. The files stay safe on the handover partition.\n\nRuntime detection of the install kind exists (grep -qx via=gap /etc/auros/installer-choices.conf, used only by answer.sh handover_wanted). settings.c does not need it, because one wording is true for both.",
  "changed_files": [
    "src/aurshell/settings.c",
    "rootfs/usr/share/auros/help/windows.html",
    "website/guide.html",
    "website/install.html",
    "tools/helptest.py",
    "tools/site/check.mjs",
    "tools/windowspagetest.c",
    "docs/TRY-IT.md",
    "docs/handoff/streams/K.md"
  ],
  "tests_run": [
    "python3 -I tools/helptest.py (with fix) -> 50 passed, 0 failed (was 49; I added a 'old sentence is gone' check)",
    "python3 -I tools/helptest.py with the four product files (settings.c, windows.html, guide.html, install.html) reverted to HEAD and the new checks kept -> 48 passed, 2 failed: the one-sentence check lists all four places missing, and the old sentence is still in all four",
    "tools/windowspagetest.c built and run on homebase, since it does not compile on macOS (st_mtim), with the fix -> 22 checks, 0 failed (was 19)",
    "same, with settings.c reverted to HEAD -> 22 checks, 2 failed (the row says the files are copied; the page does not say they go with AurOS)",
    "node --check tools/site/check.mjs -> syntax ok",
    "Scratch node script that evaluates the real 'putback:' expression extracted from tools/site/check.mjs against the repo (no browser). With the fix: code does putback carries-files, and install.html's data-claim putback:carries-files is OK. With the four product files reverted: code says 'other', install.html's old claim removes-saved MISMATCH, and the stale sentence is found in guide.html and install.html.",
    "tools/looktest.c and tools/targets.c (both compile settings.c) built on homebase. looktest shows 9 FAIL lines on both unmodified HEAD and my tree, so they are not from this change (theme-helper cases, environment). targets exits 0.",
    "homebase: free -h checked first (52 GiB available). Used the host directly with mktemp dirs under ~/work, no container, and removed them afterwards."
  ],
  "unverified": [
    "node tools/site/check.mjs was NOT run: playwright is not installed locally (npx would have had to download it), so none of the browser checks ran. This includes the in-page data-claim and STALE checks, the printable-guide fit, and the 300 KB first-screen budget. I added no images and only a few lines of text. guide.html grew by about 25 characters, and I did not check that the printed sheet still fits on one page.",
    "My new STALE rule in check.mjs, /saved in AurOS goes with it/i, was never run in a browser.",
    "The Settings page sub-text is now about 154 characters, up from about 117. It is drawn in at most 3 wrapped lines, and I did not render it, so it may be cut off with an ellipsis at large text sizes or narrow screens. Rendering it needs tools/setsheet.c plus the shots environment.",
    "Pre-existing bug found, not fixed: settings.c CHOICE_LEN is 72, so a choice's note is cut at 71 characters. The 'Save my files to a USB stick first' note is about 87 characters, so its ending ('Nothing is removed.') is probably cut off on screen. That is why the Remove row says the short 'Your files are copied into Windows first.' and the long form is on the page. I did not run this against the stick row.",
    "I did not run the end-to-end behaviour. The fast-path put-back journey on a BitLocker PC is still being proven by the lead (4e7957d5), and I did not prove it. The wording says files are copied before AurOS is removed, which I read from answer.sh, handover.sh, ferry-back and putback.ps1, and did not execute.",
    "'Programs and settings are not carried' is the code reading: only the six XDG folders are copied. I did not test what happens to hidden files in her home folder or to browser data. 'The way she set AurOS up' is my plain wording for settings.",
    "The legacy AUROS_FASTPATH=0 path was read in the code (ferry-back writes straight into C:\\Users\\<her>\\From AurOS) and was not run.",
    "The root-side 'why=' notes in answer.sh (e.g. 'Save them to a USB stick first') are unchanged, and I did not re-check the welcome.c blocked-screen wording. They are already honest about the failure cases.",
    "docs/research/people-walkthrough-2026-10.md (a dated review) and docs/DESIGN-DIRECTION.md still quote the old sentence as history. I left them alone on purpose, and docs/handoff/gate-status.json too. website/index.html no longer contains the old sentence."
  ],
  "expected_ci_flips": [
    "words.yml and the tests.yml helptest step: 49 to 50 checks, still green, and red if any of the four places reverts.",
    "tests.yml windowspagetest step: 19 to 22 checks, green.",
    "site check (check.mjs), whichever workflow runs it: the claim putback:removes-saved is replaced by putback:carries-files in install.html and in the code expression. Not run locally, so it is the first thing to watch."
  ],
  "notes": "The branch has one commit and nothing was pushed. The wording was changed everywhere to one text that is true for both install kinds, so no runtime detection was needed.\n\nThe pinned sentence is now: 'Your documents, pictures, music and videos are copied into Windows first.' Where it appears:\n- settings.c page: 'This computer restarts and removes AurOS. Your documents, pictures, music and videos are copied into Windows first. If they cannot be, nothing is removed.' I dropped 'Windows gets all its space back' from the page to keep it short, and it is still on the help page.\n- settings.c Remove row: 'Your files are copied into Windows first.' It is short because of CHOICE_LEN.\n- windows.html: the full folder list (Desktop and Downloads too), 'inside your own folder in Windows', and what is not carried. The step that said 'Copy anything you saved...onto a memory stick' is replaced by a step explaining the failure case and the 'Save my files to a USB stick first' button. It also says to leave the computer on while a window shows the files being put into Windows, and adds a one-line prudence note about photos on a memory stick.\n- guide.html: short form.\n- install.html: the data-claim is now putback:carries-files. It also says programs do not come back and that the stick is the fallback.\n\nThe memory-stick advice is kept only for the case where the copy cannot be done.\n\nCheck changes:\n- helptest.py loss regex and places: settings.c now counts 1 instead of 2. There is an added check that the old sentence does not come back in settings.c, guide, install, index or the help page.\n- check.mjs putback claim: it now also requires answer.sh to contain 'elif ! back_files; then', so the claim follows the code and not just the wording. A STALE rule for the old sentence is added.\n- windowspagetest.c: 3 new checks.\n\nDocs updated: TRY-IT.md and the K.md pinned-sentence section."
}
```

## attack:retire-fastpath-lane:1

```json
{
  "verdict": "sound",
  "issues": [
    {
      "severity": "minor",
      "file": ".github/workflows/build.yml",
      "what": "A comment left behind still refers to the deleted fast-path rebuild. It was not updated with the step it describes.",
      "evidence": "Line ~318 on agent/retire-fastpath-lane reads 'Copy them before the optional fast-path rebuild touches out/: build-info therefore identifies the normal installer...'. No such rebuild exists any more. It is harmless but misleading."
    },
    {
      "severity": "minor",
      "file": ".github/workflows/build.yml",
      "what": "The build.yml check that build/aurbridge defaults to the fast path is gone, so the default is no longer checked on every build. Coverage is still real but sits in cheaper places, and the agent said this itself.",
      "evidence": "The default is still checked in e2e.yml:165 (put-back runs only), tools/e2e/e2e.py:468 (source_default_fastpath), tools/customize/test_base_build.py:441-445 (a unit test of the same grep) and tools/site/check.mjs:920 (DEFAULT_FAST against the site's data-build attribute). Only the build.yml copy was dropped, and it ran only when the twin matched, which in practice never happened."
    }
  ]
}
```

## attack:othersys-cinnamon:1

```json
{
  "verdict": "sound",
  "issues": [
    {
      "severity": "minor",
      "what": "The firstboot hang at switch-root is not fixed. The firstboot checks (SESSION-UP, ISO gone, root grown, BootNext) will only flip if the hang does not recur. The agent says this plainly. My check: two earlier runs passed Cinnamon firstboot in about 55 s with every check yes (41538432102, 41538963268). Both ran the same src/othersys code as the failing run 41539330303, because the only diff between those commits is 5 lines in .github/workflows. So the hang is not tied to the helper code, but nothing here shows whether it is a flake or a Cinnamon-specific first-start problem.",
      "evidence": "git diff --stat 00797e0e 6ae5e01 -- src/othersys tools/othersys .github shows only .github/workflows changes. Results of 41538432102 and 41538963268 show 'firstboot power on to the desktop session' at 54.4 s and 55.4 s. Run 41539330303 serial-firstboot.log ends at 'Starting initrd-switch-root.service' with no output from the new root's systemd. The lead should expect the Cinnamon firstboot to possibly fail again on the next VM run."
    },
    {
      "severity": "minor",
      "what": "The new selftest case replaces m.STATE_DIR, m.INBOX and m.remove_iso and puts them back at the end of the case, not in try/finally. If cmd_boot raises, the module stays pointing at the temp paths and the monkeypatched remove_iso. Only the failure path is affected. It also covers only the 'boot is slow, answer arrives meanwhile' order. The 'answer first, boot after' order is not tested, though it is trivially correct because boot reloads state under the lock.",
      "file": "tools/othersys/selftest.py",
      "evidence": "Diff lines: real_dirs/real_remove_iso are restored after racer.join() and the expect() calls, with no finally."
    },
    {
      "severity": "minor",
      "what": "The lock file is opened before cmd_boot's 'no state' check. If cmd_boot or cmd_answer is run by hand where /var/lib/auros-othersys does not exist, the user gets a FileNotFoundError traceback, not the old clean 'boot: no state; this system was not installed by AurOS' log line. The boot unit's ConditionPathExists=state.json and the answer .path unit's PathExists guard mean systemd never hits this, so I rate it cosmetic.",
      "file": "src/othersys/ubuntu/othersys.py",
      "evidence": "The one_at_a_time wrapper does open(os.path.join(STATE_DIR,'lock'),'w') before fn(plan) runs. Both units are guarded as described: auros-othersys-boot.service has ConditionPathExists=/var/lib/auros-othersys/state.json, and auros-othersys-answer.path has PathExists=.../inbox/answer."
    }
  ]
}
```
