#!/bin/sh
# The top ten programs, installed from the store's backend and opened, in a VM under KVM
# (tools/parity/guest/checks/apps.sh; the list is tools/parity/guest/apps.tsv).
#   sudo sh tools/apptest.sh --image auros-desktop.img --out apptest-out [--apps vlc,solitaire]
# Needs root (loop mount), KVM, a network that reaches Flathub, ~25 GB of room. It runs in CI as the
# second half of parity.yml; run it by hand from an Actions dispatch rather than on homebase (disk).
cd "$(dirname "$0")/.." || exit 2
exec python3 tools/parity/run.py --checks apps --disk-gb 28 --timeout 5700 "$@"
