#!/bin/sh
# NOT PART OF AUROS. Runs inside the network namespace "parityprn" (see root.sh): another machine on the
# home network that has a printer. IPP Everywhere, accepts PDF only, keeps every job it receives.
. /usr/lib/auros-parity/config
SPOOL=/var/lib/auros-parity/spool
mkdir -p "$SPOOL"; chown "$PARITY_USER" "$SPOOL"
python3 /usr/lib/auros-parity/mdns_printer.py 192.168.77.2 8000 "Parity Printer" parity-printer.local > /var/lib/auros-parity/mdns.log 2>&1 &
exec runuser -u "$PARITY_USER" -- ippeveprinter -v -k -d "$SPOOL" -F application/pdf -f application/pdf \
    -p 8000 -M AurOS -m ParityPrinter "Parity Printer" > /var/lib/auros-parity/ippeve.log 2>&1
