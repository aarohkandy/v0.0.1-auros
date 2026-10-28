# The monitor, in one page

`.github/workflows/monitor.yml` runs `tools/ops/monitor.py` every 15 minutes (GitHub's cron is best
effort: expect it a few minutes late sometimes, and note that GitHub switches scheduled workflows off
after 60 days without any repository activity; a push brings them back).

| Check | Passes when | Pages when |
|---|---|---|
| `site` | the website answers 200 and is the AurOS page | listed in `docs/ops/live.txt` |
| `download` | "Get AurOS" delivers an installer-sized file (hourly: the whole file hashes to SHA256SUMS' line) | listed |
| `pieces` | every piece in `pieces.txt` answers a one-byte Range request with 206 | listed |
| `channel` | `InRelease` verifies against the committed keyring and has more than 3 days of Valid-Until, if it has one | listed |
| `wishes` | `/v1/health` says ok and a token-less `/v1/wish` is refused with 401 | listed |
| `gate` | the edge Worker (once the domain exists) answers, and downloads are not paused | the domain is set |
| `flathub` | Flathub's repository answers | never (information) |

Pages = one GitHub issue per failing check, labelled `monitor`, assigned to the owner and to the
repository variable `AUROS_SECOND_RESPONDER` if set, closed by the monitor when the check passes. GitHub
notifies assignees by email and, with the GitHub mobile app and notifications on, by a phone push:
**the owner should install the app and turn on notifications for this repository** (`docs/OWNER-SETUP.md`
section 8). No third-party pager is used until the owner picks one.

A failing check that is **not** in `docs/ops/live.txt` is "pending": shown in every run, never an issue.
Add the word the day the thing is really live. `waves.py wave 1` refuses until all five are listed.

A check that cannot reach its target reports `answered nothing (no answer)` plus the reason; it does not
crash, and a check that raises is reported as failed with the exception. Every check is tested down four
ways (timeout, 404, empty answer, dropped connection): `python3 -I tools/ops/monitor.py --selftest`.

What it does **not** see yet: Anthropic spend (the Worker's own daily budget and the Console's workspace limit
are the control; the half-budget alert is `WISHES_ALERT_URL`), the share of Wishes answered "resting", the
field outcome numbers (WP-73), security.txt (WP-37), Valid-Until on the real channel (it has none).
