# Stream OPS — addresses, monitoring, incidents, launch waves, distribution (state, 2026-10-23, session B)

**Goal.** No installed PC depends on a GitHub address; the owner learns about a broken download, update or
cloud within 15 minutes and has a rehearsed fix; the launch opens in waves that stop by themselves.
Brief: `docs/PLAN-END-TO-END.md` section 4 "OPS" (WP-15, 35, 60, 74, 82, 83, 94). Owns `infra/**`,
`.github/workflows/{monitor,drill,fleet,edge}.yml`, `docs/ops/**`, `tools/ops/**`.

## The owner has not bought the domain yet (decision 1, he decides 2026-10-24)
Everything is built to work the moment he sets **one repository variable, `AUROS_DOMAIN`**, plus the secret
`AUROS_OPS_TOKEN` (steps with exact commands: `docs/ops/DOMAIN.md` "What the owner does"; also OWNER-SETUP 8).
Until then every address is GitHub's and nothing is deployed. Nothing built so far was deployed to Cloudflare.

## Done and pushed (all on main; commit subjects start with the WP)
- **WP-15 address plumbing** (6457ce6): `tools/repo.sh` gained `apt|download|get` and `AUROS_DOMAIN`
  (site/host become `<domain>/`, apt `updates.<d>/apt/`, pieces `get.<d>/download`, installer `get.<d>/`);
  `build/aptchannel`, `updatepolicytest.sh`, `aptrepo.yml`, `build.yml` (also through `sudo` to forge),
  `release.yml` follow it; `repotest.sh` proves the domain answers and 8 refused names (mutants shown red).
- **WP-15 scan** (`tools/ops/addrtest.py`, step in `tests.yml`'s unit job): the image's AurOS files (what the
  `auros-*` dpkg lists name + AurOS dirs), every byte of the staging initramfs, the exe (ASCII and UTF-16).
  Fails on github.com / github.io / githubusercontent.com / workers.dev when `AUROS_DOMAIN` is set (or `--strict`);
  before that it reports. `--selftest`: planted string in each of the three places, a wide string, a dpkg-listed file,
  an Ubuntu file that must be ignored. On build 39907966817 it finds exactly: the pieces base, the wizard host
  twice; `tools/ops/addr-allow.txt` lists ntfs-3g's and shim's own upstream URLs. Also run on the real
  rootfs of build 39926230164 (207 AurOS files, extracted on homebase): exactly one hit, `auros.sources`' GitHub address.
- **WP-15 website** (stamp.sh, door.js, check.mjs, pages.yml): with the domain, Get AurOS goes to `get.<d>`,
  the page names no GitHub release address. check.mjs ran on homebase with and without a domain: identical.
- **WP-15 edge Worker** (`infra/edge`, `edge.yml`): get./updates./status./www./apex passing on to GitHub; 14 node
  tests, 9 mutants shown red. Deployed **only** with the variable + `AUROS_OPS_TOKEN` + a Cloudflare token that may
  edit the zone. Never deployed yet, so the Cloudflare side (custom domains, DO migration) is **unproven live**.
- **WP-74 waves + stop rules** (`infra/edge/src/gate.js`, `tools/ops/waves.py`, `docs/ops/LAUNCH.md`): wave 0 closed
  by default; caps 25/100/300/none per UTC day; counted = whole-file GET only; pieces never capped or paused; pause by
  owner or second responder token, resume/wave/status owner only; stop rules (data loss 1, failure after shrink > 2%,
  put-back > 15% of >= 5 finished, unreadable numbers keep it shut) tested with 6 mutants; `waves.py wave 1` refuses
  while `docs/ops/live.txt` lacks the five checks.
- **WP-35 monitor** (`monitor.yml`, `tools/ops/monitor.py`, MONITOR.md): 15-minute cron, hourly deep hash check,
  checks site/download/pieces/channel/wishes(+401 probe)/flathub/gate; each tested down four ways (timeout, 404,
  empty, reset) and with nothing listening, 83 cases (gpg ones run in CI and on homebase); issues assigned to the
  owner (+ `AUROS_SECOND_RESPONDER`), label `monitor`, self-closing. `docs/ops/live.txt` = what pages (today: wishes).
  Run live on main: green, four others "pending" (site/download/channel are 404 until the first `[latest]`).
- **Status page**: `website/status.html` + `website/status.json` (default "not checked yet") + one footer link in
  `website/index.html` (D's file, announced). Live data arrives from the edge Worker's `/status.json` once the domain exists.
- **INCIDENTS.md** (5 playbooks) + **drill.yml** run for real on a throwaway prerelease (run 39927811756): monitor
  red 42-53 s, issue assigned to the owner 54-65 s, repaired 13-51 s, `docs/ops/drills.md`.
- **COSTS.md** (WP-60 part): limits from the providers' pages, a stated model for 1k/10k/100k, what breaks first.
- **fleet.csv** (`fleet.yml`, `fleet.py`): daily row as an asset of prerelease tag `fleet`.

## Not done (in order of value)
1. **After the owner sets the variable**: run `edge` once and watch it; fix whatever Cloudflare says (custom domain on an
   apex, DO migration). Then `gh workflow run monitor.yml`; add `gate` words to `docs/ops/live.txt` as things go live.
   Then a build with the variable set: addrtest must be green (strict) - the exe still carries the pieces base and
   wizard host until a build runs with `AUROS_DOMAIN`.
2. Others owe WP-15 pieces (listed at the end of `docs/ops/DOMAIN.md`): CL (`wish.<d>` route, outcome numbers in the shape
   LAUNCH.md gives), A2/B (several `base` lines in pieces.txt), S5 (`mirror+file:` and the "moved" updatetest).
3. WP-82 (Sev-1 lane, `docs/ops/SEV1.md`, the rescue stick with A2), WP-83 (second responder drill: needs the person),
   WP-94 (lookalike watch, the wizard's own-hash check with B/S2), WP-60 rest (R2 mirror, `[hotfix]` lane, org move).
4. The monitor cannot yet see: Anthropic spend, the share of Wishes answered "resting" (needs a counter from the Worker;
   ask CL), security.txt, outcome numbers (WP-73).
5. Drill the gate (pause/resume, the second responder under 15 minutes) once the domain exists.

## Traps
- `/health` on the Wishes Worker is 404; its route is `/v1/health`.
- GitHub's issue lists lag seconds behind writes: poll, never read right after writing (cost two failed drills).
- A monitor selftest in CI must not wait between retries (`MON_FAST`); the test now sets it itself.
- The 4 `check.mjs` "promises" failures on index.html/advanced.html (words about going back) are stream D/K's, from 5aeb3c7;
  pages.yml is red on main because of them.
- Do not let anything install from a build made before `AUROS_DOMAIN` is set (PLAN section 3, rule 1).
- Cloudflare's terms limit big non-HTML files through the free proxy: the edge only redirects; keep it so (COSTS.md).

## How to verify
```sh
python3 -I tools/ops/monitor.py --selftest      # needs gpg for the channel cases (homebase or CI)
python3 -I tools/ops/addrtest.py --selftest
python3 -I tools/ops/waves_test.py
( cd infra/edge && node --test test/*.test.js )
sh tools/repotest.sh
python3 -I tools/ops/monitor.py --deep          # the real thing, against today's addresses (WISHES_URL=... for Wishes)
gh workflow run drill.yml --ref main            # the rehearsal, about 3 minutes
```
