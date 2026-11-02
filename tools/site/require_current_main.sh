#!/bin/bash
# Run inside the serialized cloud job immediately before each mutation step.
set -euo pipefail
[[ ${GITHUB_REPOSITORY:-} =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ && ${GITHUB_SHA:-} =~ ^[a-f0-9]{40}$ ]] || { echo "::error::Invalid cloud source identity"; exit 1; }
current=$(timeout 25 gh api "repos/$GITHUB_REPOSITORY/git/ref/heads/main" --jq '.object.sha')
[[ $current =~ ^[a-f0-9]{40}$ && $current = "$GITHUB_SHA" ]] || { echo "::error::Cloud source is no longer current main; run cloud on current main before publishing"; exit 1; }
