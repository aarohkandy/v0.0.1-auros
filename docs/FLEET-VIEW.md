# FLEET VIEW — the smallest thing that lets anyone notice the fleet has stopped updating

Status: **DESIGN ONLY, not built.** Written 2026-09-20 in answer to SYSTEM-REVIEW §2.4, H4, H16 and
its Closing section. Nothing here is implemented, and nothing here has been measured on hardware.

Labels used throughout:
- **[SOURCE]** — a file, line or decision in this repository, or a page read on the date given.
- **[JUDGMENT]** — my reasoning. Arguable. Replace it with a measurement when one exists (D24).
- **[OWNER]** — a decision this document does **not** make. Cost, accounts, data retention, or what a
  school is told. §9 of SPEC reserves these; they are collected in §11.

---

## 1. Why this exists at all, and why it is not §6E

SPEC §6E says the console is built **last**, after a real customer, and *"Do not build this
speculatively"* [SOURCE: `docs/SPEC.md` §6E]. This document proposes building a fraction of it
**before** the first pilot, which contradicts that sentence. **[OWNER] D-1: amend §6E** (or refuse, and
accept H4/H16 as open at pilot).

The case for the amendment, from the review:

1. **H4.** The machine-side canary has already been fixed to measure the right thing — it now reads
   the booted image's build time, `.status.booted.image.timestamp`, not just the fetch stamp
   [SOURCE: `auros-base/update-agent/greenboot/check/wanted.d/70-update-freshness.sh`, header item 3,
   `IMAGE_WARN_AFTER_DAYS=21`]. But it still writes its verdict to the boot-status motd, on a product
   whose D4.1 is zero-terminal [SOURCE: SYSTEM-REVIEW §2.4 last paragraph; DECISIONS D4.1]. **The
   detection is fixed; nobody can see it.** That half of H4 is what this closes.
2. **H16.** Key rotation step 2 is *"Wait until the fleet has taken an update containing both. Confirm
   from the console, not from an assumption"*, and skipping it strands every machine that has not
   updated, with no way back except physical access [SOURCE:
   `auros-base/signing/keys/README.md`, "Rotation"]. There is no console. So today rotation is unsafe
   by construction, and it becomes unsafe **the day the first key is minted** (B10).
3. **Closing.** *"A school that bought this today … would stop receiving security updates without
   anyone — them or us — being told."* [SOURCE: SYSTEM-REVIEW, Closing.]

Scope is deliberately the **read-only** part of §6E — *"list machines, show image/version"*. **Not in
scope:** pushing recipe changes, lock, wipe. None of those are needed to close H4 or H16, and every
one of them is a remote-control channel into a school laptop, which is a different security product.

---

## 2. What each machine reports

Only facts the machine already has, from `bootc status --json` or from files greenboot and the update
agent already write. **No verdicts** — the server computes staleness from raw facts, so a machine
whose own check is broken cannot report itself green (the §2.4 lesson: a canary must not grade its
own homework).

| Field | Source on the machine | Citation |
|---|---|---|
| `booted_digest` | `.status.booted.image.imageDigest` | [SOURCE] D25; `auros-update:153`; `gate1-exit.yml:222` |
| `booted_created` | `.status.booted.image.timestamp` (ImageStatus.timestamp, from the image's `org.opencontainers.image.created`, optional) | [SOURCE] `70-update-freshness.sh` header item 3, citing bootc v1.16.10 `spec.rs:206-217`, `status.rs:203-209` |
| `booted_ref` | `.status.booted.image.image.image` (the pulled reference, e.g. `ghcr.io/aarohkandy/auros-<recipe>:…`) | [JUDGMENT] field path from bootc's `ImageReference`; **confirm against v1.16.10 `host-v1.schema.json` before building** — not yet cited anywhere in this repo |
| `signature_mode` | `.status.booted.image.image.signature` — must be `containerPolicy` | [SOURCE] D25; `wanted.d/50-signature-enforcement.sh` |
| `staged_digest` | `.status.staged.image.imageDigest` (absent = nothing staged) | [SOURCE] `auros-update:230` |
| `rollback_digest` | `.status.rollback.image.imageDigest` (singular, D10) | [SOURCE] D25; `red.d/10-auros-record-bad-boot.sh` |
| `last_fetch_attempt` | `/var/lib/auros/update-agent/last-fetch-attempt` | [SOURCE] `auros-update:28,201` |
| `last_successful_fetch` | `/var/lib/auros/update-agent/last-successful-fetch` | [SOURCE] `auros-update:29,224` |
| `last_error_rc` | `rc=` line of `last-error` only — **not** the `output` block | [SOURCE] `auros-update:212-214`. [JUDGMENT] output is dropped: it is unbounded and could carry a proxy URL with credentials in it |
| `last_green_boot`, `last_green_digest` | written by greenboot's green.d hook | [SOURCE] `green.d/10-auros-record-good-boot.sh` (the last ~15 lines) |
| `last_red_boot`, `last_red_digest` | the newest `--- <ts> ---` block and its `booted_digest=` line in `red-boots.log` | [SOURCE] `red.d/10-auros-record-bad-boot.sh` |
| `policy_pristine` | `cmp /etc/containers/policy.json /usr/etc/containers/policy.json` → true/false | **Outside the "bootc/greenboot only" rule, deliberately. See §7.** |
| `reporter_version` | constant in the script | — |

Derived **on the server**, never on the machine:

- **rolled back** = `last_red_boot` is newer than the previous report's `last_green_boot`, and
  `last_red_digest ≠ booted_digest`. The machine is running, it is green, and the image it tried is
  the one that failed. [JUDGMENT] this is the
  combination the red.d hook was written to make visible ("it restarted a few times and then it was
  fine").
- **image age** = now − `booted_created`. Missing timestamp → **UNKNOWN, shown red**, matching the
  machine-side rule "a canary that cannot see is not allowed to say green" [SOURCE:
  `70-update-freshness.sh`].
- **behind its siblings** = another machine in the same school reports a newer `booted_created`
  for more than 3 days. [JUDGMENT] This catches *one* machine stalling; image age catches *the
  supply* stalling (§2.4). Neither needs a GitHub or GHCR API call — B18's lesson that the fleet path
  must not spend the order path's GitHub budget [SOURCE: BLOCKED.md B18].

Payload is ~600 bytes of JSON. [JUDGMENT] It fits in KV key metadata (1024 bytes — §4).

What is **not** reported, ever: hostname, user names, logged-in state, MAC/serial/DMI, IP address,
installed Flatpaks, file names, location, `/etc/machine-id`. See §5.

---

## 3. How: push, outbound, once a day

**Pull is impossible.** Inbound is rejected except DHCPv6 and mDNS [SOURCE:
`auros-base/hardening/firewalld-zone-auros.xml`], sshd is masked [SOURCE: same file's comment], and
opening a port for this would be the worst trade in the design. So the machine **pushes** one HTTPS
POST outbound.

**Rejected: inferring the fleet from registry pulls.** GHCR shows per-package download counts, not
per-machine identity. It cannot say *which* machine stopped, and a stalled supply looks like a
healthy fleet pulling nothing new. [JUDGMENT]

Machine side, three files in `auros-base/update-agent/`:

- `libexec/auros-fleet-report` — python3 stdlib only (`json`, `urllib.request`), the same convention
  as `json_get` in `auros-update:133-143` ("python3 so we do not depend on jq"). Uses the same
  `AUROS_TEST_ROOT` seam [SOURCE: `auros-update:38-43`].
- `systemd/auros-fleet-report.service` + `.timer`: `OnBootSec=20min`, `OnUnitInactiveSec=24h`,
  `RandomizedDelaySec=2h`. A **separate unit**, not an `ExecStartPost` on the update service, so a
  broken reporter cannot touch updating.
- **The reporter always exits 0.** A failed unit is read by `40-no-new-failed-units.sh` as a
  regression and rolls the machine back [SOURCE: `auros-update:19-22`; the service drop-in's
  comment]. A reporter that rolls machines back when our server is down would be the fleet view
  causing the outage it exists to detect. A failed POST writes `/var/lib/auros/fleet/last-error`
  and nothing else. **This rule gets its own red test** (D34).

[JUDGMENT] Once a day, not four times: nothing on the fleet page needs finer resolution than
"reported in the last 36 hours", and it keeps KV writes inside the free tier (§4).

---

## 4. Where it goes: zero new paid services, and the account question answered honestly

### Option A — GitHub as the backend. **Rejected.**

Every mechanism for a machine to write to GitHub needs a credential **on the machine**, and every
image is public (GHCR public, repos public, D31). A token baked into the image is a public token.
A per-machine token would have to be a PAT or App token with `contents:write`/`issues:write`, and one
extracted from one laptop could read, overwrite or delete **every other school's** reports. It would
also spend the installation budget B18 already shows is contended. The data would be public if the
repo is public (D31); a private repo is free (D36) but does not fix the credential. [JUDGMENT]

### Option B — one more route on the existing Cloudflare Worker + KV. **Recommended.**

The order Worker already exists [SOURCE: `auros-web/worker/`], already declares one KV binding
`AUROS_KV` [SOURCE: `auros-web/wrangler.jsonc:43-44`], and already has the KV rate-limit pattern
[SOURCE: `worker/lib/http.js:88-98`]. Adding `/fleet/*` is new code, not a new service.

**The honest part: that account does not exist.** B14 records no Cloudflare account and no token, and
that creating one is the owner's act [SOURCE: BLOCKED.md B14 item 1]. So this is "no new account
**beyond the one Gate 4 already needs**" — not "no account". If the pilot happens before B14 is done,
the fleet view does not exist at the pilot. **[OWNER] D-2: create the B14 account before the pilot,
or accept H4/H16 open at the pilot.** The account is free (Workers Free plan).

Free-plan KV limits, read 2026-09-20 from developers.cloudflare.com/kv/platform/limits [SOURCE]:
100,000 reads/day, **1,000 writes/day to different keys**, 1 write/sec per key, 1 GB storage,
**1024 bytes of metadata per key**.

The binding constraint is **1,000 writes/day, shared account-wide with `/order-submit`'s rate
limiter** — the same budget-contention shape as B18. Therefore:

- one key per machine: `fleet/<school>/<machine-id>`, summary in **metadata**, so the school page is
  **one `list()` call** that returns every machine's status without N reads;
- the Worker **skips the write** if the stored record is under 20 h old and no field changed (one read,
  zero writes) — so writes/day ≤ number of machines, whatever a machine or an attacker sends;
- **unauthenticated requests never write** (no rate-limit counter for them — `rateLimit()` itself costs
  a write per call [SOURCE: `http.js:96`]); they are refused after one read.

[JUDGMENT] That gives room for ~700 machines at one write each per day alongside orders. Beyond that,
the options are Workers Paid (~$5/month — **[OWNER] D-3, money**) or moving the table to D1 (also on
the free plan, far higher write allowance — **verify the current D1 limits at that time; I did not
read them**). The 180-machine pilot sits well inside.

### Option C — nothing central; a desktop notification only. **Not sufficient alone.**

Closes H4's "someone sees it" for the person at that one machine. Cannot answer H16 ("has the
*fleet* taken the update?"), and tells us nothing. Kept as a possible addition — §6.

---

## 5. Privacy, for a school

- **Machine identity:** a random 128-bit ID generated on first report and stored in
  `/var/lib/auros/fleet/id`. **Not** derived from `/etc/machine-id`, DMI serial or MAC, so it
  correlates with nothing outside this system. Reinstall = new ID (the old one goes stale, §8).
  [JUDGMENT]
- **Human label:** the page shows the first 6 hex characters of the ID. If the school wants asset tags
  ("Room 12 – 04"), the admin types them into the page and they are stored server-side only — the
  machine never learns or sends them. Whether to offer this at all: **[OWNER] D-4** (an asset tag next
  to "rolled back" can become "Mr X's laptop keeps breaking").
- **No IP stored.** The Worker sees `cf-connecting-ip`; the fleet route must not log it or the body.
  `wrangler.jsonc:68` has `"observability": { "enabled": true }`, which records Worker logs on
  Cloudflare's side — **the fleet route must `console.log` nothing that identifies a machine**, and
  **[OWNER] D-5: log retention** on that account applies to this route too.
- **Content is about the OS image, not a person:** digests, timestamps, exit codes, a boolean.
- **This contradicts two published statements, and they must change before the reporter ships:**
  1. `auros-web/src/content/faq/5-do-you-see-my-data.md` last paragraph: the registry fetch *"does
     not tell us, or anybody, what is on the machine."* With this, a machine tells us which image it
     runs and whether it is healthy.
  2. `auros-base/README.md:78` *"No telemetry"*, and `hardening/telemetry.tsv`, whose own rule is *"a
     privacy claim with a quiet exception is a false claim"*. This needs a `unit-keep` row for
     `auros-fleet-report.timer` with its rationale, exactly like `fwupd-refresh.timer`.
  **[OWNER] D-6: what the school is told** — the FAQ wording, and whether it is in the contract.
  **[OWNER] D-7: on by default, or opt-in per recipe.** [JUDGMENT] Recommend on by default with a
  recipe opt-out, **and** the opt-out text saying plainly that with it off, neither we nor they will
  know the machines stopped updating, and key rotation will strand those machines unless someone
  confirms them by hand.

---

## 6. What the school admin sees, and where (D4: not a terminal)

**A web page.** `https://<site>/fleet/<school-view-token>` — server-rendered HTML, no JS needed,
`Referrer-Policy: no-referrer`, `X-Robots-Tag: noindex`, `Cache-Control: no-store`.

One row per machine, sorted worst first:

| State | Meaning | Colour |
|---|---|---|
| **Not reporting** | no report for > 7 days | red |
| **Late** | no report for 36 h – 7 days (weekend, holiday, switched off) | amber |
| **Image too old** | `booted_created` ≥ 21 days old, or missing — same threshold as the machine-side check [SOURCE: `IMAGE_WARN_AFTER_DAYS=21`] | red |
| **Behind** | siblings run a newer image for > 3 days | amber |
| **Rolled back** | last update failed and the machine returned to the previous image | amber |
| **Can't fetch** | `last_error_rc` set and last successful fetch ≥ 14 days [SOURCE: `WARN_AFTER_DAYS=14`] | red |
| **Not enforcing signatures** | `signature_mode ≠ containerPolicy` | red |
| **OK** | none of the above | green |

Headline above the table: **"172 of 180 machines reported in the last 36 hours. 3 need attention."**
The "of 180" is the ordered device count from the order record [SOURCE: `orders.js`/`putOrder` in
`routes/order.js:201`], so a machine that **never** reported is visible as a gap in the count, not
invisible.

**Nobody checks a page daily.** So once a day a Worker Cron Trigger computes the same states and, if
any school has a red row that is new since yesterday, opens or updates **one** deduplicated GitHub
issue in the private repo (D36) — the same mechanism H1 proposes for the nightly [SOURCE:
SYSTEM-REVIEW H1]. At most one GitHub API call per day, so B18's budget is not touched in any real
sense. [JUDGMENT] Cron Triggers are available on the Workers Free plan — **verify when the account
exists**.

Alerting the **school** (email) needs a mail provider account — the Worker's `email.js` uses Resend
and is off until an owner enables it [SOURCE: `worker/lib/email.js` header]. **[OWNER] D-8: do
schools get an alert, or only the page (and us)?** [JUDGMENT] v1: page + us; we contact the school.

Pupil-desktop notification (Option C): **[OWNER] D-9.** [JUDGMENT] not in v1 — a pupil cannot act on
"your image is 23 days old", and a red notification a class learns to dismiss is the same failure as
a motd nobody reads.

---

## 7. Authentication, spoofing and enumeration (B18's lessons applied)

B18 found three things [SOURCE: BLOCKED.md B18]: an unauthenticated endpoint spending a shared budget;
different responses for known vs unknown names, which **enumerates customers**; and a comment claiming
a property the code did not have. Each has a rule here.

**Machine → server (reporting).**
- **Nothing secret goes in the image.** The image is public; anything in it is public. The endpoint
  URL is in `/usr/lib/auros/fleet.conf` (public, fine).
- **Enrollment token, per school, not in the image.** Written into `/var/lib/auros/fleet/enroll` by
  the install-media step. That step is today a checkbox (`GATE5-RUNBOOK.md:28-29`, SYSTEM-REVIEW §2.15,
  H9); the single install-media script H9 proposes is where this line goes. [JUDGMENT] Whether
  bootc-image-builder's ISO path can drop a file into `/var` or needs a kickstart `%post` is
  **unverified** — prove it in the VM job (§10, step 5) before relying on it.
- **First report = registration.** The machine generates a random 256-bit secret, stores it
  `/var/lib/auros/fleet/secret` mode 0600 root, and sends `{id, secret, enroll_token, report}`. The
  Worker checks the enroll token, checks the school is under its cap (ordered count × 1.1, [JUDGMENT]),
  and stores **`sha256(secret)`** only. After that, the enroll token is deleted from the machine.
- **Every later report** sends `{id, secret, report}` over TLS; the Worker compares hashes.
  [JUDGMENT] A hashed bearer secret over TLS is enough: a KV leak exposes no usable secrets (256-bit
  random input), and TLS gives replay protection in transit. Ed25519 signatures would add code on both
  ends and defend against nothing extra in this threat model.
- **What an attacker gets.** From the public image: nothing. From a leaked enroll token: the ability to
  register fake machines into **one** school, up to its cap — visible on the page as extra rows,
  fixed by rotating that school's token (already-registered machines are unaffected; they auth with
  their own secret). From one stolen laptop with root: that laptop's own row. No path reads anything.
- **Students do not have root** — no passwordless sudo [SOURCE: `auros-base/README.md` hardening
  list] — so the secret is not readable from a pupil session.

**Viewer → server (reading).**
- **Capability URL**: `<school-view-token>` is 128 random bits, one per school, rotatable. No login,
  so no new accounts for the school. [JUDGMENT] Its risk is being forwarded; the page shows only
  image health, so a forwarded link leaks "their machines are/aren't patched" — worth knowing, not
  catastrophic. **[OWNER] D-10: capability URL (no accounts) vs. requiring a login provider (a new
  account for someone).**
- **One response for every miss.** Unknown school, wrong token, malformed token, unknown machine,
  wrong secret: **the same 404 body, same headers**. No endpoint answers "does school X exist".
  Tokens are compared in constant time. This is B18's enumeration lesson, and it gets a test that
  asserts byte-identical responses for the five cases.
- **The fleet route never calls GitHub** except the once-daily cron. It cannot starve `/order-submit`.
- **KV keys use the token-derived school ID, not the recipe name**, so a KV listing leak does not map
  to customer names. [JUDGMENT]

**Operator view** (us): `/fleet/ops/<ops-token>` lists all schools. The ops token is a Worker secret,
never in any repo.

---

## 8. Failure modes — a silent machine must go red, never disappear

| Failure | What the page shows | Why |
|---|---|---|
| Machine stops reporting (off, dead, reporter broken, network blocks our host) | Late → **Not reporting** (red) | Records **never expire**: no `expirationTtl` on fleet keys. State is computed at view time from `received_at`. A TTL would make a dead machine vanish, which is the exact failure this exists to prevent. |
| Machine never enrolled (token missing on install media) | Headline gap: "172 of 180" | Expected count comes from the order, not from the reports. |
| We stop publishing (§2.4) | Every machine → **Image too old** at day 21, while all show reports fresh | Image age is independent of fetch success. |
| Our Worker/account down | Machines keep working and keep updating; reports are lost; on recovery everything shows Late until the next daily report | Reporter always exits 0 (§3). [JUDGMENT] No local queue — the next report carries the current state, which is all that matters. |
| Reporter crashes or bug sends garbage | Row goes Late → Not reporting; Worker rejects bodies > 2 KB or failing schema with the same 404 | No verdict is trusted from the machine. |
| Clock wrong on the machine | Server uses its **own** `received_at` for staleness; machine timestamps only for image age | [JUDGMENT] |
| Machine reinstalled | Old ID goes red, new ID appears | Retire needed — see D-11. |
| Machine retired/stolen/sold | Stays red forever | **[OWNER] D-11: retention.** [JUDGMENT] v1: operator deletes on request; school-side "retire" button later. Records for a cancelled customer: deleted after N days — the owner sets N. |
| KV writes exhausted | Worker gets write errors → returns 503; next day recovers | Bounded by one write/machine/day (§4); still alert on it in the daily cron. |
| Heartbeat of the view itself | If the daily cron has not run in 48 h, nothing tells anyone | [JUDGMENT] The cron updates a `fleet/cron-last-run` key; the nightly workflow (which already runs) reads it via the public ops health endpoint and fails red if stale — H1's heartbeat pattern. |

---

## 9. How it supports the signing-key rotation wait (H16)

The rotation README's step 2 becomes a page, and optionally a gate.

**Definition of "rotation-ready" for one machine:**
1. `booted_digest` ∈ the set of digests published with `keyPaths: [old, new]` in `policy.json`. The
   operator records the first such digest per recipe when step 1 ships; any later digest in the
   ledger qualifies. [JUDGMENT] Computed from our own records, not reported by the machine.
2. **`policy_pristine == true`.** This is the one reported field outside `bootc status`/greenboot, and
   it is needed: `policy.json` is installed to **`/etc`**/containers/policy.json [SOURCE:
   `auros-base/build/30-update-agent.sh:315`], and root can edit it [SOURCE: `signing/README.md:322`].
   bootc's `/etc` handling keeps a locally modified file across updates rather than taking the new
   image's copy [JUDGMENT — standard ostree three-way `/etc` merge; **confirm on a VM** in §10 step 6].
   A machine whose `/etc/containers/policy.json` was touched would boot an image "containing both
   keys" while still verifying with only the old one — and step 3 would strand it while the page
   said ready.
3. `signature_mode == containerPolicy`.

**The page** `/fleet/ops/<token>/rotation`: *"171 ready · 6 not yet updated · 3 not reporting (last
seen 12, 40, 95 days ago)."* The not-reporting machines are the dangerous ones: a laptop in a
cupboard over summer will come back after step 3 and be stranded. **[OWNER] D-12: the rotation
rule** — [JUDGMENT] recommend: step 3 is allowed only when every *non-retired* machine is ready,
and a machine is only excluded by an explicit retire (D-11), never by being silent.

**Optional gate** [JUDGMENT, small]: the CI job that ships step 3 (dropping the old key from
`policy.json`) calls the ops rotation endpoint and refuses unless `not_ready == 0`. That turns
"confirmed from the console, not from an assumption" into a mechanical check, the SPEC §3 way.

---

## 10. Sized plan

Sizes: S ≈ half a day, M ≈ 1–2 days, for one agent. [JUDGMENT]

| # | Step | Size | Where it can be proven | Needs |
|---|---|---|---|---|
| 1 | Confirm the bootc 1.16.10 field paths not yet cited in the repo (`image.image.image`, and whether a `rollbackQueued`-type field exists) from `host-v1.schema.json`; record in D25 | S | desk | — |
| 2 | `auros-fleet-report` + service/timer + `run-tests.sh` cases via `AUROS_TEST_ROOT` stubs: each field, missing timestamp, POST failure → exit 0 (red test: make it exit non-zero and watch the test fail, D34), no identifying fields in the body (assert against a denylist: hostname, machine-id, MAC) | M | **CI** (existing `unit-tests.yml` in auros-base) | — |
| 3 | Worker `/fleet/report`, `/fleet/<token>`, `/fleet/ops/...`, cron; `node --test` with a fake KV: registration, cap, write-skip, identical-404 for all five miss cases, no TTL on fleet keys, state computation incl. Not reporting, rotation-ready | M | **CI** (`auros-web/worker`, `npm test`); no Cloudflare account needed | — |
| 4 | `telemetry.tsv` `unit-keep` row; FAQ 5 and README:78 rewrite; CLAIMS.md row | S | CI (honesty gate, `10-hardening.test.sh`) | **D-6, D-7 first** |
| 5 | VM check: boot the image in the check-matrix VM with the reporter pointed at a local Worker (`wrangler dev`/Miniflare, host reachable from QEMU user-net); assert registration, a report arrives, fields match `vm-digest`; stop the reporter, advance the Worker clock → row goes Late then Not reporting; enroll-token-on-install-media path | M | **VM in CI** (the existing matrix workflows) | — |
| 6 | VM rotation rehearsal with **two development keys** (D32 dev key cannot publish, so this is safe): VM-A updates to a two-key image, VM-B is held on the old image; page shows 1 ready / 1 not; gate refuses step 3; then force step 3 and **watch VM-B refuse the update** (the negative proof — D34). Also: hand-edit `/etc/containers/policy.json` on VM-C, update, confirm `policy_pristine=false` and not-ready | M | **VM in CI** | — |
| 7 | Deploy the route; set tokens; school page for the pilot | S | real | **D-1, D-2**, B14's publish permission |
| 8 | First-week check on pilot hardware: every machine appears; switch one off for 8 days → red | S | **hardware** | pilot |

**Steps 1–6 need no hardware, no account and no money.** Only 7–8 do. Total ≈ 5–8 agent-days.

---

## 11. Owner decisions

| ID | Decision | Kind |
|---|---|---|
| **D-1** | Amend SPEC §6E to allow this read-only subset before the first customer | scope |
| **D-2** | Create the B14 Cloudflare account (free) before the pilot, or accept H4/H16 open at the pilot | **account** |
| **D-3** | If the fleet outgrows the KV free write allowance (~700 machines): Workers Paid (~$5/mo) or move to D1 | **money** |
| **D-4** | Offer school-entered asset labels, or IDs only | privacy |
| **D-5** | Worker log retention on that account (observability is on, `wrangler.jsonc:68`) | **data retention** |
| **D-6** | What the school is told: FAQ 5 and README "no telemetry" wording; contract clause | **what the school is told** |
| **D-7** | Reporting on by default with recipe opt-out, or opt-in | **what the school is told** |
| **D-8** | Whether schools get alerts (needs a mail provider = new account) or only the page | **account** |
| **D-9** | Pupil-desktop notification: yes/no | product |
| **D-10** | Capability URL vs. a login for the school page | **account** |
| **D-11** | Retention: how long records live, who can retire a machine, deletion after a customer leaves | **data retention** |
| **D-12** | The rule for rotation step 3 (recommended: every non-retired machine ready; silence never counts as ready) | risk |

---

## 12. What this does not solve

- It notices; it does not fix. A school whose machines stop updating still needs a human to act.
- It depends on the install-media step (H9) to carry the enroll token. Until that script exists, the
  token is a manual step on a checklist — the kind of thing §3 of the review lists.
- It trusts each machine's own `bootc status`. A machine whose root has been compromised can lie.
  That is out of scope for a view whose job is noticing neglect, not detecting intrusion.
