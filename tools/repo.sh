#!/bin/sh
# Which GitHub repository this checkout is published from, and the
# addresses that follow from it. Nothing else in the tree (outside docs)
# names the repository: everything asks here, so a move to another
# repository changes nothing but the clone address (docs/MOVING.md).
#
#   sh tools/repo.sh            owner/name           Someone/auros
#   sh tools/repo.sh owner      the owner            Someone
#   sh tools/repo.sh name       the repository name  auros
#   sh tools/repo.sh site       the website          https://someone.github.io/auros/
#   sh tools/repo.sh host       the same, no scheme  someone.github.io/auros/
#   sh tools/repo.sh releases   the releases         https://github.com/Someone/auros/releases
#   sh tools/repo.sh apt        the update channel   https://github.com/Someone/auros/releases/download/apt/
#   sh tools/repo.sh download   where pieces live    https://github.com/Someone/auros/releases/download
#                               (a release's files are <download>/<tag>/<file>)
#   sh tools/repo.sh get        the installer        https://github.com/Someone/auros/releases/latest/download/AurOS-Setup.exe
#
# AUROS_DOMAIN=example.org (the repository variable, docs/ops/DOMAIN.md):
# site, host, apt, download and get become the owner's own addresses
# (https://example.org/, https://updates.example.org/apt/,
# https://get.example.org/download, https://get.example.org/), which
# Cloudflare passes on to GitHub (infra/edge). Unset or empty: GitHub's.
# A computer keeps the addresses it was built with for good, so nothing
# she installs may be built before the domain is set (PLAN-END-TO-END §3).
#
# Where the answer comes from, first that is set:
#   1. AUROS_REPO=owner/name      (to build for another repository)
#   2. GITHUB_REPOSITORY          (set by GitHub Actions)
#   3. the address of the checkout's `origin` remote
# Anything that is not a plain owner/name is refused: the answer is
# baked into C, HTML and URLs, so it may hold only the characters GitHub
# allows in those two names. Exit 1, with the reason, when there is no
# answer.
set -eu
here=$(cd "$(dirname "$0")" && pwd)

if [ -n "${AUROS_REPO:-}" ]; then repo=$AUROS_REPO; from=AUROS_REPO
elif [ -n "${GITHUB_REPOSITORY:-}" ]; then repo=$GITHUB_REPOSITORY; from=GITHUB_REPOSITORY
else
    from="the origin remote"
    url=$(git -C "$here" remote get-url origin 2>/dev/null || true)
    # https://github.com/o/n(.git), git@github.com:o/n(.git),
    # ssh://git@github.com/o/n, or a proxy's http://h:p/git/o/n: the
    # last two parts of the address are the owner and the name.
    repo=$(printf '%s\n' "$url" | sed -e 's|/*$||' -e 's|\.git$||' \
        -e 's|^.*[/:]\([^/:]*\)/\([^/]*\)$|\1/\2|')
    [ -n "$url" ] || {
        echo "tools/repo.sh: cannot tell which GitHub repository this is: no AUROS_REPO," >&2
        echo "  no GITHUB_REPOSITORY and no origin remote. Set AUROS_REPO=owner/name." >&2
        exit 1; }
fi
case "$repo" in
    */*/*|/*|*/) bad=1 ;;
    */*) bad=0 ;;
    *) bad=1 ;;
esac
owner=${repo%%/*}; name=${repo#*/}
if [ "$bad" = 1 ] ||
   printf '%s' "$owner" | LC_ALL=C grep -q '[^A-Za-z0-9-]' ||
   printf '%s' "$name" | LC_ALL=C grep -q '[^A-Za-z0-9._-]' ||
   [ "$name" = . ] || [ "$name" = .. ]; then
    echo "tools/repo.sh: \"$repo\" (from $from) is not a GitHub owner/name" >&2
    exit 1
fi

# GitHub Pages: https://<owner, lower case>.github.io/<name>/, except the
# repository called <owner>.github.io, whose site is the host itself.
lower=$(printf '%s' "$owner" | tr 'A-Z' 'a-z')
if [ "$(printf '%s' "$name" | tr 'A-Z' 'a-z')" = "$lower.github.io" ]; then
    host="$lower.github.io/"
else
    host="$lower.github.io/$name/"
fi

gh="https://github.com/$owner/$name/releases"
apt="$gh/download/apt/"; download="$gh/download"; get="$gh/latest/download/AurOS-Setup.exe"
dom=$(printf '%s' "${AUROS_DOMAIN:-}" | tr 'A-Z' 'a-z')
if [ -n "$dom" ]; then
    # Baked into C, apt sources and URLs: a plain host name, nothing else.
    case "$dom" in
        *[!a-z0-9.-]*|.*|*.|-*|*-|*..*|*.-*|*-.*) bad=1 ;;
        *.*) bad=0 ;;
        *) bad=1 ;;
    esac
    [ "$bad" = 0 ] || { echo "tools/repo.sh: AUROS_DOMAIN \"$dom\" is not a domain name" >&2; exit 1; }
    host="$dom/"; apt="https://updates.$dom/apt/"
    download="https://get.$dom/download"; get="https://get.$dom/"
fi

case "${1:-repo}" in
    repo)     echo "$owner/$name" ;;
    owner)    echo "$owner" ;;
    name)     echo "$name" ;;
    site)     echo "https://$host" ;;
    host)     echo "$host" ;;
    releases) echo "$gh" ;;
    apt)      echo "$apt" ;;
    download) echo "$download" ;;
    get)      echo "$get" ;;
    *) echo "usage: sh tools/repo.sh [repo|owner|name|site|host|releases|apt|download|get]" >&2; exit 2 ;;
esac
