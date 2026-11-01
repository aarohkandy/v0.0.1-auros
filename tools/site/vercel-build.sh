#!/bin/sh
# The website on Vercel (vercel.json runs this): the same stamped copy
# pages.yml checks, written to .vercel-site. Vercel's own variables say which
# repository this is and the address she types; AUROS_SITE (a Vercel
# environment variable) wins once there is a custom domain.
set -eu
cd "$(dirname "$0")/../.."
if [ -n "${VERCEL_GIT_REPO_OWNER:-}" ] && [ -n "${VERCEL_GIT_REPO_SLUG:-}" ]; then
    AUROS_REPO=${AUROS_REPO:-$VERCEL_GIT_REPO_OWNER/$VERCEL_GIT_REPO_SLUG}
fi
AUROS_SITE=${AUROS_SITE:-${VERCEL_PROJECT_PRODUCTION_URL:-}}
export AUROS_REPO AUROS_SITE
sh tools/site/stamp.sh website .vercel-site
