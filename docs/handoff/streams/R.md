# Stream R — Ferry: her files come across from Windows

Owner of `src/ferry/**` (and this file). Brief (docs/handoff/STREAMS.md):
when she says "it works", her Documents, Pictures, Desktop, Downloads,
Music, Videos and her browser bookmarks come across from Windows,
plainly and safely: read-only on the Windows side, **never her
passwords**, honest byte-weighted progress in plain words, fast (5,000
files must not take minutes), safe with OneDrive placeholders and odd
filenames. Fuzz the parsers, measure a large fixture, a test for every
behaviour shown to fail when broken. Build and test on homebase.

## How Ferry is reached (read this first)

Welcome panel (`src/aurshell/welcome.c`) → "Bring my files across" →
`import` in `/run/auros/answer` → `rootfs/usr/lib/auros/answer.sh` (root,
in `auros-answer.service`'s systemd sandbox) → `aurfirst ferry` gate →
`/usr/bin/ferry run --auto >> /run/auros-answer/import.log` → `ferry
report`. The panel shows the newest line of import.log while it runs.
Installed by `src/ferry/install.sh` (forge calls it; forge's hardening
gate checks every ELF in /usr/lib/ferry).

## Architecture now

- `copy.c` → `ferry-copy`: THE copier (files stage and the Firefox
  profile). One process, all folders: plans totals first, then copies.
  Root opens the Windows folders and Ferry's records, then becomes her
  (setgroups/setresgid/setresuid, checked; refuses root or `--as 0`).
  Never follows a link on either side's source; temp file → one syncfs
  per batch → `renameat2(RENAME_NOREPLACE)`; collisions "(from Windows)",
  "(from Windows 2)"…; names shortened to fit 255 bytes at a UTF-8
  boundary (reported); mtime kept; leftover `.ferrypart-*` removed;
  out of room stops cleanly. Report rows → `STATE/report.d/<stage>-copy.tsv`
  (rewritten each run), copies → `manifest.tsv`, progress JSON →
  `STATE/state/<stage>.progress` (pct by bytes + 64 KiB per file), plain
  line for her on `FERRY_PLAIN_FD` about once a second ("Copying your
  Pictures: 1,200 of 5,000 files. About 4 minutes left."). No file name
  ever reaches that line.
- `winattr.c`: the classifier, shared (copy.c includes it). **Fixed**: a
  OneDrive cloud reparse tag no longer means "not here" (hydrated
  OneDrive files keep it); only RECALL_ON_DATA_ACCESS / OFFLINE /
  RECALL_ON_OPEN do. Name-surrogate tags (junctions, symlinks) are
  aliases. size>0 with 0 blocks = `suspect`: ferry-copy reads it and
  keeps it only if a byte is non-zero (NTFS resident files are real;
  an all-zero one was never on the disk).
- `ferry-common.sh`: `path_ci` refuses `.`/`..` components and any
  symlink component (one guard for every registry-derived path: a
  `Wallpaper` of `C:\..\..\etc\shadow` or a junction where Documents
  should be no longer reach outside the volume). `xdg_dir` reads
  user-dirs.dirs as her and only accepts folders inside her home.
  `as_her` (setpriv), `copy_as`, `her_copy` replace every root write
  into her home (`safe_copy`, `own`/`chown -R` are gone).

## Done (newest last)

- `fb90bcd` never a password (Firefox secrets skipped, Wi-Fi only listed,
  ferry-dpapi deleted). Broke `tools/nmtest.sh`'s Wi-Fi section (it evals
  the removed `emit_nmconnection`): **still to fix, see Next**.
- `a75e9a7` "copy as her": everything in Architecture.
  Tests: `tests/run.sh` 79 non-root / 85 as root (`tests/asroot.sh`,
  throwaway container). New fixtures: hydrated OneDrive file with cloud
  tag, all-zero sparse file, odd names (newline, tab, quote, backslash,
  leading dash, trailing space, CJK), dot in a folder name, two-deep
  collision, 250-byte name, stale temp, an old mtime; a hostile volume
  (junction for Pictures, junction mid-path for Videos, `..` in
  Documents and Wallpaper, a junction among Firefox profiles) and a
  hostile home (user-dirs.dirs pointing out by `..` and absolutely;
  as root: symlinks to a root-owned folder at ~/Pictures and ~/.local).
  `tests/mutants.sh` puts each bug back and checks the named test fails.
- `bench.sh`: see Measurements. `mutants.sh`: 31 mutants (user and root).

## In progress / next, in order

1. DONE commit 2 "the import works in its real sandbox". Three blockers,
   each of which alone made "Bring my files across" fail on every
   machine: (a) the unit's sandbox (state dir read-only, /home hidden;
   `ProtectHome=read-only` + `ReadWritePaths=/home` is NOT enough —
   systemd keeps the stricter entry for the same path — so
   `ProtectHome=no`, `-/home`, StateDirectory=ferry,
   RuntimeDirectory=… ferry); (b) `/usr/bin/ferry` is a symlink and
   Ferry looked for its tools beside the link (`readlink -f`); (c)
   `mount_ro`'s read-only check grepped /proc/mounts for "(ro," and
   undid every mount (`is_ro_mount`). Plus exit codes: `ferry run` 0
   all, 2 something missing, 3 refused, 1 nothing came (ferry-copy
   exits 1 when not one file came); answer.sh maps 1 → failed (was
   "partial" = "Your files are here"). Proof: `tests/insystemd.sh`
   (systemd PID 1 container on homebase) runs `tests/sandbox.sh` under
   every [Service] property of the unit: 11/11 with the fixed unit
   (a tree, and a real NTFS loop volume Ferry mounts itself, OneDrive
   attributes and cloud tags written by ntfs-3g, volume byte-identical
   after); 3/11 with the shipped unit.
2. DONE commit 3 plain words: `ferry run --plain` (answer.sh uses it):
   import.log gets only plain sentences ("Looking at the Windows part
   of this computer.", "Copying your Pictures: 1,200 of 5,000 files.
   About 4 minutes left.", "Bringing your bookmarks across."); Ferry's
   chatter → /var/lib/ferry/ferry.log. `ferry-report --plain` →
   "What came from Windows.txt" in her Home, written as her: files per
   folder, every file that did not come with the reason in a whole
   sentence, renamed ones, Wi-Fi network names, the passwords sentence.
   Suggest to the welcome panel's owner: its line "written down in the
   report" could say "in “What came from Windows” in your Home folder".
3. DONE (suite) commit 4 bookmarks: `chromemarks.c` → `ferry-chromemarks`
   (Edge/Chrome/Brave `Bookmarks` JSON → bookmarks.html; http/https/ftp
   only, HTML-escaped, depth and size capped). `ferry-firefox` rewritten:
   copies her Windows Firefox profile(s) via ferry-copy; Windows'
   profiles.ini is NOT copied (its [Install…] section claims the profile
   for Windows Firefox — finding 6); Linux profiles.ini/installs.ini are
   rewritten as her to make the Windows profile the default (backup
   `profiles.ini.before-windows`); with no Windows Firefox, a new profile
   `fromwindows.default-release` holding bookmarks.html is made the
   default (Firefox imports bookmarks.html into a new places.sqlite on
   first start); with a Windows Firefox AND Edge, Edge's file lands in her
   Home with plain steps; if Firefox is running here nothing is switched
   (it would write its own profiles.ini back). Suite 115 user / 121 root,
   mutants for each.
   **NOT YET PROVEN WITH A REAL FIREFOX**: `tests/realfirefox.sh` +
   `tests/infirefox.sh` (docker, Mozilla apt Firefox 158) exist and the
   first run FAILED for a test-harness reason, not (yet) a product one:
   Firefox printed "Exiting due to channel error" and never wrote
   places.sqlite — headless Firefox as a setpriv'd user in a container
   needs a working HOME/XDG_RUNTIME_DIR/dbus (try `MOZ_DISABLE_CONTENT_SANDBOX=1`,
   `--profile` for the case-0 control, or run Firefox as the container's
   non-root user with `HOME` set and `/tmp` writable; first prove a
   control: a fresh Firefox start creates places.sqlite). Until that
   passes, "her Edge bookmarks appear in Firefox" is unverified.
4. Commit 5 fuzz: `fuzz/{hive,names,marks}.c` + `fuzz/run.sh` are written
   but NEVER RUN (no crash claims). Run `sh fuzz/run.sh 120` on homebase
   with the `auros-s3-fuzz` image (clang 18; mount the repo, `docker run
   --rm -v ... auros-s3-fuzz sh -c 'cd src/ferry && FUZZ_WORK=... sh
   fuzz/run.sh 120'`). hivepeek's INT32_MIN negation is already fixed in
   the tree (finding 7); stream S3 also has `fuzz_hive.c` (bd946e9) —
   findings go to R. Regression inputs → `tests/regress/hive-*` (the
   runner already seeds from them) plus a suite step that feeds every
   file there to `ferry-hivepeek` and expects exit 0/1/2, never a signal;
   and an ASan build of hivepeek over corrupted hives in the suite
   (FERRY.md claims one; none exists).
5. `tools/nmtest.sh`: drop its Ferry Wi-Fi section (emitter deleted on
   purpose in fb90bcd) — another stream's file, smallest edit.
6. `docs/FERRY.md`: still says passwords and Wi-Fi keys migrate and
   describes safe_copy; rewrite those sections.

## Measurements (homebase, shared and busy; ext4 on NVMe; `tests/bench.sh 5000 10`)

5,000 files of 4 KB in three folders plus ten 20 MB videos (5,010 files,
219 MB), `ferry-files --only Pictures`:
- new (ferry-copy): **3.6 s** first run (1,400 files/s, 62 MB/s), 0.35 s
  re-run with everything already there;
- old shell copier (df2b5d8): **266 s** first run, 348 s re-run.
75x faster; 5,000 photos is seconds, not minutes.

## Traps

- homebase is shared: never run a host-wide `sync` (another stream's
  stuck `umount` made every global sync hang for 10+ minutes); use
  `sync -f`. ferry-copy only calls `syncfs` on her home's filesystem.
- `rsync --delete` of the worktree deletes `build/`: run `make` after.
- A `dup()`'d directory fd shares its offset: `rewinddir` after
  `fdopendir` (the copy pass found nothing until this was fixed).
- As root, tests need a real owner (`ferryher`, uid 4242) and homes she
  owns; `--owner root` is refused by design.
- POSIX sh: `_`-prefixed locals collide across functions.
- `tests/sandbox.sh` needs `/var/lib/auros` and `/run/auros` to exist
  (the unit's ReadWritePaths without "-").

## How to verify

```
cd src/ferry && make && sh tests/run.sh       # non-root
sh tests/asroot.sh                            # as root, in docker
sh tests/mutants.sh                           # each check bites
sh tests/insystemd.sh                         # inside the unit's sandbox
sh tests/bench.sh 5000 10                     # speed
```
