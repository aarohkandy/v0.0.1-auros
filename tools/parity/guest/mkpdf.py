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


def form():
    """A one-page form with two fill-in boxes (AcroForm text fields): Full name and City."""
    s = (b"BT /F1 16 Tf 72 662 Td (Full name:) Tj ET BT /F1 16 Tf 72 612 Td (City:) Tj ET "
         b"BT /F1 24 Tf 72 730 Td (Library card form) Tj ET")
    def field(name, y):
        return (b"<< /Type /Annot /Subtype /Widget /FT /Tx /T (" + name + b") /Rect [180 " + str(y).encode() +
                b" 500 " + str(y + 28).encode() + b"] /F 4 /DA (/Helv 14 Tf 0 g) /P 3 0 R "
                b"/MK << /BC [0 0 0] /BG [0.95 0.95 1] >> /Ff 0 >>")
    return build([b"<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [6 0 R 7 0 R] /DA (/Helv 14 Tf 0 g) "
                  b"/DR << /Font << /Helv 5 0 R >> >> /NeedAppearances true >> >>",
                  b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                  b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
                  b"/Resources << /Font << /F1 5 0 R /Helv 5 0 R >> >> /Annots [6 0 R 7 0 R] >>",
                  b"<< /Length %d >>\nstream\n" % len(s) + s + b"\nendstream",
                  b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
                  field(b"fullname", 650), field(b"city", 600)])


if __name__ == "__main__":
    if sys.argv[1] == "plain":
        open(sys.argv[2], "wb").write(plain(sys.argv[3]))
    elif sys.argv[1] == "form":
        open(sys.argv[2], "wb").write(form())
