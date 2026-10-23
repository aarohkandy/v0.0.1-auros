# (e) A PDF form is filled in and saved: the way she does it -- double-click the file (the default
# viewer opens it), click a box, type, Save a copy -- and the typed words are in the saved file.
. /usr/lib/auros-parity/lib.sh
mkdir -p "$HOME/Documents"; rm -f "$HOME/Documents/filled.pdf"
python3 "$P/mkpdf.py" form "$HOME/Documents/form.pdf"
note "default viewer: $(gio mime application/pdf 2>&1 | head -1)"
gio open "$HOME/Documents/form.pdf" &
sleep 15
snap pdfform-open
act near pdfform-open pdfform.visible 390 170 1020 690 242 242 255 6 400
click 742 299; sleep 2
typing "Ada Lovelace"
click 742 352; sleep 2
typing "London"
snap pdfform-filled
# 1. Save straight away, the way she would
key ctrl+s 12
snap pdfform-save
typing "filled-a"
snap pdfform-name
key ret 8
# 2. Leave the last box first (Tab, as a form is normally filled in), then save
key tab 3
key ctrl+s 12
typing "filled-b"
key ret 8
snap pdfform-saved
pkill -u "$(id -un)" evince

# are the typed words inside the saved file (plain text or UTF-16, hex or not, compressed or not)?
words() {
python3 - "$1" <<'PY'
import sys, re, os, zlib
p = sys.argv[1]
if not os.path.exists(p):
    print("MISSING"); sys.exit(0)
d = open(p, "rb").read()
blobs = [d]
for m in re.finditer(rb"stream\r?\n(.*?)\r?\nendstream", d, re.S):
    try: blobs.append(zlib.decompress(m.group(1)))
    except Exception: pass
def has(t):
    forms = [t.encode(), t.encode("utf-16-be"), t.encode().hex().encode(), t.encode("utf-16-be").hex().encode()]
    return any(f.lower() in b.lower() for b in blobs for f in forms)
n, c = has("Ada Lovelace"), has("London")
print("OK" if n and c else "name=%s city=%s" % ("kept" if n else "LOST", "kept" if c else "LOST"))
PY
}
va=$(words "$HOME/Documents/filled-a.pdf"); vb=$(words "$HOME/Documents/filled-b.pdf")
note "saved a: $va; saved b: $vb; $(ls -la "$HOME/Documents" | tail -n +2 | awk '{print $5, $9}' | tr '\n' ' ')"
if [ "$vb" = OK ]; then result pdfform.saved pass "the saved copy holds both boxes she filled in"
else result pdfform.saved fail "the saved copy is wrong: $vb"; fi
if [ "$va" = OK ]; then result pdfform.last-box pass "saving right after typing keeps the last box"
else result pdfform.last-box fail "saving right after typing loses the box she was still in ($va): the viewer only keeps a box once she leaves it"; fi
