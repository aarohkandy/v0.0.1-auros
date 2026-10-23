# (c) Printing: a printer on the home network (IPP Everywhere, here ippeveprinter) is FOUND by the
# computer (DNS-SD), added without a driver, and a print job comes out as a PDF.
. /usr/lib/auros-parity/lib.sh
python3 "$P/mkpdf.py" plain "$HOME/letter.pdf" "AurOS print test 8f3a2c"

# found: the way the printer dialog and the CUPS dnssd backend find it
URI=""
i=0
while [ "$i" -lt 40 ] && [ -z "$URI" ]; do
    URI=$(lpinfo -v 2>/dev/null | sed -n 's/^network \(dnssd:\/\/Parity.*\)$/\1/p' | head -1)
    [ -n "$URI" ] || sleep 3; i=$((i+1))
done
note "lpinfo -v: $(lpinfo -v 2>&1 | tr "\n" "|" | cut -c1-200)"
note "avahi: $(avahi-browse -a -t -r 2>&1 | head -12 | tr '\n' '|' | cut -c1-300)"
note "ns: $(cat /var/lib/auros-parity/without.log 2>/dev/null | tr '\n' '|') ping=$(ping -c1 -W2 192.168.77.2 2>&1 | tail -2 | tr '\n' '|')"
if [ -z "$URI" ]; then
    result print.discover fail "the printer on the network was never found (lpinfo -v lists no dnssd:// printer)"
    # without discovery she could still add it by address: see if everything after that works
    URI="ipp://192.168.77.2:8000/ipp/print"
else
    result print.discover pass "found $URI"
fi

ADD=$(lpadmin -p parity -E -v "$URI" -m everywhere 2>&1 | head -3 | tr "\n" " ")
note "lpadmin: $ADD"
sleep 3
note "lpstat: $(lpstat -p -a 2>&1 | tr '\n' '|' | cut -c1-300)"
rm -f /var/lib/auros-parity/spool/*
lp -d parity "$HOME/letter.pdf" 2>&1 | while read l; do note "lp: $l"; done
got=""
i=0
while [ "$i" -lt 45 ]; do
    got=$(ls /var/lib/auros-parity/spool/*.pdf /var/lib/auros-parity/spool/*.dat 2>/dev/null | head -1)
    [ -n "$got" ] && break; sleep 2; i=$((i+1))
done
note "spool: $(ls -la /var/lib/auros-parity/spool 2>&1 | tr '\n' '|' | cut -c1-300) lpstat: $(lpstat -o -W all 2>&1 | head -3 | tr '\n' '|')"
if [ -n "$got" ] && head -c 5 "$got" | grep -q '%PDF' && pdftotext "$got" - 2>/dev/null | grep -q 8f3a2c; then
    result print.pdf pass "the printer received a $(stat -c %s "$got") byte PDF with the page's text"
else
    result print.pdf fail "no PDF came out of the printer; adding it said: ${ADD:-nothing}"
fi
lpadmin -x parity 2>/dev/null
