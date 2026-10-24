#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
#  aptgate -- may this workflow_run publish AurOS's signed update channel?
#  (.github/workflows/aptrepo.yml, docs/security/UPDATES.md)
#
#  The update-signing key is root on every AurOS computer. A run of
#  `build` or `release` may start the publish only if a real promote
#  happened in THAT run, and the commit's SUBJECT (its first line) says
#  [latest]. Not a body that merely mentions it (three accidental
#  triggers: runs 40042981951, 40042987246, 40046665287, the last of
#  which loaded the key on a runner), and not a run whose promote job was
#  skipped, cancelled or failed.
#
#    bash tools/aptgate.sh decide     env: MSG (the commit message),
#                                          RUN_ID (the triggering run),
#                                          REPO (owner/name)
#         prints   publish=true|false   and   reason=...
#         exit 0   decided (either way)
#         exit 1   could not decide (an API error, bad input): it still
#                  prints publish=false, and the caller must stop.
#    bash tools/aptgate.sh selftest   every refusal made to fire, and
#                                     each rule broken to see it caught
#
#  Fail closed: the only path to publish=true is the `grep` at the end of
#  `decide`, reached after every check. There is no `|| true` on the API
#  call. GH names the GitHub CLI (a test stands in a stub for it).
# ═══════════════════════════════════════════════════════════════════
set -euo pipefail

GH=${GH:-gh}

deny()  { printf 'publish=false\nreason=%s\n' "$1"; }
allow() { printf 'publish=true\nreason=%s\n' "$1"; }

decide() {
    local subj jobs
    subj=${MSG:-}; subj=${subj%%$'\n'*}
    case "$subj" in
        *'[latest]'*) ;;
        *) deny "[latest] is not in the commit subject"; return 0 ;;
    esac
    if [ -z "${RUN_ID:-}" ] || [ -z "${REPO:-}" ]; then deny "no run id or repository given"; return 1; fi
    case "$RUN_ID" in *[!0-9]*) deny "the run id is not a number"; return 1 ;; esac
    case "$REPO" in */*) ;; *) deny "the repository is not owner/name"; return 1 ;; esac
    # No `|| true`: an API error is "cannot decide", never "go ahead".
    if ! jobs=$("$GH" api --paginate "repos/$REPO/actions/runs/$RUN_ID/jobs?per_page=100" \
                 --jq '.jobs[] | "\(.name)|\(.conclusion)"' 2>/dev/null); then
        deny "could not read the jobs of run $RUN_ID (API error)"; return 1
    fi
    if [ -z "$jobs" ]; then deny "run $RUN_ID has no jobs"; return 0; fi
    # The promote job of the run: "release / promote" when build.yml calls
    # release.yml, "promote" when release.yml ran alone. Exactly that name,
    # exactly "success": skipped, cancelled, failure and look-alikes do not count.
    if grep -qxE '(release / )?promote\|success' <<<"$jobs"; then
        allow "the promote job of run $RUN_ID concluded success"
    else
        deny "the promote job of run $RUN_ID did not conclude success"
    fi
}

# ── selftest ───────────────────────────────────────────────────────
selftest() {
    local T bad=0 n=0 out rc
    T=$(mktemp -d); trap "rm -rf '$T'" EXIT
    cat > "$T/gh" <<'STUB'
#!/bin/sh
# A stand-in for `gh api ... --jq`: what the jobs of a run look like.
case "${STUB_MODE:-}" in
    called_ok)   printf '%s\n' 'tests / unit|success' 'release / which|success' 'release / publish|success' 'release / promote|success' 'release / pages|failure' ;;
    alone_ok)    printf '%s\n' 'which|success' 'publish|success' 'promote|success' 'pages|success' ;;
    skipped)     printf '%s\n' 'tests / unit|success' 'release|skipped' ;;
    promote_skipped) printf '%s\n' 'release / which|success' 'release / promote|skipped' ;;
    cancelled)   printf '%s\n' 'release / which|success' 'release / promote|cancelled' ;;
    failure)     printf '%s\n' 'release / which|success' 'release / promote|failure' ;;
    lookalike)   printf '%s\n' 'release / promote-docs|success' 'xpromote|success' 'promote2|success' 'release / promote |success' ;;
    empty)       : ;;
    apierror)    echo 'gh: Server Error (HTTP 502)' >&2; exit 1 ;;
    *)           echo "stub: no mode" >&2; exit 2 ;;
esac
STUB
    chmod +x "$T/gh"
    # name | message | run id | repo | stub mode | want publish | want exit
    local NL=$'\n'
    check() { # name msg runid repo mode want_publish want_rc
        n=$((n+1))
        set +e
        out=$(GH="$T/gh" STUB_MODE="$5" MSG="$2" RUN_ID="$3" REPO="$4" decide 2>&1); rc=$?
        set -e
        if [ "$rc" = "$7" ] && printf '%s\n' "$out" | grep -qx "publish=$6"; then
            printf '    %-72s ok\n' "$1"
        else
            printf '    %-72s FAIL\n' "$1"; printf '      want publish=%s exit %s; got exit %s:\n' "$6" "$7" "$rc"; printf '%s\n' "$out" | sed 's/^/        /'
            bad=$((bad+1))
        fi
    }
    echo
    echo "== the gate fires only for a real promote"
    check "subject [latest] + promote succeeded (called from build)"          "[release] [latest] ship it" 123 o/r called_ok   true  0
    check "subject [latest] + promote succeeded (release.yml alone)"          "[publish] [latest]"          123 o/r alone_ok    true  0
    echo "== it does not fire"
    check "[latest] only in the BODY"                                         "docs: notes${NL}${NL}the [latest] word is explained here" 123 o/r called_ok false 0
    check "[latest] only in the body, even though a promote succeeded"        "wip${NL}[latest]"            123 o/r alone_ok    false 0
    check "no [latest] anywhere"                                              "fix a thing"                 123 o/r called_ok   false 0
    check "a release run with every job skipped"                              "[latest] x"                  123 o/r skipped     false 0
    check "promote skipped"                                                   "[latest] x"                  123 o/r promote_skipped false 0
    check "promote cancelled"                                                 "[latest] x"                  123 o/r cancelled   false 0
    check "promote failed"                                                    "[latest] x"                  123 o/r failure     false 0
    check "look-alike job names (promote-docs, xpromote, promote2, trailing space)" "[latest] x"            123 o/r lookalike   false 0
    check "the run has no jobs"                                               "[latest] x"                  123 o/r empty       false 0
    echo "== it fails closed"
    check "an API error: publish=false and exit 1"                            "[latest] x"                  123 o/r apierror    false 1
    check "no run id: publish=false and exit 1"                               "[latest] x"                  ""  o/r called_ok   false 1
    check "run id that is not a number"                                       "[latest] x"                  "12;id" o/r called_ok false 1
    check "no repository"                                                     "[latest] x"                  123 ""  called_ok   false 1
    check "an empty commit message"                                           ""                            123 o/r called_ok   false 0
    echo
    if [ "$bad" = 0 ]; then echo "aptgate selftest: $n cases, all ok"; else echo "aptgate selftest: $bad of $n WRONG"; return 1; fi
}

case "${1:-}" in
    decide)   decide ;;
    selftest) selftest ;;
    *) sed -n '2,29p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
