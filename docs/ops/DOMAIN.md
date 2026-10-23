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
