# The owner's domain (WP-15): one variable, and every address follows it

**Why.** Every address baked into an installed computer stays there for
good: the update channel, where the installer fetches the image, the help
pages the wizard and staging name. Today they are all GitHub's
(`github.com/<owner>/<repo>/releases/...`, `<owner>.github.io/<repo>/`). If
the repository is renamed, suspended or moved, every computer installed
from it is stranded. So before the first real install (PLAN-END-TO-END §3,
rule 1), every one of those addresses becomes the owner's own name, and
Cloudflare passes each request on to GitHub (for now).

**How.** One repository variable, `AUROS_DOMAIN` (for example `auros.app`).
`tools/repo.sh` reads it, and everything that bakes an address asks
`tools/repo.sh`:

| What | Without `AUROS_DOMAIN` (today) | With it | Asked by |
|---|---|---|---|
| the website (`site`, `host`) | `https://<owner>.github.io/<repo>/` | `https://<domain>/` | build/aurbridge (the wizard's help link), tools/site/stamp.sh |
| the update channel (`apt`) | `https://github.com/<owner>/<repo>/releases/download/apt/` | `https://updates.<domain>/apt/` | build/aptchannel (the image's `auros.sources`) |
| the image pieces (`download`) | `https://github.com/<owner>/<repo>/releases/download` | `https://get.<domain>/download` | build.yml (`base` in pieces.txt, baked into the exe) |
| the installer (`get`) | `https://github.com/<owner>/<repo>/releases/latest/download/AurOS-Setup.exe` | `https://get.<domain>/` | the website's button (stream D, when it switches) |

The Cloudflare side is `infra/edge` (one Worker, `auros-edge`), deployed by
`.github/workflows/edge.yml` only when the variable is set.

`tools/ops/addrtest.sh` scans a built image's AurOS files, the staging
initramfs and the exe for `github.com`, `github.io`, `githubusercontent.com`
and `workers.dev`. Without the variable it only reports; with it, any hit
fails the build's tests.

## What the owner does (about 20 minutes, once; decision 1)

Nothing below changes anything for a person until step 5, and nothing is built from the new name until step 6.

1. **Buy the domain** at Cloudflare (dash.cloudflare.com -> Domain Registration -> Register: sold at cost, and the
   domain lands in the same account, so Cloudflare already knows it). Short, easy to say aloud, nothing that looks
   like a bank. This is the name written on the printable guide, so choose it as if it were on her fridge.
2. **Let the Cloudflare token touch the domain** (the one in the `CLOUDFLARE_API_TOKEN` secret only edits Workers):
   dash.cloudflare.com/profile/api-tokens -> that token -> Edit -> add **Zone -> Workers Routes -> Edit** and
   **Zone -> DNS -> Edit**, for the new domain only. Save. (`docs/OWNER-SETUP.md` 7c lists the original permissions.)
3. **Two secrets and one variable**, in a Terminal on the Mac where `gh` is logged in. The first line makes the owner's
   gate token, stores it in the repository and puts a copy on the clipboard: paste it into the password manager, it
   is shown nowhere else.
   ```sh
   T=$(openssl rand -hex 24); printf %s "$T" | gh secret set AUROS_OPS_TOKEN -R aarohkandy/v0.0.1-auros && printf %s "$T" | pbcopy; unset T
   gh variable set AUROS_DOMAIN -b <the domain> -R aarohkandy/v0.0.1-auros
   ```
   For the second responder (halt and pause only, never publish), later: the same with `AUROS_HALT_TOKEN`, and the
   value goes to that person only.
4. **GitHub on the phone**: install the GitHub app and turn notifications on for this repository (Watch -> Custom ->
   Issues). The monitor's alerts are issues assigned to the owner; this is how they reach a phone.
5. **Run `edge`** (Actions -> edge -> Run workflow). It publishes `get.`, `updates.`, `status.` and `www.` and the
   website on the domain, all passing on to GitHub, and **closed** (wave 0: the Get AurOS button shows
   "not open to everyone yet"). `https://get.<domain>/_ops/state` should answer with `"wave":0`.
6. **Rebuild from the new name.** Anything built before the variable was set points at GitHub for good. Push a
   commit whose subject says `[release]`. The `addrtest` step in `tests.yml` fails the build if a GitHub or
   workers.dev address is left in the image, the staging environment or the exe. **Nobody installs from a
   release built before this** (PLAN-END-TO-END section 3, rule 1).
7. Tell the agents "the domain is in": the Wishes address moves to `wish.<domain>` (stream CL: a route in
   `cloud/wrangler.toml`, then the `wishes.conf` package in the update channel), and `docs/ops/live.txt` grows
   as each thing goes live.

## What each stream still has to do for WP-15 (not done by OPS)

- **CL**: `cloud/wrangler.toml` drops `workers_dev = true` for a route `wish.<domain>`; `aurwish` reads its address
  from the signed `auros-wishes-config` package (`docs/CLOUD.md` section 6); the report Worker serves the outcome
  numbers in the shape `docs/ops/LAUNCH.md` gives.
- **A2/B**: `pieces.txt` allowing several `base` lines (the parser in `src/aurbridge/phases.c` reads one);
  a sim getimage where base 1 answers 503, 429 or a bad Content-Range and base 2 completes.
- **S5**: `auros.sources` with a `mirror+file:` list and GitHub as the fallback; an updatetest "moved" case where the
  primary answers 404 and the update still arrives.
  (`build/aptchannel` and `updatepolicytest.sh` already accept `updates.<domain>`.)
- **M**: `docs/MOVING.md` gets a line for the domain: the move to another GitHub repository then changes only the
  `GH_REPO` that edge.yml renders.
