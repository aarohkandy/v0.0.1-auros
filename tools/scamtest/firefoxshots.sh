#!/bin/sh
# firefoxshots.sh OUTDIR -- the pictures behind firefoxlive.py: the real
# Firefox on a virtual screen, before and after the two fixes.
#
#   tou-before.png    first start WITHOUT the two switches that hide it
#                     (SkipTermsOfUse, and the locked preference
#                     datareporting.policy.dataSubmissionPolicyBypassNotification;
#                     either one hides it, found by bisecting): "By continuing,
#                     you agree to the Terms of Use ... sends diagnostic and
#                     interaction data to Mozilla" covers her first page
#   tou-after.png     first start with the file as it ships: a plain browser
#   crash-before.png  the browser crashed with no crash-report switch: Mozilla's
#                     window, "Tell Mozilla about this crash" and "Include the
#                     address of the page I was on" both ticked
#   crash-after.png   the same crash with the desktop's environment: nothing
#
# Root, in a throwaway container with firefox (Mozilla's apt), xvfb and
# imagemagick; run from the repository root:
#   docker run --rm --privileged -v "$PWD":/w -w /w IMAGE sh tools/scamtest/firefoxshots.sh docs/shots/firefox
set -eu
out=${1:?output directory}; mkdir -p "$out"
pol=rootfs/etc/firefox/policies/policies.json
mkdir -p /etc/firefox/policies

one() {   # name policy-file crash(0|1) env...
    name=$1; pf=$2; crash=$3; shift 3
    cp "$pf" /etc/firefox/policies/policies.json
    P=$(mktemp -d); mkdir -p "$P/home" "$P/p"
    ( export HOME="$P/home" DISPLAY=:77 LIBGL_ALWAYS_SOFTWARE=1; for kv in "$@"; do export "$kv"; done
      firefox --profile "$P/p" --no-remote about:blank >"$P/out" 2>&1 &
      sleep 25
      if [ "$crash" = 1 ]; then kill -SEGV "$(pgrep -of 'firefox --profile')"; sleep 8; fi
      import -window root "$out/$name.png"
      pkill crashreporter || true; pkill firefox || true; sleep 2 )
    rm -rf "$P"
}

Xvfb :77 -screen 0 1024x600x24 >/dev/null 2>&1 &
sleep 1
no_tou=$(mktemp)
sed -e '/"SkipTermsOfUse"/d' -e '/dataSubmissionPolicyBypassNotification/d' "$pol" > "$no_tou"
one tou-before "$no_tou" 0
one tou-after  "$pol" 0
one crash-before "$pol" 1
one crash-after  "$pol" 1 MOZ_CRASHREPORTER_DISABLE=1
