#!/usr/bin/env python3
"""NOT PART OF AUROS. Writes the test's PDFs: `mkpdf.py plain OUT.pdf "text"`."""
import sys


def build(objs):
    out, offs = b"%PDF-1.5\n", []
    for i, o in enumerate(objs, 1):
        offs.append(len(out))
        out += b"%d 0 obj\n" % i + o + b"\nendobj\n"
    x = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    out += b"".join(b"%010d 00000 n \n" % o for o in offs)
    return out + b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)


def plain(text):
    s = b"BT /F1 24 Tf 72 700 Td (" + text.encode() + b") Tj ET"
    return build([b"<< /Type /Catalog /Pages 2 0 R >>",
                  b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                  b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
                  b"<< /Length %d >>\nstream\n" % len(s) + s + b"\nendstream",
                  b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"])


if __name__ == "__main__":
    if sys.argv[1] == "plain":
        open(sys.argv[2], "wb").write(plain(sys.argv[3]))
