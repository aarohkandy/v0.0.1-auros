# The Wishes cloud service

> Owner: "if this many people message me at the same time, how am I going to
> manage that?" — You don't. There is no server to look after. Cloudflare runs
> as many copies of the service as there are people asking, and the only real
> limit is how many requests a minute Anthropic accepts from your account,
> which you raise by moving up a tier. The service protects you from cost and
> abuse by itself.

Live address: **https://auros-wishes.aarohkandy.workers.dev** (deployed by
`.github/workflows/cloud.yml`). The API is `docs/AI.md` §5; the wish
schema both sides compile is `docs/wish-v1.schema.json`; the code is `cloud/`.

## 1. How it works

```
her computer (aurwish, the only client)
   │  HTTPS, Bearer token
   ▼
Cloudflare Worker "auros-wishes"   cloud/src/app.js  (any number of copies, worldwide)
   │  1 call before, 1 after          │  1 call
   ▼                                  ▼
Durable Object "ledger"            Anthropic Messages API
cloud/src/ledger.js                claude-haiku-5-5, structured output,
every counter, in one place:       cached system prompt, max_tokens 400/300
tokens, daily quotas, budget
pools, calls in flight
```

1. **Register** (once per computer, again every 90 days, silently): the
   computer proves about 2 s of work (hashcash) and gets an opaque token.
   Only 3 registrations a day per home network (IPv4 /24, IPv6 /64).
2. **A wish**: the Worker checks the token first (strangers get 401 before
   anything is read), then the body (≤ 8 KB, wish ≤ 300 characters), then
   asks the ledger: is the token alive, under today's quota, is there money in
   its budget pool and a free slot? Only then is Claude called. Claude must
   answer in the wish schema (constrained decoding); the answer is checked
   again (schema, the computer's own list of triggers/actions/sensors, the
   free-text rules) and anything wrong becomes `{kind:"cannot"}`. The ledger
   is told the real cost and frees the slot.
3. **Her computer checks everything again** (`docs/AI.md` §7.1). The service
   is a cost filter, not the safety layer: a stolen key or a fooled model
   cannot make a harmful wish, because the engine refuses it.

Why these pieces: Workers scale to zero and to millions without servers; one
Durable Object gives exact counters and a global limit with no database to
run; Haiku-class with a cached prompt costs a hundredth of a cent per wish.

## 2. Scale: what happens when many people ask at once

The real limit is Anthropic's rate limit for the model, per organization
(platform.claude.com/docs/en/api/rate-limits, October 2026, Claude Haiku 5.5):

| Anthropic tier | Requests / minute | ≈ wishes / second | ≈ wishes / day if spread evenly | `MAX_INFLIGHT` to set |
|---|---|---|---|---|
| Start (monthly cap $500) | 1,000 | 16 | 1.4 million | 40 (deployed) |
| Build ($1,000) | 5,000 | 83 | 7 million | 150 |
| Scale ($200,000) | 10,000 | 166 | 14 million | 300 |

New organizations may start in an "Evaluation" tier below Start until they
have some history. Cloudflare is not the limit: the Worker scales by itself,
and the one Durable Object does about a thousand small operations a second
(each wish uses two), far above even the Scale tier.

**Load test** (`npm run load` in `cloud/`: the real handler behind a real HTTP
server, Claude replaced by a mock that answers in 0.6–1.4 s and enforces a
requests-per-minute limit like Anthropic's; run on Linux, 2026-10-23):

| scenario | answered | told "resting" | answered in (p50 / p99) | "resting" in (p50) | calls to Claude (refused by its limit) |
|---|---|---|---|---|---|
| 1,000 people press Wish in the same second, Start tier, deployed limits | 44 | 956 | 1.9 s / 3.6 s | 0.6 s | 75 (31) |
| the same burst with no slot limit | 70 | 930 | 2.3 s / 4.4 s | 2.7 s | 2,915 (2,845) |
| 1,000 people over one minute (a busy evening), Start tier | **1,000** | 0 | 1.0 s / 1.4 s | – | 1,000 (0) |
| 1,000 at once, Build tier, 150 slots | 150 | 850 | 1.4 s / 2.6 s | 0.4 s | 205 (55) |

What this means: a thousand people in the *same second* cannot all be served
by any Start-tier account; the service answers as many as Anthropic allows and
tells the rest, within a second, "Wishes are resting for a little while", with
a `retry_after` of 30–60 s chosen at random so the second try is spread out.
There is no automatic retry (`docs/AI.md` §7.9); her next press after that
time goes through. The slot limit matters: without it the same burst hammers
Anthropic 2,900 times and everyone waits longer for the same answer. Anthropic
429/529 errors are retried twice with backoff inside a 15 s deadline (the
computer gives up at 20 s). A thousand people spread over a minute are all
answered in about a second.

**The plan as usage grows**: watch the "rate_limit" and "busy" reasons in the
logs. When they appear daily, request the next Anthropic tier (Console → Rate
limits → Request tier increase) and raise `MAX_INFLIGHT` in
`cloud/wrangler.toml` to the table's value. A queue inside the service is not
built: a wait longer than her 20 s patience helps nobody, and a fast "resting"
plus a randomized retry time is the queue.

## 3. What it costs

**Per call** (Claude Haiku 5.5: $0.10 per million input tokens, $0.01 cached,
$0.125 cache write, $0.50 output). The wish system prompt is about 1,500
tokens and the Ask prompt (with the help pages) about 4,200, both cached; her
request is about 150 tokens; the answer 60–150 tokens.

| | estimate | measured (live eval) |
|---|---|---|
| one wish | about $0.0001 ($0.0004 when the 5-minute cache has to be rewritten) | *pending: first live run after the key fix* |
| one question (Ask AurOS) | about $0.0001–0.0006 | *pending* |
| refused before Claude (no token, too long, quota) | $0 | $0 |

**Per 10,000 people a day**, each making 2 wishes and asking 1 question: about
30,000 calls ≈ **$3–5 a day, $100–150 a month** in Anthropic charges. The
daily budget (`DAILY_BUDGET_USD`, $5 deployed) caps it whatever happens: when
a pool is empty, Wishes rests until midnight UTC. Worst case is the cap: $5 ×
31 = $155 a month, plus the workspace spend limit in the Anthropic Console as
the hard backstop. The contract's check (§7.10): quota × cost × installs
stays under the cap: 10,000 installs × (20 + 20) calls × $0.0004 = $160 a day
in the impossible case that every install uses its whole quota; the $5 pools
stop that at $5.

**Cloudflare**: the Free plan covers about 5,000 people a day (100,000
requests and 100,000 stored-row writes a day; each wish writes about 4 rows).
Beyond that, or if Workers ever reports "exceeded CPU" (the Free plan allows
10 ms of CPU per request; not yet measured on the wish path), the Paid plan is
$5 a month for 10 million requests.

## 4. Privacy, in plain words

- **What leaves her computer**: the words of her wish or question, her
  language, which kinds of wishes her computer can do, and which sensors it
  has. Never her files, names of her programs, contacts, location, or what she
  types elsewhere. Her computer replaces long numbers and e-mail addresses in
  her words before sending (§7.12).
- **Who sees it**: Cloudflare (runs the service, in the US and worldwide) and
  Anthropic (the model, in the US). Anthropic keeps requests for up to 30 days
  (up to 2 years if its safety systems flag one), unless zero data retention is
  agreed (`docs/OWNER-SETUP.md` §7). AurOS's own service keeps none of her
  words: they are used to answer and dropped.
- **What the service stores**: for each computer's token, the day it first
  registered and whether it has had a wish (90 days, then gone); how many
  requests each token and each home network made today, under a scrambled key
  that is unreadable after 48 hours because the day's key is deleted; how much
  was spent today. No words, no addresses, no names, no install ids.
- **What it logs**: one line per request: which endpoint, the result, how
  long it took, tokens and cost, and why it failed if it did. Never the words,
  a token, an id or an address. A test fails the build if that ever changes
  (`cloud/test/adversarial.test.js`, and a live check of the deployed logs in
  `cloud.yml`). Cloudflare's own per-request logs, which would hold
  addresses, are switched off (`cloud/wrangler.toml`).
- **Forgetting**: "Forget everything" or turning Wishes off calls
  `/v1/forget`: her token and its counters are deleted at once. Anthropic's
  copy expires on Anthropic's schedule; the service cannot delete it.

Draft consent text for `docs/AI.md` §1.4 (the lead and W2 own the wording on
screen; the owner fills the brackets):

> **Wishes uses the internet.** When you make a wish or ask a question, the
> words you type or say are sent to AurOS's wish service, run by [owner's
> name] ([contact e-mail]), to work out what you mean. The service uses two
> companies in the United States: Cloudflare, which runs it, and Anthropic,
> whose AI model reads your words. Nothing else on your computer is sent.
> AurOS keeps none of your words. Anthropic may keep them for up to 30 days,
> or up to 2 years if its safety checks flag them. You can turn Wishes off at
> any time, and "Forget everything" deletes what AurOS's service holds about
> your computer. You have the right to ask what is kept and to have it
> deleted: [privacy policy link]. **Yes, use Wishes** / **No, thanks**.

## 5. Runbook

Where to look: Cloudflare dashboard → Workers & Pages → `auros-wishes` →
**Logs** (one JSON line per request; filter on `reason`, `status`,
`limited`). Anthropic Console → Usage and Billing for the money side.

| Symptom | What the logs say | What to do |
|---|---|---|
| Everyone gets "resting" all day | `reason:"budget"` | Today's pool is empty. Wait for midnight UTC, or raise `DAILY_BUDGET_USD` in `cloud/wrangler.toml` and push (deploys in 2 minutes). The 50 % alert line (`ev:"budget_half"`, and a push to `WISHES_ALERT_URL` if set) comes first. |
| "resting", and the Console shows the limit reached | `reason:"spend_limit"` | The Anthropic workspace's monthly limit is hit. Raise it in the Console (Settings → Workspaces → AurOS Wishes → Limits). |
| "resting" for minutes, now and then | `reason:"rate_limit"` or `"busy"` | Anthropic's per-minute limit or the slot limit. If daily: request the next tier, then raise `MAX_INFLIGHT` (§2). |
| Anthropic is down | `reason:"overloaded"`, `"upstream_5xx"`, `"timeout"`, `"network"` | Nothing to do; status.anthropic.com. Wishes she already made keep working (they run on her computer). |
| Every request "resting" since a change | `reason:"auth"`, `"bad_request"`, `"not_found"` with `detail` | The key or request is refused; `detail` is Anthropic's message. A key not scoped to a workspace: `docs/OWNER-SETUP.md` §7. |
| Abuse spike: registrations or quota hits pile up, the "new" pool drains | `limited:"register"`, `limited:"quota"` with a `tag` | Ban the noisiest tags: add them to `BANNED` in `wrangler.toml`, push. Make registering harder: `POW_BITS = "24"` (4× the work). Lower `QUOTA_NEW`. The pools already stop new tokens from spending established tokens' money. |
| Rotate the Anthropic key | – | Console: new key in the AurOS Wishes workspace → GitHub secret `ANTHROPIC_API` → Actions → cloud → Run workflow → delete the old key. No downtime. |
| Rotate the token-signing secret (a leak, or start everyone fresh) | – | `cd cloud && npx wrangler secret delete TOKEN_SECRET`, then run the cloud workflow: it makes a new one. Every computer gets 401 once and registers again silently (a home with more than 3 computers finishes over two days). |
| Rotate the Cloudflare token | – | Cloudflare: new token (§7 of OWNER-SETUP) → GitHub secret → revoke the old one. |
| Switch Wishes off entirely | – | Cloudflare dashboard → `auros-wishes` → Settings → Domains & Routes → disable workers.dev. Her computers say "Wishes is not available" and everything else is unaffected. |

Changing the model is a code change (`MODEL` and `PRICE` in
`cloud/src/app.js`) followed by `npm run live-eval` with a key: it is a safety
decision, not a setting.

## 6. How AurOS finds the service

`docs/AI.md` §7.11: the address comes from a signed package, not from the
image. Proposal (owners: stream S5 for the channel, W1 for the reader):

- A tiny package `auros-wishes-config` in AurOS's signed update channel
  installs `/usr/share/auros/wishes.conf` with one line:
  `url=https://auros-wishes.aarohkandy.workers.dev`.
- `aurwish` reads that file; no file or no line means Wishes is "not
  available yet" (§1.6) and nothing else changes.
- Moving the service (another Cloudflare account, a custom domain) = publish
  a new version of that one package. Every AurOS computer gets it with its
  normal updates; no image rebuild. The image build installs the package's
  current version, so a fresh install works at once.
- Until the package exists, the build can write the same file from a
  repository variable `AUROS_WISHES_URL`, the way the repository address is
  baked in today (`tools/repo.sh`).

## 7. Knobs (`cloud/wrangler.toml` [vars]; push to change)

| Name | Deployed | Meaning |
|---|---|---|
| `DAILY_BUDGET_USD` | 5 | the day's Anthropic budget, split 70 % established / 20 % new / 10 % Ask |
| `QUOTA_ESTABLISHED`, `QUOTA_NEW` | 20, 3 | wishes (and, separately, questions) per token per day |
| `ESTABLISH_DAYS` | 7 | how old a token must be (plus one wish received) to be established |
| `REGISTER_PER_NET` | 3 | registrations per /24 or /64 per day |
| `MAX_INFLIGHT` | 40 | Claude calls at once (§2 table) |
| `POW_BITS` | 22 | registration work: +1 doubles it |
| `BANNED` | "" | token tags (from `limited:"quota"` log lines), comma-separated |

Secrets (never in the repository): `ANTHROPIC_API_KEY` (from the GitHub
secret), `TOKEN_SECRET` (made once by the workflow; nobody sees it),
`ALERT_URL` (optional), and the variable `ANTHROPIC_WORKSPACE_ID` (optional).

## 8. Checking it

```sh
cd cloud && npm ci
npm test              # 30 tests, mocked Claude: contract, behaviour, hostile model, logs
npm run load          # the load test above (run it on Linux: macOS caps the listen backlog at 128)
ANTHROPIC_API_KEY=... npm run live-eval    # the real model on the acceptance + adversarial sets
node test/smoke.mjs https://auros-wishes.aarohkandy.workers.dev   # end to end against the live service
```

`cloud.yml` runs all four on every change to `cloud/`, the schema or the help
pages; results are in the run's summary.
