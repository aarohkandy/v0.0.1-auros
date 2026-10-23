# Drills: what was rehearsed, and how long it took

`drill.yml` makes a throwaway prerelease, breaks it the way INCIDENTS.md playbooks 1 and 3a
describe, and times: the monitor going red, the alert issue being open and assigned to the owner,
the repair going green, the issue closing. Run it (`gh workflow run drill.yml --ref main`) before
opening a wave and after changing the monitor or the playbooks. Copy the run's table here.

Not yet drilled, and why: pausing and resuming the public download (needs the owner's domain and
the edge Worker); the second responder halting without the owner (needs the person named, WP-83);
the rescue stick (WP-82); a key rotation (needs the owner's offline key); the Wishes budget switch
(would change the live service).

## Drills

### 2026-10-23, run 39927811756 (GitHub-hosted runner, throwaway prerelease `drill-39927811756-...`, removed at the end)

| what was broken | monitor red after | alert issue open and assigned after | repaired and green after | assigned to |
|---|---|---|---|---|
| A. the installer swapped for another file (playbooks 1 and 2) | 42 s | 54 s | 13 s | the owner |
| B. the signed index removed, then put back (playbook 3a) | 53 s | 65 s | 51 s | the owner |

Repair target: under 30 minutes. Met by a wide margin; the slow part in real life is the 15-minute cron
before the monitor looks (add up to 15 minutes to the first columns). Each issue closed itself when its check went
green. No leftover release or open drill issue afterwards (checked).

What the first three runs of the drill taught (all fixed, all in git history): GitHub's issue list lags a few
seconds behind a creation or a close, so a drill (or anything) that reads it right after writing must poll
(runs 1 and 2 failed on it and left issues 1 and 2 open until closed by hand); the monitor's own self-test
took minutes in CI because it waited between retries.

Not drilled with a stopwatch: the monitor's cron itself (it has run only as a push and a manual dispatch so far;
GitHub starts a new schedule within the hour).

## Real incidents

(none yet)
