#!/usr/bin/env python3
"""NOT PART OF AUROS. Writes a tiny .docx and .xlsx (plain zip files of XML, as Word and Excel write them):
`mkoffice.py DIR` -> DIR/letter.docx, DIR/budget.xlsx."""
import sys
import zipfile

CT = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
      '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
      '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
      '<Default Extension="xml" ContentType="application/xml"/>%s</Types>')
REL = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
       '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">%s</Relationships>')
OD = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/"


def docx(path):
    z = zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED)
    z.writestr("[Content_Types].xml", CT % '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>')
    z.writestr("_rels/.rels", REL % ('<Relationship Id="rId1" Type="%soffice' % OD + 'Document" Target="word/document.xml"/>'))
    z.writestr("word/document.xml", '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
               '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body>'
               '<w:p><w:r><w:t>Dear Ada, the AurOS test number is 7d41.</w:t></w:r></w:p></w:body></w:document>')
    z.close()


def xlsx(path):
    z = zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED)
    z.writestr("[Content_Types].xml", CT % ('<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
                                          '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'))
    z.writestr("_rels/.rels", REL % ('<Relationship Id="rId1" Type="%soffice' % OD + 'Document" Target="xl/workbook.xml"/>'))
    z.writestr("xl/workbook.xml", '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
               '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
               'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
               '<sheets><sheet name="Budget" sheetId="1" r:id="rId1"/></sheets></workbook>')
    z.writestr("xl/_rels/workbook.xml.rels", REL % ('<Relationship Id="rId1" Type="%sworksheet" Target="worksheets/sheet1.xml"/>' % OD))
    z.writestr("xl/worksheets/sheet1.xml", '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
               '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
               '<row r="1"><c r="A1" t="inlineStr"><is><t>Groceries</t></is></c><c r="B1"><v>40</v></c></row>'
               '<row r="2"><c r="A2"><v>2</v></c><c r="B2"><f>A2+B1</f></c></row>'
               '</sheetData></worksheet>')
    z.close()


if __name__ == "__main__":
    docx(sys.argv[1] + "/letter.docx")
    xlsx(sys.argv[1] + "/budget.xlsx")
