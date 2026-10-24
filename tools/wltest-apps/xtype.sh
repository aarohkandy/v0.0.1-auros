#!/bin/sh
# xtype.sh -- an X terminal that writes the line typed into it to /out/xtype.txt, for the shell's --type path.
exec xterm -fa Monospace -fs 14 -T xtype -e sh -c 'read line; echo "got: $line" > /out/xtype.txt; sleep 600'
