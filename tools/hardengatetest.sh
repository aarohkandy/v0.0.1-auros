#!/bin/sh
# hardengatetest.sh -- tools/hardengate.sh refuses a red, absent or unfinished
# hardening boot of a build's image and passes a green one: run against a fake
# `gh` that answers from canned JSON (through the real jq filters).
set -u
here=$(cd "$(dirname "$0")" && pwd)
command -v jq >/dev/null || { echo "hardengatetest: needs jq"; exit 2; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/bin"
# The fake gh: finds --jq EXPR and the path, prints jq -r EXPR over $FAKE/<kind>.json.
cat > "$tmp/bin/gh" <<'GH'
#!/bin/sh
expr=""; path=""
while [ $# -gt 0 ]; do
    case $1 in --jq) expr=$2; shift ;; repos/*) path=$1 ;; esac
    shift
done
case $path in
    */actions/workflows/harden.yml/runs*) f=$FAKE/runs.json ;;
    */actions/runs/*/jobs*) id=${path#*/actions/runs/}; id=${id%%/*}; f=$FAKE/jobs-$id.json ;;
    */actions/runs/*) f=$FAKE/build.json ;;
    *) echo "fake gh: unexpected $path" >&2; exit 1 ;;
esac
jq -r "$expr" < "$f"
GH
chmod +x "$tmp/bin/gh"
fail=0
# case NAME EXPECTED-EXIT BOOT... : harden runs 1.. of build run 900, newest last; BOOT = conclusion or status
run() {
    name=$1; want=$2; shift 2
    FAKE=$tmp/f; rm -rf "$FAKE"; mkdir "$FAKE"; export FAKE
    echo '{"created_at":"2026-10-24T00:00:00Z"}' > "$FAKE/build.json"
    n=0; runs=""
    for b in "$@"; do
        n=$((n + 1))
        runs="$runs{\"id\":$n,\"created_at\":\"2026-10-24T0$n:00:00Z\",\"event\":\"workflow_run\",\"display_title\":\"harden of build run 900\"},"
        case $b in
            success|failure|cancelled|skipped) c="\"$b\"" ;; *) c=null ;;
        esac
        echo "{\"jobs\":[{\"name\":\"which\",\"conclusion\":\"success\",\"status\":\"completed\"},{\"name\":\"boot\",\"conclusion\":$c,\"status\":\"$b\"}]}" > "$FAKE/jobs-$n.json"
    done
    # a run of another build, green: must never count
    runs="$runs{\"id\":99,\"created_at\":\"2026-10-24T09:00:00Z\",\"event\":\"workflow_run\",\"display_title\":\"harden of build run 901\"}"
    echo "{\"workflow_runs\":[$runs]}" > "$FAKE/runs.json"
    echo '{"jobs":[{"name":"boot","conclusion":"success","status":"completed"}]}' > "$FAKE/jobs-99.json"
    PATH=$tmp/bin:$PATH GITHUB_REPOSITORY=o/r sh "$here/hardengate.sh" 900 >"$tmp/out" 2>&1; got=$?
    if [ "$got" = "$want" ]; then printf '    %-58s ok\n' "$name"; else printf '    %-58s FAIL (exit %s, wanted %s)\n' "$name" "$got" "$want"; sed 's/^/        /' "$tmp/out"; fail=$((fail + 1)); fi
}
run "a green boot of the build passes" 0 success
run "a red boot of the build is refused" 1 failure
run "no harden run of the build is refused (only another build's)" 1
run "a boot still running is refused" 1 in_progress
run "a skipped boot is refused" 1 skipped
run "a cancelled boot is refused" 1 cancelled
run "red, then a green re-run, passes" 0 failure success
run "green, then a red re-run, is refused" 1 success failure
run "a red boot is not hidden by a later cancelled one" 1 failure cancelled
if sh "$here/hardengate.sh" 9x >/dev/null 2>&1; then echo "    a run id that is not digits is refused  FAIL"; fail=$((fail + 1)); else echo "    a run id that is not digits is refused  ok"; fi
echo "hardengatetest: $([ $fail = 0 ] && echo ok || echo "$fail wrong")"
[ $fail = 0 ]
