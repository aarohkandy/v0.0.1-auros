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
3. DONE and PROVEN WITH A REAL FIREFOX (Mozilla's 158 in docker:
   `tests/infirefox.sh` → `tests/realfirefox.sh`, 14/14) — bookmarks.
   `chromemarks.c` → `ferry-chromemarks` (Edge/Chrome/Brave `Bookmarks`
   JSON → bookmarks.html; http/https/ftp only, HTML-escaped, caps).
   Things Firefox 158 does that the first design got wrong, all found by
   running it: (a) it keeps profiles in `~/.config/mozilla/firefox`
   (Firefox 147+), but uses `~/.mozilla/firefox` if only that exists —
   Ferry uses the XDG folder when it exists, else the old one; (b) a
   profile merely marked `Default=1` is IGNORED (Firefox makes a new empty
   one and writes its own `[Install<hash>]`): to have it open ours, Ferry
   writes `[Install<hash>] Default=<profile>` in profiles.ini and
   `[<hash>]` in installs.ini; (c) `<hash>` = CityHash64 of the UTF-16
   folder holding the firefox binary (`ferry-ffhash`, `ffhash.c`; checked
   against a real Firefox: /usr/lib/firefox → 4F96D1932A9F858E, and the
   Windows 308046B0AF4A39CB; paths over 32 characters are refused because
   that branch is unconfirmed); (d) `bookmarks.html` in a never-run
   profile is imported on first start with NO extra preference (a
   `user.js` pref would re-import every start); (e) `[ProfileN]` numbers
   must be contiguous or Firefox stops reading at the gap; (f) a second
   start does not duplicate. Policy: Firefox is switched to the profile
   from Windows only if she has not used Firefox here (no
   `[Install<hash>]` for this install, or it is one Ferry made earlier);
   if she has, or it is open, nothing is switched and Edge's file goes
   to her Home with plain steps. Windows profiles.ini/installs.ini are
   never copied. Suite 125 user / 131 root.
4. DONE (needs mutants confirmed) REVERSE FERRY — lead's I-01/E-01:
   "Put Windows back" no longer deletes what she saved in AurOS.
   `ferry back [--plain]` (`ferry-back` + `ferry-copy --to-windows`)
   copies her AurOS Documents/Pictures/Desktop/Downloads/Music/Videos
   into `C:\Users\<her>\From AurOS\<Folder>` (plus `Read me.txt`),
   from INSIDE AurOS at the moment she asks, because the restore later
   runs from a staging environment that cannot see her decrypted home.
   No change in src/aurstage was needed. Hook: `answer.sh` `putback`
   runs `back_files` after `staging_present` and BEFORE reading the
   clock/arming; only exit 0 arms the restart, anything else answers
   `result=failed` + `code=N` + a plain `note=`. Exit codes 0 all saved,
   2 some not, 3 Windows hibernated/dirty, 4 not enough room (nothing
   copied), 5 drive could not be opened, 1 failed. Windows-safe names,
   case-blind collisions (`name (from AurOS).ext`), nothing overwritten,
   room checked first (`FERRY_ROOM_BYTES` is the test seam), the drive
   mounted read-write only here (`mount_rw`, ntfs-3g, her uid; a test
   checks nothing else can). Windows user = `$FERRY_STATE/winuser`
   (recorded by ferry-files), else the only user, else `Users\Public`.
   Proof: `tests/sandbox.sh` scenario 3 (27/27 in `tests/insystemd.sh`):
   the photo saved in AurOS is on a real NTFS volume byte for byte after
   `answer.sh putback` under every property of the unit; no room /
   hibernated: nothing armed. Suite 164 user / 170 root.
   **COMPLETION SIGNAL for stream C (E-01)**: `ferry run` and `ferry back`
   remove `/run/auros-answer/ferry.done` at start and write it whole at
   the end: `kind=import|back`, `result=ok|partial|refused|noroom|
   unopened|failed`, `files=N`, `at=`. Absent = not finished. Progress
   text while running: newest line of `/run/auros-answer/import.log`
   (import) or `/run/auros-answer/putback.log` (back). Ask C to show the
   `note=` from `/run/auros-answer/result` on failure, and to decide what
   to offer when it fails (see "For the lead").
5. DONE fuzz: `fuzz/{hive,names,marks}.c` + `fuzz/run.sh` (libFuzzer +
   ASan + UBSan; clang is in homebase's `auros-s3-fuzz` image:
   `docker run --rm -v <repo>:/w:ro -v <out>:/out auros-s3-fuzz sh -c 'cp
   -r /w/src/ferry /tmp/f && cd /tmp/f && FUZZ_WORK=/tmp/fw sh
   fuzz/run.sh 60'`). Clean after fixes: hive 0.8M, names 2.6M (incl. the
   Windows-name sanitizer and case-folding registry), bookmarks 2.0M
   executions in 60 s each. FOUND AND FIXED: (a) chromemarks.c read up to 4
   bytes past the end of the file when it ended mid-"true/null/false"
   (heap over-read, ASan); (b) make_name returned an EMPTY name for a
   name of only UTF-8 continuation bytes; (c) hivepeek negated INT32_MIN
   (UB). Kept found: `tests/regress/*` replayed by `tests/run.sh` under
   gcc ASan+UBSan through `fuzz/replay.c`, plus 3,000 corrupted hives and
   1,500 corrupted bookmark files made with a fixed seed; mutants put (a)
   (b) (c) back and watch the replay fail. The old FERRY.md claim of "ASan
   over thousands of corrupted hives" is now true. Not fuzzed yet:
   `ffhash` (tiny, validated against real values), `ferry-winattr`, the
   shell parsers (ferry-wifi XML, windowsZones).
6. `tools/nmtest.sh`: drop its Ferry Wi-Fi section (emitter deleted on
   purpose in fb90bcd) — another stream's file, smallest edit.
7. DONE `docs/FERRY.md` rewritten to match the code (passwords, copier,
   browser, way back, signal, tests).

## Disk hygiene (rules this stream learned the hard way)

`tests/run.sh` once left a 340 MB `/tmp/ferrytest.*` per mutant on homebase
(234 of them, 28 GB): `mutants.sh` piped the suite into `grep -q`, which
leaves at the first match, the suite died of SIGPIPE, and dash runs an
EXIT trap only on a normal exit. Now: every script that makes a directory
traps EXIT **and** INT TERM HUP PIPE; mutants.sh writes to a file instead
of a pipe; the suite frees its big fixtures as soon as they are used,
removes its directory itself at the end and ASSERTS it is gone ("no mess
left behind"); data is smaller (progress file 32 MB, kill test 600 x 20
KB). Checked: gone after normal end, SIGPIPE, TERM and INT; and with the
fix removed a directory is left. Never `pkill -f` a pattern that appears
in your own ssh command line.

## For the lead (decisions and risks that are not mine to make)

- **S-08 / R-18 / R-19 (FINDINGS.md) are fixed on Ferry's side; C owns
  the panel.** `result=choose` + `users=Bob,Harold` (several Windows
  accounts, none chosen) needs a "whose files?" screen that sends back
  `import:NAME`, and `src/aurfirst/request.c`'s fixed word list (`is_one_of_ours`)
  must learn `import:` + a name; `result=empty` (`files=0`) needs its own
  sentence; `result=ok` now always carries `files=N`.

- **A BitLocker-locked or otherwise unopenable Windows drive blocks "Put
  Windows back"** (exit 5): the files cannot be saved there, and removing
  AurOS would delete them. Safe default, but she needs a way out: "save
  them to a memory stick", or "put Windows back anyway". That is a UI
  decision (stream C/B) plus a new answer word in aurfirst's request
  allow-list (`src/aurfirst/request.c`); Ferry exposes everything needed
  (`code=`, `note=`, `putback.log`).
- Not enough room in Windows is the next most likely block (the install
  shrank C:). The note says so; freeing space in Windows is the fix.
- The first `ferry back` of a large library can take many minutes; the
  "within the hour" freshness check of the restore is read AFTER the
  copy (answer.sh `read_asked`) so it does not eat into it.
- The unit (`auros-answer.service`, stream BT's) now has `ProtectHome=no`
  and `-/home`; `ProtectHome=read-only` + ReadWritePaths does not work.

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
