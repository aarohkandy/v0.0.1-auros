#!/bin/sh
# xkeys.sh -- run by input-probe: an xterm that logs the bytes typed into it, and
# a look at what the window manager says about focus. Outputs go to /out.
xterm -fa Monospace -T keytest -e sh -c 'echo ready; cat -v > /out/keys.txt' &
sleep 9
xprop -root _NET_ACTIVE_WINDOW _NET_CLIENT_LIST > /out/xprop.txt 2>&1
xwininfo -root -tree > /out/tree.txt 2>&1
sleep 30
