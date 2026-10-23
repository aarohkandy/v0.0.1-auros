#!/bin/sh
# sh tools/repotest.sh — nothing names the repository by hand.
#
# On 2026-10-23 the project moved from ComputerDude771/auros-from-scratch
# (branch claude/brave-hypatia-x72g1m, worked on in a container under
# /home/user) to its own repository. What builds, tests, installs or
# publishes AurOS asks tools/repo.sh (or ${{ github.repository }}), so
# the next move edits nothing but the clone address (docs/MOVING.md).
# Fails when:
#   1. the old owner/name, its Pages host, a claude/* branch or a
#      container path appears outside docs/ (HANDOFF.md and START-HERE.md
#      are handoff records and may tell the history);
#   2. the website, stamped for another repository, still names this one
#      or the old one (or the stamp wrote nothing);
#   3. tools/repo.sh answers wrongly for a repository it is told, or
#      accepts something that is not an owner/name.
set -eu
cd "$(dirname "$0")/.."
fail=0
bad() { echo "repotest: $*"; fail=1; }

old='computerdude771|auros-from-scratch|claude/[a-z]+-[a-z]+-[a-z0-9]{6}|claude/\*\*|/home/user/|/tmp/claude-0|/root/\.claude'

# The pattern itself: catches every old name and not the new ones, so
# check 1 cannot pass because the pattern broke.
for s in ComputerDude771/auros-from-scratch https://computerdude771.github.io/auros-from-scratch/ \
         origin/claude/brave-hypatia-x72g1m 'claude/**' /home/user/x /tmp/claude-0/x /root/.claude/x; do
    printf '%s\n' "$s" | grep -qiE "$old" || bad "the pattern misses $s"
done
printf '%s\n' aarohkandy/v0.0.1-auros origin/main /.claude/ /home/username/ | grep -iE "$old" &&
    bad "the pattern catches a name that is not old"

# 1. The tree. ponytail: the pinned addresses of files published only in
# the old repository (windows.yml, e2e.yml env) are allowed until stream
# E moves them to this repository's releases; delete the second grep then.
hits=$(git grep -n -I -i -E "$old" -- . ':!docs' ':!HANDOFF.md' ':!START-HERE.md' ':!tools/repotest.sh' |
       grep -v -E '^\.github/workflows/(windows|e2e)\.yml:[0-9]+:  (PUBLISHED|KNOWN_BAD|IMAGE_PIECES|INSTALLER)_URL: ' || true)
[ -z "$hits" ] || bad "the old repository, branch or container is named outside docs/:
$hits"

# 2. A move: the site published from someone else's copy.
here=$(sh tools/repo.sh) && host=$(sh tools/repo.sh host) || { bad "tools/repo.sh has no answer here"; exit 1; }
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
AUROS_REPO=Some-One/their-copy sh tools/site/stamp.sh website "$t/site" > /dev/null || bad "stamp.sh failed"
left=$(grep -rnIiF -e "$here" -e "${host%/}" -e computerdude771 -e auros-from-scratch "$t/site" || true)
[ -z "$left" ] || bad "a site stamped for Some-One/their-copy still names $here or the old repository:
$left"
grep -rqIF -e Some-One/their-copy -e some-one.github.io/their-copy "$t/site" ||
    bad "the stamped site names no repository at all (did the placeholders go?)"

# 3. tools/repo.sh, told the repository.
want() { got=$(AUROS_REPO=$1 sh tools/repo.sh "$2" 2>/dev/null) || got="(refused)"
         [ "$got" = "$3" ] || bad "AUROS_REPO=$1 repo.sh $2 gave $got, not $3"; }
want Some-One/their-copy site https://some-one.github.io/their-copy/
want Some-One/Some-One.github.io site https://some-one.github.io/
want Some-One/their-copy releases https://github.com/Some-One/their-copy/releases
for r in a/b/c 'a/b"c' 'a/b c' a/.. /b a; do
    AUROS_REPO=$r sh tools/repo.sh > /dev/null 2>&1 && bad "repo.sh accepted \"$r\""
done
# ... and told the owner's domain (AUROS_DOMAIN, docs/ops/DOMAIN.md).
wantd() { got=$(AUROS_REPO=Some-One/their-copy AUROS_DOMAIN=$1 sh tools/repo.sh "$2" 2>/dev/null) || got="(refused)"
          [ "$got" = "$3" ] || bad "AUROS_DOMAIN=$1 repo.sh $2 gave $got, not $3"; }
wantd Example.ORG apt https://updates.example.org/apt/
wantd example.org download https://get.example.org/download
wantd example.org get https://get.example.org/
wantd example.org host example.org/
wantd example.org releases https://github.com/Some-One/their-copy/releases
wantd '' apt https://github.com/Some-One/their-copy/releases/download/apt/
wantd '' get https://github.com/Some-One/their-copy/releases/latest/download/AurOS-Setup.exe
for d in x .a.b a..b 'a b.c' a/b.c -a.b a.b- 'a"b.c'; do
    AUROS_REPO=Some-One/their-copy AUROS_DOMAIN=$d sh tools/repo.sh > /dev/null 2>&1 && bad "repo.sh accepted AUROS_DOMAIN=\"$d\""
done

[ "$fail" = 0 ] && echo "repotest: ok (no old name outside docs/; a move to Some-One/their-copy names only it)"
exit "$fail"
