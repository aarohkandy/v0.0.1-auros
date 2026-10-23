#!/usr/bin/env python3
"""NOT PART OF AUROS. A printer on the home network, announcing itself the way every IPP Everywhere
printer does: DNS-SD over multicast DNS (_ipp._tcp). Runs inside the test's network namespace, next to
ippeveprinter, because the image's avahi-daemon is configured never to publish (disable-publishing=yes),
so a printer cannot advertise from the computer itself and has to be 'another machine'.

    mdns_printer.py IP PORT "Instance name" host.local
"""
import socket
import struct
import sys
import time

IP, PORT, INST, HOST = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
GROUP = "224.0.0.251"
SVC = "_ipp._tcp.local"
FULL = "%s.%s" % (INST, SVC)


def name(n):
    return b"".join(bytes([len(p)]) + p.encode() for p in n.strip(".").split(".")) + b"\0"


def rr(n, t, data, unique=True, ttl=120):
    return name(n) + struct.pack(">HHIH", t, (0x8001 if unique else 1), ttl, len(data)) + data


TXT = ["txtvers=1", "qtotal=1", "rp=ipp/print", "ty=" + INST, "product=(ParityPrinter)",
       "pdl=application/pdf", "Color=T", "Duplex=F", "adminurl=http://%s:%d/" % (HOST, PORT),
       "note=Test printer", "priority=0", "UUID=b4585200-75cf-32cf-4498-b50fb61ba133"]
RECORDS = [
    rr("_services._dns-sd._udp.local", 12, name(SVC), unique=False),
    rr(SVC, 12, name(FULL), unique=False),
    rr(FULL, 33, struct.pack(">HHH", 0, 0, PORT) + name(HOST)),
    rr(FULL, 16, b"".join(bytes([len(i)]) + i.encode() for i in TXT)),
    rr(HOST, 1, socket.inet_aton(IP)),
]
PACKET = struct.pack(">HHHHHH", 0, 0x8400, 0, len(RECORDS), 0, 0) + b"".join(RECORDS)

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", 5353))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(GROUP) + socket.inet_aton(IP))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(IP))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
for _ in range(3):
    s.sendto(PACKET, (GROUP, 5353))
    time.sleep(1)
while True:
    data, _ = s.recvfrom(9000)
    if len(data) >= 12 and not data[2] & 0x80:        # a question, not an answer: say everything
        s.sendto(PACKET, (GROUP, 5353))
