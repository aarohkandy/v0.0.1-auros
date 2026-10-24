#!/bin/sh
# hardengate.sh RUN-ID -- did the hardening boot test pass on THAT build run's image?
#
# A [latest] (the full release the website serves) is refused unless
# harden.yml's `boot` job succeeded on the image of build run RUN-ID
# (docs/RELEASE.md). Which image a harden run tested is in its title, not
# its head_sha (a workflow_run's head_sha is the tip of main): harden.yml's
# run-name is "harden of build run <id>". tools/releasegate.py harden judges
# the rows; tools/hardengatetest.sh proves it refuses and passes, on a fake gh.
# Needs gh, GH_TOKEN, GITHUB_REPOSITORY. Exit 0 = passed, 1 = refused.
set -eu
rid=${1:?usage: hardengate.sh RUN-ID}
repo=${GITHUB_REPOSITORY:?GITHUB_REPOSITORY}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# A build run id is digits: it is put into the jq text below.
case $rid in ''|*[!0-9]*) echo "hardengate: not a run id: $rid" >&2; exit 2 ;; esac
# Only harden runs from after the build began.
created=$(gh api "repos/$repo/actions/runs/$rid" --jq .created_at)
gh api -X GET --paginate "repos/$repo/actions/workflows/harden.yml/runs" -f per_page=100 -f "created=>=$created" \
    --jq ".workflow_runs[] | select(.display_title == \"harden of build run $rid\") | \"\\(.id)|\\(.created_at)|\\(.event)\"" > "$tmp/runs"
: > "$tmp/rows"
while IFS='|' read -r id at ev; do
    [ -n "$id" ] || continue
    boot=$(gh api "repos/$repo/actions/runs/$id/jobs?filter=latest&per_page=100" \
        --jq '[.jobs[] | select(.name == "boot") | (.conclusion // .status)] | first // "none"')
    echo "$at|$ev|harden of build run $rid|$boot" >> "$tmp/rows"
done < "$tmp/runs"
cat "$tmp/rows"
python3 -I "$(dirname "$0")/releasegate.py" harden "$tmp/rows" "$rid"
