#!/bin/sh
# sec004checktest.sh -- tools/sec004check.sh is green only when every SEC-004 step
# is done and red for each one missing: run against a fake `gh` answering from
# canned JSON (through the real jq filters).
set -u
here=$(cd "$(dirname "$0")" && pwd)
command -v jq >/dev/null || { echo "sec004checktest: needs jq"; exit 2; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/bin"
cat > "$tmp/bin/gh" <<'GH'
#!/bin/sh
# Fake gh: `secret list [--env E]`, `api PATH [--jq X]` from $FAKE.
if [ "$1" = secret ]; then
    case " $* " in *" --env apt-signing "*) cat "$FAKE/env-secrets" ;; *) cat "$FAKE/repo-secrets" ;; esac
    exit 0
fi
[ "$1" = api ] || exit 1
shift; path=""; expr="."
while [ $# -gt 0 ]; do case $1 in --jq) expr=$2; shift ;; repos/*) path=$1 ;; esac; shift; done
case $path in
    */environments/apt-signing/deployment-branch-policies) f=$FAKE/policies.json ;;
    */environments/apt-signing) [ -f "$FAKE/env.json" ] || exit 1; f=$FAKE/env.json ;;
    */rulesets/*) f=$FAKE/ruleset.json ;;
    */rulesets) f=$FAKE/rulesets.json ;;
    *) exit 1 ;;
esac
jq -r "$expr" < "$f"
GH
chmod +x "$tmp/bin/gh"
fail=0
# setup KEY-IN-REPO DECOY-IN-REPO KEY-IN-ENV DECOY-IN-ENV POLICIES REVIEWER RULESET
#   POLICIES: main | none | "main other"   REVIEWER: yes|no   RULESET: both|nodelete|none
mk() {
    F=$tmp/f; rm -rf "$F"; mkdir "$F"; export FAKE=$F
    : > "$F/repo-secrets"; : > "$F/env-secrets"
    [ "$1" = yes ] && echo "AUROS_APT_SIGNING_KEY	2026-10-24" >> "$F/repo-secrets"
    [ "$2" = yes ] && echo "APT_ENV_CANARY	2026-10-24" >> "$F/repo-secrets"
    [ "$3" = yes ] && echo "AUROS_APT_SIGNING_KEY	2026-10-24" >> "$F/env-secrets"
    [ "$4" = yes ] && echo "APT_ENV_CANARY	2026-10-24" >> "$F/env-secrets"
    echo "ANTHROPIC_API	2026-10-23" >> "$F/repo-secrets"
    case $5 in
        main)     echo '{"branch_policies":[{"name":"main","type":"branch"}]}' > "$F/policies.json"; cust=true ;;
        none)     echo '{"branch_policies":[]}' > "$F/policies.json"; cust=false ;;
        *)        echo '{"branch_policies":[{"name":"main","type":"branch"},{"name":"*","type":"branch"}]}' > "$F/policies.json"; cust=true ;;
    esac
    if [ "$6" = yes ]; then rules='[{"type":"required_reviewers","reviewers":[{"type":"User","reviewer":{"login":"aarohkandy"}}]}]'; else rules='[]'; fi
    echo "{\"name\":\"apt-signing\",\"protection_rules\":$rules,\"deployment_branch_policy\":{\"protected_branches\":false,\"custom_branch_policies\":$cust}}" > "$F/env.json"
    case $7 in
        both)     echo '[{"id":7,"enforcement":"active","target":"branch"}]' > "$F/rulesets.json"
                  echo '{"conditions":{"ref_name":{"include":["~DEFAULT_BRANCH"]}},"rules":[{"type":"deletion"},{"type":"non_fast_forward"}]}' > "$F/ruleset.json" ;;
        nodelete) echo '[{"id":7,"enforcement":"active","target":"branch"}]' > "$F/rulesets.json"
                  echo '{"conditions":{"ref_name":{"include":["~DEFAULT_BRANCH"]}},"rules":[{"type":"non_fast_forward"}]}' > "$F/ruleset.json" ;;
        disabled) echo '[{"id":7,"enforcement":"disabled","target":"branch"}]' > "$F/rulesets.json" ;;
        *)        echo '[]' > "$F/rulesets.json" ;;
    esac
}
t() { # name want-exit setup-args...
    name=$1; want=$2; shift 2
    mk "$@"
    PATH=$tmp/bin:$PATH GH=gh sh "$here/sec004check.sh" o/r >"$tmp/out" 2>&1; got=$?
    if [ "$got" = "$want" ]; then printf '    %-62s ok\n' "$name"; else printf '    %-62s FAIL (exit %s, wanted %s)\n' "$name" "$got" "$want"; sed 's/^/        /' "$tmp/out"; fail=$((fail + 1)); fi
}
t "everything done: green"                                         0 no  no  yes yes main yes both
t "the key still a repository secret: red"                         1 yes no  yes yes main yes both
t "the key never moved into the environment: red"                  1 no  no  no  yes main yes both
t "the decoy still a repository secret: red"                       1 no  yes yes yes main yes both
t "the decoy missing from the environment: red"                    1 no  no  yes no  main yes both
t "no deployment-branch rule: red"                                 1 no  no  yes yes none yes both
t "main plus another rule (not main only): red"                    1 no  no  yes yes more yes both
t "no required reviewer: red"                                      1 no  no  yes yes main no  both
t "main can still be deleted (no deletion rule): red"              1 no  no  yes yes main yes nodelete
t "a disabled ruleset does not count: red"                         1 no  no  yes yes main yes disabled
t "no ruleset at all: red"                                         1 no  no  yes yes main yes none
t "today's state (nothing done): red"                              1 yes no  no  no  none no  none
echo "sec004checktest: $([ $fail = 0 ] && echo ok || echo "$fail wrong")"
[ $fail = 0 ]
