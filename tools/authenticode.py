#!/usr/bin/env python3
"""authenticode — the SHA-256 Authenticode digest of a PE, from the bytes.

    tools/authenticode.py digest FILE     print the digest (64 hex)
    tools/authenticode.py signed FILE     exit 0 if FILE carries a signature

The digest is what a signature signs: the file with its checksum field,
its certificate-table entry and the certificate table itself left out.
So the unsigned AurOS-Setup.exe and the one SignPath sends back must have
the SAME digest -- the signature is the only difference -- and that is
the check (tools/signed-check.sh) that what was signed is what was built,
not just that "a signature verifies". It is written here, in another
language than osslsigncode, and compared with osslsigncode's own
"Calculated message digest" on the signed file, so neither can be wrong
alone.
"""
import hashlib, struct, sys


def _u16(b, o): return struct.unpack_from('<H', b, o)[0]
def _u32(b, o): return struct.unpack_from('<I', b, o)[0]


def _layout(b):
    if b[:2] != b'MZ':
        raise SystemExit('not a Windows program')
    pe = _u32(b, 0x3C)
    if b[pe:pe + 4] != b'PE\0\0':
        raise SystemExit('no PE header')
    nsec = _u16(b, pe + 6)
    opt_sz = _u16(b, pe + 20)
    opt = pe + 24
    pe32plus = _u16(b, opt) == 0x20b
    checksum = opt + 64
    cert_dir = opt + (112 if pe32plus else 96) + 4 * 8
    size_hdr = _u32(b, opt + 60)
    cert_off, cert_len = _u32(b, cert_dir), _u32(b, cert_dir + 4)
    secs = []
    for i in range(nsec):
        o = opt + opt_sz + i * 40
        size, ptr = _u32(b, o + 16), _u32(b, o + 20)
        if size:
            secs.append((ptr, size))
    return checksum, cert_dir, size_hdr, sorted(secs), cert_off, cert_len


def digest(path):
    b = open(path, 'rb').read()
    checksum, cert_dir, size_hdr, secs, cert_off, cert_len = _layout(b)
    h = hashlib.sha256()
    h.update(b[:checksum])
    h.update(b[checksum + 4:cert_dir])
    h.update(b[cert_dir + 8:size_hdr])
    pos = size_hdr
    for ptr, size in secs:
        h.update(b[ptr:ptr + size])
        pos = ptr + size
    # Whatever follows the last section (an overlay), up to the
    # certificate table, which is not hashed.
    if cert_off:
        h.update(b[pos:cert_off])
        h.update(b[cert_off + cert_len:])
    else:
        h.update(b[pos:])
    return h.hexdigest()


def main(argv):
    if len(argv) == 3 and argv[1] == 'digest':
        print(digest(argv[2]))
        return 0
    if len(argv) == 3 and argv[1] == 'signed':
        b = open(argv[2], 'rb').read()
        return 0 if _layout(b)[5] else 1
    print(__doc__.strip().split('\n\n')[0], file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
