# What AurOS costs to run, and what breaks first as it grows

Written 2026-10-23 (stream OPS, WP-60). **The limits** are quoted from the providers' pages read that day
(links at the end). **The usage** is a model with stated assumptions, not a measurement: nothing has been
installed yet, so every number marked "model" must be replaced by the field numbers (WP-73) after wave 1.
**The Anthropic price per call** is stream CL's estimate in `docs/CLOUD.md` section 3, "pending" its first
live evaluation. Change an assumption below and redo the arithmetic; nothing else here depends on it.

## The answer in one table

Monthly cost to the owner, by number of computers with AurOS installed (model; Wishes used by 30 % of them
on a day, 3 calls each, $0.0004 a call):

| Installed | GitHub | Cloudflare | Anthropic (Wishes) | The domain | Humans |
|---|---|---|---|---|---|
| 1,000 | $0 | $0 | about **$11** | about $1 (a $12 a year name) | read every ticket |
| 10,000 | $0 | $0, watch the daily request count (about 58k of 100k) | about **$110** | about $1 | 200 to 500 emails in a bad first month |
| 100,000 | $0 *if* the downloads stay unthrottled; see below | about **$7** (Workers Paid $5 plus overage) | about **$1,100** | about $1 | needs paid support (v1.0, WP-69) |

Anthropic is the only cost that grows in step with use, and it has a ceiling you set: `DAILY_BUDGET_USD`
(deployed: **$5**, so at most about $155 a month) and the Console workspace limit behind it. At the model's usage
$5 a day is **about 14,000 installed computers**; beyond that Wishes answers "resting" until midnight UTC, by
design. Raise it on purpose, never by accident.

One-time costs the owner decides on (`docs/PLAN-END-TO-END.md` section 5): the used PCs for the lab about
$2,000 to $3,000 (decision 11); a lawyer's review and E&O insurance, and an outside security review (decisions 9,
20): **no quote yet, ask before relying on any figure**.

## The model

| Assumption | Value | Why |
|---|---|---|
| Image download per new install | 1.5 GB (16 pieces of 90 MiB, plus the 35 MB installer) | `auros-pieces` is 1.46 GB |
| Requests to `get.<domain>` per new install | 20 | installer, SHA256SUMS, pieces.txt, 16 pieces |
| Requests to `updates.<domain>` per installed computer per day | 3 | InRelease, Packages, now and then a package |
| Website views per new install | 3, about 12 requests and 0.8 MB each | people look more than they install |
| Share of installs using Wishes on a given day | 30 % | a guess; the opt-in field numbers will say |
| Wishes calls per active day | 3 (two wishes, one question) | `docs/CLOUD.md` section 3 |
| Cost per call | $0.0004 | the top of CLOUD.md's $0.0001 to $0.0006 |
| Everyone arrives in the same month | yes | worst case for downloads |

| Per month | 1,000 | 10,000 | 100,000 |
|---|---|---|---|
| Image downloads | 1.5 TB | 15 TB | 150 TB |
| Website bandwidth | 2.4 GB | 24 GB | 240 GB |
| Cloudflare Worker requests per day (updates + downloads + site + Wishes) | 5,800 | 57,700 | 576,700 |
| ... of which Wishes, and the rows it writes per day | 900, 3,600 | 9,000, 36,000 | 90,000, 360,000 |
| Anthropic calls per day, dollars per day | 900, $0.36 | 9,000, $3.60 | 90,000, $36 |
| Anthropic, a month | $11 | $108 | $1,080 |

## What breaks first, in order, and what to do

1. **The Anthropic daily cap, at about 14,000 installs** (model). By design Wishes rests until midnight.
   Raise `DAILY_BUDGET_USD` in `cloud/wrangler.toml` and the Console workspace limit together; ask Anthropic
   for the next rate tier when `rate_limit` shows in the logs (`docs/CLOUD.md` section 2: 1,000 requests a minute
   at the first tier; 90,000 calls a day averages 1 a second, so 10 a second at a busy hour is inside it).
2. **Cloudflare Workers Free: 100,000 requests a day for the whole account, at about 15,000 to 25,000 installs.**
   Every installed computer asks `updates.<domain>` through the edge Worker a few times a day, the website's
   every file goes through it, and the Wishes Worker counts against the same account. Past the limit the
   Worker errors (Cloudflare's own page says "Free: 100,000/day"). Fix, in order of effort: the Workers Paid
   plan, **$5 a month for 10 million requests, then $0.30 a million** (100,000 installs is about 17 million a
   month, about $7); or move the update redirects to Cloudflare Redirect Rules, which are not Worker requests.
   Also on the Free plan, the Durable Objects limits: 5 million rows read and **100,000 rows written** a day
   (the Wishes ledger writes about 4 rows a wish: fine to about 25,000 wishes a day).
3. **GitHub Pages: a *soft* 100 GB a month, and a 1 GB site.** About 40,000 new installs a month at 0.8 MB a
   view. The edge Worker now asks Cloudflare to cache the site for 5 minutes, which cuts what Pages serves to a
   few requests per file per 5 minutes. **A condition to remember:** GitHub's Pages terms do not allow a
   business site or a software-as-a-service site on the free hosting. A free, open-source download page is fine;
   the day AurOS sells anything (v1.0), the site must move (Cloudflare can host it for the same price).
4. **GitHub Releases bandwidth.** GitHub's documentation says there is no limit on a release's total size or on
   bandwidth, 2 GiB a file, 1,000 files a release. That is what the whole download path stands on, and it is a
   policy, not a contract: 150 TB in a month for 100,000 installs is the kind of traffic that gets a polite
   email. Remove the dependency before it matters (WP-60): copy each release's pieces to **Cloudflare R2**
   (10 GB storage free, 1 million writes and 10 million reads a month free, **no egress fee**; 3 releases is
   4.5 GB; 100,000 installs is about 2 million reads a month). The addresses already allow it: `get.<domain>`
   decides where a piece comes from, and `pieces.txt` may list several `base` lines.
5. **Cloudflare's own terms for the free proxy:** serving large non-HTML files (installers, images) *through* the
   free CDN proxy is limited. The edge Worker **redirects** to GitHub (and later R2) instead of passing the bytes;
   keep it that way.
6. **People.** At 1,000 installs the owner reads every email. A model guess of 2 % to 5 % writing in during the
   first month is 20 to 50, 200 to 500, 2,000 to 5,000 emails; at ten minutes each that is 3 to 8, 33 to 83, 330
   to 830 hours. Nothing in this repository changes that arithmetic; the second responder (WP-83) and paid
   support (WP-69) are the answer, and the waves (WP-74) are what keeps the first month small.

## Things that cost nothing and must stay that way

- **The repository must stay public.** Public repositories get unlimited Actions minutes and KVM runners;
  a private one would bill by the minute and has no KVM for image builds.
- **The monitor** is 96 runs a day of about 20 seconds: free. **The edge, the monitor, the drill** use no
  paid service. No third-party pager is used until the owner picks one (a free phone push is the GitHub app).
- **Cloudflare Registrar** sells the domain at cost; the Workers custom domains, routes, DNS and the cache are
  free on every plan.

## Sources (read 2026-10-23)

- Cloudflare Workers limits: https://developers.cloudflare.com/workers/platform/limits/
- Cloudflare Workers and Durable Objects pricing: https://developers.cloudflare.com/workers/platform/pricing/
- Cloudflare R2 pricing: https://developers.cloudflare.com/r2/pricing/
- GitHub Pages limits: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits
- GitHub release limits: https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases
- Anthropic price per call and rate tiers: `docs/CLOUD.md` sections 2 and 3 (stream CL; Console numbers to be re-checked
  at https://platform.claude.com/docs/en/api/rate-limits before raising a cap)
