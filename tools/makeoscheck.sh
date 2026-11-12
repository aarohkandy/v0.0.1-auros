#!/bin/sh
# ═══════════════════════════════════════════════════════════════════
#  makeoscheck -- the first seconds of .github/workflows/make-os.yml
#
#    sh tools/makeoscheck.sh check       (reads the environment below, writes
#                                         profile= tag= short= windows= to
#                                         $GITHUB_OUTPUT, or to the screen)
#    sh tools/makeoscheck.sh selftest    (each rule refuses what it exists to
#                                         refuse, with a fake gh)
#
#  A script and not a block of YAML so that it can be run with hostile input.
#  Before an hour of runner it decides:
#    - WHICH PROFILE: the dispatch input, or (a push of a makeos/** branch) the
#      last path segment of the branch name. Both are text a person typed; they
#      only ever arrive here through the environment, never inside a script.
#    - THAT IT IS ONE: the repo's own rule (tools/customize/installer_profile.py:
#      lower-case letters, digits, dashes, 1-32 characters), and a
#      profiles/<name>.profile that says the same profile_id, at this commit.
#    - WHAT IT WILL BE CALLED: the release tag os-<profile>-<7 of the commit>.
#      The `os-` prefix is what keeps it out of everything that looks for an
#      AurOS release (those look for v<version>-<sha7>, apt, e2e-, profiles-...).
#    - THAT NOTHING IS THERE YET. The tag is baked into the installer's piece
#      list. A rerun of the same commit builds new bytes (an image is not
#      bit-for-bit reproducible), and an installer from one build must never
#      fetch pieces from another. So a release or a tag of that name already
#      existing stops the run NOW, with the command that clears it, instead of
#      after the build. The same for the release sources-<tag>, which the run
#      makes before the installer's release: it is the source of THAT build's
#      packages (a new build may hold newer ones), it is never deleted
#      (docs/OWNER-SETUP.md), and sources.yml only verifies one that exists. A
#      commit that has one needs a new commit.
#
#  ENVIRONMENT  EVENT (push | workflow_dispatch)  REF_NAME  PROFILE_IN
#               WINDOWS_IN (true|false)  SHA (40 hex)  REPO (owner/name)
#               GH_TOKEN (for gh)
# ═══════════════════════════════════════════════════════════════════
set -u
cd "$(dirname "$0")/.." || exit 2

fail() { printf '::error::%s\n' "$*" >&2; exit 1; }

check() {
    case "${EVENT:-}" in
        push)              p=${REF_NAME:-}; p=${p##*/} ;;
        workflow_dispatch) p=${PROFILE_IN:-} ;;
        *) fail "this workflow runs for a push of a makeos/... branch or when started by hand, not for '${EVENT:-}'" ;;
    esac
    # (text a person typed: what is echoed back is cut down to harmless characters)
    ps=$(printf '%s' "$p" | tr -cd 'A-Za-z0-9._/ -' | cut -c1-40)
    [ -n "$p" ] || fail "no profile name. Start it with: gh workflow run make-os.yml -f profile=NAME   (or push a branch called makeos/NAME)"
    # The repo's own rule, and the file at this commit. `--` so that a name
    # starting with a dash is a name, not an option.
    out=$(python3 tools/customize/installer_profile.py --output "${TMPDIR:-/tmp}/makeos-check.$$.h" -- "$p" 2>&1) || {
        rm -f "${TMPDIR:-/tmp}/makeos-check.$$.h"
        have=$(cd profiles && ls ./*.profile 2>/dev/null | sed 's|^\./||; s|\.profile$||' | tr '\n' ' ')
        fail "'$ps' cannot be made into an installer: $(printf '%s' "${out#installer profile: }" | tr -cd 'A-Za-z0-9._/ ,;:()'"'"'-' | cut -c1-200). Profiles in this commit: ${have}"
    }
    rm -f "${TMPDIR:-/tmp}/makeos-check.$$.h"
    # (belt and braces: what follows puts the name in a tag and in file names)
    printf '%s' "$p" | grep -Eq '^[a-z0-9]([a-z0-9-]{0,30}[a-z0-9])?$' || fail "'$ps' is not a profile name"
    printf '%s' "${SHA:-}" | grep -Eq '^[0-9a-f]{40}$' || fail "no commit to name the release after"
    short=$(printf '%s' "$SHA" | cut -c1-7)
    tag="os-$p-$short"
    printf '%s' "${REPO:-}" | grep -Eq '^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$' || fail "no repository"

    # A release with this tag? A tag with no release? Anything but a clear
    # "no" is a stop: a gate must not pass on an API error.
    if err=$(gh release view "$tag" -R "$REPO" 2>&1 >/dev/null); then
        fail "the release $tag already exists (an earlier run of this commit). A new build is different bytes, so it must not be mixed into that one. Delete it, then run again: gh release delete $tag -R $REPO --cleanup-tag -y   Or push a new commit."
    fi
    case "$err" in
        *"release not found"*) ;;
        *) fail "could not tell whether the release $tag exists: $err" ;;
    esac
    if err=$(gh release view "sources-$tag" -R "$REPO" 2>&1 >/dev/null); then
        fail "the release sources-$tag already exists (the source code of an earlier build of this commit; a source release is never deleted). A new build may hold other package versions, so it must not be mixed into that one. Push a new commit and run again. If only the publishing failed in a run that is still there, try Re-run failed jobs on that run instead."
    fi
    case "$err" in
        *"release not found"*) ;;
        *) fail "could not tell whether the release sources-$tag exists: $err" ;;
    esac
    if err=$(gh api "repos/$REPO/git/ref/tags/$tag" 2>&1 >/dev/null); then
        fail "the tag $tag already exists without a release. Delete it, then run again: git push https://github.com/$REPO.git :refs/tags/$tag"
    fi
    case "$err" in
        *"Not Found"*|*"404"*) ;;
        *) fail "could not tell whether the tag $tag exists: $err" ;;
    esac

    windows=true; [ "${EVENT}" = workflow_dispatch ] && [ "${WINDOWS_IN:-true}" = false ] && windows=false
    {
        echo "profile=$p"; echo "tag=$tag"; echo "short=$short"; echo "windows=$windows"
    } >> "${GITHUB_OUTPUT:-/dev/stdout}"
    echo "profile $p, commit $short: release $tag (the Windows journey: $windows)"
}

selftest() {
    t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
    mkdir "$t/bin"
    # A gh that answers from $FAKE_RELEASE and $FAKE_SOURCES (the releases os-... and
    # sources-os-...; exists | none | error) and $FAKE_TAG.
    cat > "$t/bin/gh" <<'GH'
#!/bin/sh
case "$1" in
    release) case "$3" in sources-*) r=$FAKE_SOURCES ;; *) r=$FAKE_RELEASE ;; esac
             case "$r" in
                 exists) echo '{}'; exit 0 ;;
                 none)   echo 'release not found' >&2; exit 1 ;;
                 *)      echo 'HTTP 502: Bad Gateway' >&2; exit 1 ;;
             esac ;;
    api)     case "$FAKE_TAG" in
                 exists) echo '{}'; exit 0 ;;
                 none)   echo 'gh: Not Found (HTTP 404)' >&2; exit 1 ;;
                 *)      echo 'HTTP 502: Bad Gateway' >&2; exit 1 ;;
             esac ;;
esac
exit 2
GH
    chmod +x "$t/bin/gh"
    bad=0; sha=0123456789abcdef0123456789abcdef01234567
    # case NAME WANT(0|1) EVENT REF PROFILE WINDOWS RELEASE TAG [EXPECTED-OUTPUT-LINE]
    # (FAKE_SOURCES, the state of sources-<tag>, is "none" unless the case sets it)
    FAKE_SOURCES=none; export FAKE_SOURCES
    run() {
        name=$1; want=$2; ev=$3; ref=$4; pin=$5; win=$6; fr=$7; ft=$8; line=${9:-}
        EVENT=$ev REF_NAME=$ref PROFILE_IN=$pin WINDOWS_IN=$win SHA=$sha REPO=o/r FAKE_RELEASE=$fr FAKE_TAG=$ft \
            GITHUB_OUTPUT=$t/out PATH=$t/bin:$PATH sh "$0" check > "$t/log" 2>&1; got=$?
        [ "$got" = 0 ] || got=1
        ok=1; [ "$got" = "$want" ] || ok=0
        if [ -n "$line" ] && ! grep -qx -- "$line" "$t/out" 2>/dev/null; then ok=0; fi
        # a refused run writes nothing for the next job to trust
        if [ "$want" = 1 ] && [ -s "$t/out" ]; then ok=0; fi
        if [ "$ok" = 1 ]; then printf '    %-62s ok\n' "$name"
        else printf '    %-62s FAIL (exit %s, wanted %s)\n' "$name" "$got" "$want"; sed 's/^/        /' "$t/log" "$t/out" 2>/dev/null; bad=$((bad+1)); fi
        rm -f "$t/out"
    }
    run "a dispatch of a shipped profile"                  0 workflow_dispatch main office "" none none "profile=office"
    run "...gets the tag os-<profile>-<7 of the commit>"   0 workflow_dispatch main school-kiosk true none none "tag=os-school-kiosk-0123456"
    run "...and runs the Windows journey by default"       0 workflow_dispatch main office "" none none "windows=true"
    run "windows=false is the only way to skip it"         0 workflow_dispatch main office false none none "windows=false"
    run "a push of makeos/office builds office"            0 push makeos/office "" "" none none "profile=office"
    run "a push of makeos/a/b/revive builds revive (last segment)" 0 push makeos/a/b/revive "" "" none none "profile=revive"
    run "a push always runs the Windows journey"           0 push makeos/office "" false none none "windows=true"
    for hostile in 'Office' '../x' 'a;b' '' 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' '-h' '--output=x' 'a b' 'nosuchprofile' 'office/../x' '$(id)' 'a`id`' "o'x"; do
        run "dispatch refuses '$hostile'"                  1 workflow_dispatch main "$hostile" "" none none
    done
    run "a push of makeos/ with no name is refused"        1 push makeos/ "" "" none none
    run "a push of makeos/Office is refused"               1 push makeos/Office "" "" none none
    run "a push of makeos/x;y is refused"                  1 push 'makeos/x;y' "" "" none none
    run "another event is refused"                         1 pull_request main office "" none none
    run "a release with that tag already exists: refused"  1 workflow_dispatch main office "" exists none
    run "a tag with that name already exists: refused"     1 workflow_dispatch main office "" none exists
    run "an API error on the release: refused (fails closed)" 1 workflow_dispatch main office "" error none
    run "an API error on the tag: refused (fails closed)"  1 workflow_dispatch main office "" none error
    FAKE_SOURCES=exists
    run "a source release of that name exists: refused"    1 workflow_dispatch main office "" none none
    FAKE_SOURCES=error
    run "an API error on the source release: refused (fails closed)" 1 workflow_dispatch main office "" none none
    FAKE_SOURCES=none
    # the message says what to run
    EVENT=workflow_dispatch PROFILE_IN=office SHA=$sha REPO=o/r FAKE_RELEASE=exists FAKE_TAG=none GITHUB_OUTPUT=$t/o \
        PATH=$t/bin:$PATH sh "$0" check > "$t/log" 2>&1
    if grep -q 'gh release delete os-office-0123456 -R o/r --cleanup-tag -y' "$t/log"; then printf '    %-62s ok\n' "the stop for an existing release names the command that clears it"
    else printf '    %-62s FAIL\n' "the stop for an existing release names the command that clears it"; bad=$((bad+1)); fi
    # a hostile name never reaches a file path: nothing outside profiles/ was read
    EVENT=workflow_dispatch PROFILE_IN='../../etc/passwd' SHA=$sha REPO=o/r FAKE_RELEASE=none FAKE_TAG=none \
        PATH=$t/bin:$PATH sh "$0" check > "$t/log" 2>&1
    if grep -q 'is not lower-case letters' "$t/log"; then printf '    %-62s ok\n' "a path as a name is refused by the repo's own rule"
    else printf '    %-62s FAIL\n' "a path as a name is refused by the repo's own rule"; sed 's/^/        /' "$t/log"; bad=$((bad+1)); fi
    echo "makeoscheck selftest: $([ "$bad" = 0 ] && echo ok || echo "$bad wrong")"
    [ "$bad" = 0 ]
}

case "${1:-}" in
    check)    check ;;
    selftest) selftest ;;
    *) echo "usage: sh tools/makeoscheck.sh check|selftest" >&2; exit 2 ;;
esac
