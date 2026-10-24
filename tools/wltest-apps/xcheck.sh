#!/bin/sh
# xcheck.sh -- run by `wltest-apps.sh "custom:sh /w/tools/wltest-apps/xcheck.sh"` inside the container, as the
# program on the real aurshell's desktop (DISPLAY set by aurwl_spawn). Writes /out/xcheck.txt:
#   - the parity probe (tools/parity/guest/xprobe.py) sees the X window the desktop manages, and
#   - the X root carries her text size as Xft.dpi (TEXT_SCALE=2.0 -> 192), as a real xrdb reads it.
rm -f /out/xcheck.txt
xterm -fa Monospace -fs 14 -T xcheck -e sh -c 'sleep 600' &
{
  echo "display $DISPLAY"
  python3 /w/tools/parity/guest/xprobe.py 20; echo "probe exit $?"
  echo "xrdb: $(xrdb -query 2>&1 | tr '\t\n' '  ')"
  echo "client list: $(xprop -root _NET_CLIENT_LIST 2>&1)"
} > /out/xcheck.txt 2>&1
sleep 600
