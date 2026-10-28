# Opening the public link in waves (WP-74): off until the owner opens wave 1

**State today:** nothing here is switched on. The edge Worker (`infra/edge`) is deployed only after the
owner sets `AUROS_DOMAIN` and `AUROS_OPS_TOKEN` (`docs/ops/DOMAIN.md`), and it starts **closed** (wave 0).

| Wave | Who may download per UTC day | Opens when (the owner decides, after reading the numbers) |
|---|---|---|
| 0 | nobody (a plain "not open to everyone yet" page) | the default |
| 1 | about 25 | `docs/LAUNCH-READINESS.md` all green, the outside review (WP-84) has no critical or high finding open for wave 2 and beyond, a named second responder (WP-83), the drill done |
| 2 | 100 | a week of wave 1 inside the stop rules |
| 3 | 300 | a week of wave 2 inside them |
| 4 | no cap | a week of wave 3 inside them |

```sh
export AUROS_DOMAIN=<domain>
OPS_TOKEN=<owner token> python3 -I tools/ops/waves.py wave 1     # refuses while docs/ops/live.txt misses a check
python3 -I tools/ops/waves.py state                               # wave, cap, today's count, paused?
```

**Stop rules** (decision 21): any one pauses downloads by itself, within one monitor run (15 minutes):

1. any suspected data loss reported (one);
2. failed after Windows was shrunk, above 2% of the computers whose Windows was shrunk;
3. put Windows back, above 15% of finished installs (only counted from 5 finished installs: a rate of 3 is noise).

`tools/ops/waves.py stop-rules` evaluates them; `monitor.yml` runs it every 15 minutes once the domain,
`AUROS_OPS_TOKEN` and the repository variable `AUROS_OUTCOMES_URL` exist. Numbers it cannot read keep the
gate shut ("the field numbers cannot be read"); an outcomes address that does not answer is a red run, not a
pause. **Resuming is the owner's alone** (`waves.py resume`); the second responder's token can only pause.

**What the outcomes address must serve** (stream CL builds it on the WP-39/73 report Worker; this is the
contract the rules read). `GET` with `Authorization: Bearer <OPS_TOKEN>`:

```json
{"suspected_data_loss": 0, "shrunk": 12, "failed_after_shrink": 0, "completed": 11, "putback": 1}
```
Counts for the trailing 7 days, non-negative integers; `failed_after_shrink` is at most `shrunk`, `putback`
at most `completed`. No identifier, no address.

**When the door is shut** she sees one calm page: "AurOS is full for today. Come back tomorrow." (or "not open to
everyone yet", or "paused for a little while ... your computer is not affected"). Installs under way keep
fetching their pieces: those are never counted, capped or paused.

**The first week** the owner reads the numbers every day and answers the same questions: how many came,
how many finished, how many put Windows back and why, anything in the Sev-1 lane. Decide the next wave on
data, in writing, in the Progress log.
