#!/bin/bash
# Waits for build run $1 (which includes its release/e2e jobs), then "harden of build run $1".
R=aarohkandy/v0.0.1-auros; B=$1
st() { gh run view "$1" -R $R --json status,conclusion -q '.status+" "+.conclusion' 2>/dev/null; }
while [ "$(st $B | cut -d' ' -f1)" != completed ]; do sleep 150; done
echo "build $B: $(st $B)"
gh run view $B -R $R --json jobs -q '.jobs[]|select(.conclusion!="success" and .conclusion!="skipped")|[.name,.conclusion]|@tsv'
echo "--- (non-success jobs above)"
HAR=""; for i in $(seq 1 20); do
  HAR=$(gh run list -R $R -w harden -L 30 --json databaseId,displayTitle -q ".[]|select(.displayTitle==\"harden of build run $B\")|.databaseId" | head -1)
  [ -n "$HAR" ] && break; sleep 60; done
echo "harden run: ${HAR:-none}"
[ -n "$HAR" ] && { while [ "$(st $HAR | cut -d' ' -f1)" != completed ]; do sleep 120; done; echo "harden $HAR: $(st $HAR)"; }
