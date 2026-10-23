# Stream CL — the Wishes cloud service (state, 2026-10-23)

**Goal.** The service behind Wishes and "Ask AurOS" with no server to manage:
`docs/AI.md` §5 (CL owns it) as amended by the binding §7. Files owned:
`cloud/**`, `.github/workflows/cloud.yml`, `docs/CLOUD.md`,
`docs/wish-v1.schema.json` (shared with W1), AI.md §5, OWNER-SETUP §7.

## Live

- **https://auros-wishes.aarohkandy.workers.dev** (Cloudflare Worker
  `auros-wishes` + Durable Object `Ledger`), deployed by `cloud.yml` on every
  push to `cloud/**`, the schema or the help pages.
- Proven live (runs 39906177468, 39906475542): health; no token / forged
  token → 401; register refuses a missing proof of work and accepts a real one
  (22 bits, 4.3 M hashes, 6.8 s in Node on a runner); forget → token gone
  (401); the service's captured log lines (11) hold no token, install id or
  wish words.
- **Real model proven live** (run 39923358865, 2026-10-23 08:16Z, after the
  owner's workspace-scoped key): 26/26 acceptance + adversarial cases right;
  potato and compass examples exact; cost **$0.000089 per wish**, $0.000082 per
  question; 1.2 s per wish, 2.3 s per question (service-side); no body, token,
  id or Authorization header in the 12 captured live log lines.
- **Open: Worker CPU vs the Free plan.** Right after a deploy (cold isolates)
  the tail shows /v1/wish p50 11 ms (max 13), /v1/ask max 33 ms, /v1/forget 2,
  other routes ~1 ms; every outcome "ok" (no error 1102). The Free plan allows
  10 ms per request, so either the account is on the Paid plan or cold-start
  numbers overstate steady state. Owner question: is the Cloudflare account on
  Workers Paid ($5/month)? If not, expect 1102 errors for Ask under load and
  recommend Paid (CLOUD.md §3). Warm numbers: read a later run's summary.

## Done (commits on main)

- 7c2672f `docs/wish-v1.schema.json`: the shared schema, contract v1 (no
  title/summary/install, site/app_name/theme_name, 60-emoji allowlist with
  🥔, floors, speak_time/pointer_size/do_not_disturb).
- 2b1e569 the service: `cloud/src/app.js` (handler), `ledger.js` (one DO:
  tokens, HMAC day-keyed quotas, pools 70/20/10, slots, 48 h day keys, sweep),
  `validate.js` (request + wish checks, API schema transform that keeps
  enums), `prompts.js` (cached system prompts; her words escaped as data),
  `worker.js`, `build-help.mjs` (help pages → `src/help.gen.js`, generated,
  not committed). 30 tests.
- a8135d8 `cloud.yml` + `test/load.mjs`, `test/live-eval.mjs`,
  `test/smoke.mjs`.
- d2923f5 failure detail in logs (Anthropic's message), tail JSON parser
  `test/tail-logs.mjs`, live eval always runs.
- 3499c27 AI.md §5 rewritten as built.
- a587cda token checked before the body (lead's finding); optional
  workspace id.
- f58f672 docs/CLOUD.md, OWNER-SETUP §7, this file.
- 0d31261 / e8e568f 21-bit proof of work (measured: ~2 s on a 2013 laptop),
  outcome + CPU reporting in the log check.
- 7eaf9b0 / 88ca38c / 330af0a the schema probe and the fix: the API refuses a
  grammar for the whole wish schema ("compiled grammar is too large" from about
  22 alternatives; vocabulary = 26 shapes; one generic action object is "too
  complex"), so the wish is JSON text in `{kind, missing, wish_json}` and is
  validated by `checkWish`. 31 tests.

## Decisions and interpretations (tell the lead if you change them)

- Model `claude-haiku-5-5`, thinking disabled (fixed max_tokens 400/300 must
  all go to the answer), constant in code, not a setting. Opus/Sonnet would
  also fail the $0.01 estimate rule. Measure with `npm run live-eval`.
- "a token with one confirmed wish" = the service has returned that token a
  wish (the cloud never sees her Yes; the device is untrusted anyway).
- No server-side refusal fallback (Haiku has none; a refusal is a `cannot`).
- No queue: a fast 503 with a random 30–60 s `retry_after` (CLOUD.md §2).
- `missing` enum: unclear, question, compass, tilt, light, install, files,
  messages, money, private, security, other.
- `/v1/ask` returns `help_page_id: null` when no page fits; its wish must be
  `when: now`.
- Register limit keyed per IPv4 /24 (and /64): a home with > 3 computers
  registers over two days.

## Next steps, in order

1. Quality, not safety: Spanish "pon la pantalla más brillante" came back as
   cannot/unclear (with an alt wish). Add a few non-English examples to
   `prompts.js` if more cases like it show up; re-run `npm run live-eval`.
2. Check Worker CPU (Cloudflare dashboard → auros-wishes → Metrics → CPU
   time). Over ~8 ms on the wish path → recommend the $5 Paid plan.
3. With W1: the device client (`src/aurwish`) per AI.md §5 "Tokens and the
   device flow"; with S5: the `auros-wishes-config` package (CLOUD.md §6).
4. With the lead/W2: the consent text (draft in CLOUD.md §4) into AI.md §1.4.

## Traps

- `wrangler tail --format json` prints pretty multi-line JSON: parse with
  `test/tail-logs.mjs`, not line by line.
- macOS caps the listen backlog at 128: run `npm run load` on Linux
  (homebase: `~/work/auros/cl/`), or bursts show as connection errors.
- `src/help.gen.js` is generated; `npm test` and wrangler's build make it.
- Secrets reach wrangler on stdin only; never `set -x` in cloud.yml.
- The Worker's TOKEN_SECRET is created once by the workflow; regenerating it
  logs every computer out (they re-register silently).

## Verify

`cd cloud && npm ci && npm test` (30 pass); `npm run load` on Linux;
`node test/smoke.mjs https://auros-wishes.aarohkandy.workers.dev` (needs no
secret; uses one of this network's 3 daily registrations).
