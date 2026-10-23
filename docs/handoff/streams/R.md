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
- commit 1 (this session) "copy as her": everything in Architecture.
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
- `bench.sh`: see Measurements.

## In progress / next, in order

1. Commit 2 "the import works in its real sandbox": unit
   (`auros-answer.service`, stream BT's: ProtectHome=read-only,
   `-/home`, StateDirectory=ferry, RuntimeDirectory=… ferry);
   `mount_ro`'s read-only check (it grepped /proc/mounts for "(ro," —
   never matches, so every device mount was undone); `ferry run` exit
   codes (0 all, 2 something missing, 3 refused, 1 nothing) and
   answer.sh mapping 1 → failed (was "partial" = "Your files are
   here"). Proof: `tests/insystemd.sh` (systemd PID 1 container) runs
   `tests/sandbox.sh` under every [Service] property of the unit, on a
   tree and on a real NTFS loop volume Ferry mounts itself.
2. Commit 3 plain words: `ferry run --plain` (chatter → STATE/ferry.log,
   her lines on stdout), `ferry-report --plain` → "What came from
   Windows.txt" in her Home (as her), answer.sh uses --plain. Parked
   versions of `ferry`/`ferry-report` were in the scratchpad
   (`r-park/c3/`); if gone, re-derive from this description.
3. Commit 4 bookmarks: `chromemarks.c` (written, untested) → Edge/Chrome
   `Bookmarks` JSON → bookmarks.html. Plan: one Firefox profile "from
   Windows" made the default (profiles.ini/installs.ini rewritten as her,
   backups kept; Windows `[Install…]` sections dropped — finding 6); a
   fresh profile with bookmarks.html imports it on first start. Prove
   with a real Firefox (homebase docker, Mozilla apt).
4. Commit 5 fuzz: `fuzz/{hive,names,marks}.c` + `fuzz/run.sh` (clang;
   the `auros-s3-fuzz` image on homebase has clang 18). hivepeek's
   INT32_MIN negation is fixed in the tree (finding 7). Regression
   inputs → `tests/regress/`, and an ASan build of hivepeek over
   corrupted hives in the suite (FERRY.md claims one exists; it doesn't).
5. `tools/nmtest.sh`: drop its Ferry Wi-Fi section (emitter deleted on
   purpose in fb90bcd) — another stream's file, smallest edit.
6. `docs/FERRY.md`: still says passwords and Wi-Fi keys migrate and
   describes safe_copy; rewrite those sections.

## Measurements (homebase, shared and busy; ext4 on NVMe)

See the commit-1 message / bench.sh output. The shell copier this
replaced: ~26 ms per file (131 s for 5,000 small files, measured in the
old container).

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
