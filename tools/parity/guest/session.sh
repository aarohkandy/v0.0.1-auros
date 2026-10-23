#!/bin/sh
# NOT PART OF AUROS. tools/parity/run.py puts this into a scratch copy of the
# image (aurshell.service.d/60-parity.conf). It starts the real, unmodified
# desktop and, beside it, the checks -- in the SAME login session and cgroup,
# so polkit, the sound server and the session bus treat the checks exactly as
# they treat a person at the keyboard.
/usr/bin/aurshell "$@" &
SH=$!
/usr/lib/auros-parity/run.sh &
wait "$SH"
