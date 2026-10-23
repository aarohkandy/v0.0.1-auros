# (d) A Word file and an Excel file open and save. A person double-clicks a .docx or .xlsx from a
# download or an email; something must open it, and saving must keep what is in it.
. /usr/lib/auros-parity/lib.sh
D="$HOME/Documents"; mkdir -p "$D" "$HOME/out"
python3 "$P/mkoffice.py" "$D"
DOCX=application/vnd.openxmlformats-officedocument.wordprocessingml.document
XLSX=application/vnd.openxmlformats-officedocument.spreadsheetml.sheet
[ -s /var/lib/auros-parity/apt-size.log ] && note "size of what was installed for this run: $(tr '\n' ' ' < /var/lib/auros-parity/apt-size.log)"

# 1. a program that asks "what is this file?" (xdg-open does, for every program that opens a file for her)
gd=$(xdg-mime query filetype "$D/letter.docx"); gx=$(xdg-mime query filetype "$D/budget.xlsx")
gp=$(python3 "$P/mkpdf.py" plain "$D/x.pdf" hi; xdg-mime query filetype "$D/x.pdf")
if [ "$gd" = "$DOCX" ] && [ "$gx" = "$XLSX" ] && [ "$gp" = application/pdf ]; then
    result office.types pass "xdg-open can tell a Word, an Excel and a PDF file apart"
else
    result office.types fail "xdg-mime query filetype says docx='$gd' xlsx='$gx' pdf='$gp' (empty: xdg-open cannot choose a program and falls back to the web browser). Cause: this desktop is not one xdg-open knows, and the 'file' package it falls back to is not installed; 'gio open' works"
fi

# 2. something is registered to open them (what the file manager does on a double-click)
des=""
for m in $DOCX $XLSX; do
    app=$(gio mime "$m" 2>&1 | sed -n 's/^Default application for.*: //p' | head -1)
    ok=no; [ -n "$app" ] && gio mime "$m" 2>/dev/null | grep -q "$app" && ok=yes
    des="$des ${m##*.}=${app:-none}"
done
case "$des" in *=none*) result office.opener fail "nothing is set to open these files:$des" ;;
               *xarchiver*|*file-roller*|*ark*) result office.opener fail "a Word/Excel file would open in the ARCHIVE MANAGER (it is a zip of XML files underneath), showing her folders of XML:$des" ;;
               *) result office.opener pass "opened by:$des" ;; esac

# 3. open and save, for real
if command -v soffice >/dev/null 2>&1; then
    ( cd "$HOME/out" && timeout 240 soffice --headless --convert-to docx:"MS Word 2007 XML" --outdir . "$D/letter.docx" >/dev/null 2>&1
      timeout 240 soffice --headless --convert-to csv --outdir . "$D/budget.xlsx" >/dev/null 2>&1
      timeout 240 soffice --headless --convert-to xlsx --outdir "$HOME/out/x" "$D/budget.xlsx" >/dev/null 2>&1 )
    t=$(python3 - <<'PY'
import zipfile, os
h = os.path.expanduser("~/out")
try:
    w = zipfile.ZipFile(h + "/letter.docx").read("word/document.xml").decode("utf8", "replace")
    x = zipfile.ZipFile(h + "/x/budget.xlsx").read("xl/worksheets/sheet1.xml").decode("utf8", "replace")
    c = open(h + "/budget.csv").read()
    print("OK" if "7d41" in w and "42" in c and "<f" in x else "WRONG docx=%s csv=%r formula=%s" % ("7d41" in w, c[:40], "<f" in x))
except Exception as e:
    print("MISSING %s" % e)
PY
)
    if [ "$t" = OK ]; then result office.save pass "Writer and Calc opened both files and saved them with their contents (text, the formula and its result 42)"
    else result office.save fail "a file did not survive open and save: $t"; fi
    snap office-before
    gio open "$D/letter.docx" &
    wait_for 120 pgrep -u "$(id -un)" soffice.bin; sleep 25
    snap office-writer
    act changed office-before office-writer office.visible 260 130 1020 690 1.5
    pkill -u "$(id -un)" soffice.bin
else
    result office.save fail "no office program is installed: Writer and Calc (libreoffice-writer, libreoffice-calc) are not in the image"
fi
