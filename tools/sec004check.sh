#!/bin/sh
# sec004check.sh [OWNER/REPO] -- is the update-signing key where SEC-004 says it must be?
#
# Run on the owner's Mac (gh logged in as the repository admin; a runner cannot
# read secrets' names or the environment's rules, and no runner is ever given
# the key). Reads settings only; prints ok / FAIL per line; exit 1 if any FAIL.
#   - AUROS_APT_SIGNING_KEY and APT_ENV_CANARY are NOT repository secrets
#   - both ARE secrets of the environment apt-signing
#   - apt-signing: deployment branches limited to the one rule "main", and a
#     required reviewer
#   - the default branch cannot be deleted or force-pushed (an active ruleset)
# docs/OWNER-SETUP.md 4a-lock says what to click; tools/sec004checktest.sh shows
# this goes red for each missing step and green for all of them. The runtime half
# is .github/workflows/canary-signing.yml (a decoy secret, never the key).
# GH=... names another gh (the test's).
GH=${GH:-gh}
R=${1:-$($GH repo view --json nameWithOwner --jq .nameWithOwner)} || { echo "sec004check: which repository?"; exit 2; }
KEY=AUROS_APT_SIGNING_KEY
DECOY=APT_ENV_CANARY
fail=0
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }
has() { printf '%s\n' "$1" | awk -v n="$2" '$1 == n { f = 1 } END { exit !f }'; }

repo_secrets=$($GH secret list -R "$R" 2>&1) || { echo "sec004check: cannot list secrets of $R: $repo_secrets"; exit 2; }
env_secrets=$($GH secret list --env apt-signing -R "$R" 2>/dev/null) || env_secrets=""
for n in $KEY $DECOY; do
    has "$repo_secrets" $n && bad "$n is still a REPOSITORY secret: any workflow on any branch can read it (gh secret delete $n -R $R)" || ok "$n is not a repository secret"
    has "$env_secrets" $n && ok "$n is a secret of the apt-signing environment" || bad "$n is not in the apt-signing environment (gh secret set $n --env apt-signing -R $R)"
done

e=$($GH api "repos/$R/environments/apt-signing" 2>/dev/null) || e=""
if [ -z "$e" ]; then
    bad "the environment apt-signing does not exist"
else
    pol=$($GH api "repos/$R/environments/apt-signing/deployment-branch-policies" --jq '[.branch_policies[] | "\(.type // "branch"):\(.name)"] | sort | join(",")' 2>/dev/null) || pol=""
    custom=$(printf '%s' "$e" | jq -r '.deployment_branch_policy.custom_branch_policies // false' 2>/dev/null)
    if [ "$custom" = true ] && [ "$pol" = "branch:main" ]; then ok "deployment branches: Selected branches, exactly the rule main"
    else bad "deployment branches are '${pol:-unrestricted}' (custom policies: ${custom:-?}); want exactly: main"; fi
    rev=$(printf '%s' "$e" | jq -r '[.protection_rules[]? | select(.type == "required_reviewers") | .reviewers[]?.reviewer.login] | join(",")' 2>/dev/null)
    if [ -n "$rev" ]; then ok "required reviewer(s): $rev"; else bad "no required reviewer on apt-signing"; fi
fi

# SEC-004 step 5: main cannot be deleted or force-pushed. An active branch ruleset that
# includes the default branch and has the rules `deletion` and `non_fast_forward`.
okr=0
for id in $($GH api "repos/$R/rulesets" --jq '.[] | select(.enforcement == "active" and .target == "branch") | .id' 2>/dev/null); do
    rs=$($GH api "repos/$R/rulesets/$id" 2>/dev/null) || continue
    if printf '%s' "$rs" | jq -e '(.conditions.ref_name.include // []) | any(. == "~DEFAULT_BRANCH" or . == "refs/heads/main")' >/dev/null 2>&1 &&
       printf '%s' "$rs" | jq -e '([.rules[].type] | index("deletion")) != null and ([.rules[].type] | index("non_fast_forward")) != null' >/dev/null 2>&1; then okr=1; fi
done
[ $okr = 1 ] && ok "main: an active ruleset blocks deletion and force-pushes" || bad "main has no active ruleset that blocks deletion and force-pushes"

if [ $fail = 0 ]; then echo "sec004check: ok ($R)"; else echo "sec004check: $fail thing(s) to do ($R): docs/OWNER-SETUP.md 4a-lock"; fi
[ $fail = 0 ]
