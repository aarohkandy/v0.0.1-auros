# Legal documents

**Prepared without a lawyer (owner decision D15).** Nothing in this folder is legal advice. The
agents who wrote it checked each other: the review that found what to write is `COMPLIANCE.md`
(112 candidate findings, each challenged twice by independent skeptics, 58 kept), and every
document below was read by two adversarial reviewers before it was linked from the website
(`REVIEWS.md` has what they found and what changed). An owner who wants a lawyer to read any single
item can; nothing here needs one to start. The reviewed-under-D15 line the documents carry is the
gate that used to say "approved by a lawyer": it is closed by the review, not by a person.

## The documents

| File | What it is | Where it appears |
|---|---|---|
| [TERMS.md](TERMS.md) | The Terms, v1 | `website/terms.html`; the installer's Ready page links it and carries it as text (`notices/TERMS.txt`) |
| [DISCLAIMER.md](DISCLAIMER.md) | The Disclaimer, v1 | `website/disclaimer.html`; its short form is in the installer |
| [PRIVACY.md](PRIVACY.md) | The privacy page, v1 | `website/privacy.html` |
| [LICENSES.md](LICENSES.md) | Licenses and how to get the source | `website/licenses.html` |
| [SOURCES.md](SOURCES.md) | How the source releases are made and checked | developers |
| [COMPLIANCE.md](COMPLIANCE.md) | The review: every finding, its rule, its remedy, its status | the lead, the owner |
| [WISHES-PRIVACY-DRAFT.md](WISHES-PRIVACY-DRAFT.md) | The privacy text for Wishes, which waits for its gate (D14) | never published as it stands |
| [signpath-application.md](signpath-application.md) | Text for the SignPath Foundation application | the owner, when he applies |
| `licenses/`, `notices/` | Full license texts (Debian's, byte for byte), the copyright files of the programs inside the installer, and the files generated from them | `AurOS-Setup.exe`'s Licenses button; the image |

The website pages are **generated** from the Markdown (`python3 -I tools/legal/mkpages.py`), and
the notice files from the texts they quote (`python3 -I tools/legal/notices.py`); `python3 -I
tools/legal/check.py` fails if a page or a notice file is not what its source says, if a public
text uses a word we promised not to, if a page uses another company's name where it would read as
ours, and (with `--launch`) while the owner's mailbox, `CONTACT_EMAIL`, is still a placeholder.

## What the owner still has to give

- **A mailbox** people can write to (`CONTACT_EMAIL` stands in every document until then): then
  `python3 -I tools/legal/check.py set-contact you@example.org`. The rest of the checklist is in
  `COMPLIANCE.md` section 3.
- Nothing else is needed to publish these documents. A governing-law clause, a legal name and an
  address were left out on purpose (the Terms say "write to us first" and "this does not take away
  any right you have by law"); an LLC (checklist item 11) would put its name in them.

## Rules that keep the documents true

1. **The facts are checked against the code.** Every sentence about what AurOS does or sends is in
   `CLAIMS.md` with where it is proven; when the code changes, the sentence changes first.
2. **A new outside service means a new privacy sentence before the release that adds it** (the
   outcome ping, the report button, the download-cap Worker, the notice feed, Wishes). Run the
   host list in `PRIVACY.md` against a running computer when the egress test exists
   (`COMPLIANCE.md` PRI-2).
3. **Marks.** Other companies' names appear in running text, to say what something resembles or works
   with, with the non-affiliation paragraph in every footer; never in a title, a button, an
   address or a logo (`tools/legal/check.py`).
4. **Accessibility scope.** AurOS is offered in English, for people in the United States for now. No
   EU-directed marketing, no sale to EU consumers and no EU pre-installed computers until the
   accessibility work (WP-66, WP-67) is done and `COMPLIANCE.md` WEB-4 is re-read; paid offers in
   the US, UK, Canada or Australia are re-checked against the accessibility rules at that time.
5. **State AI laws** (Colorado, California, Utah, Texas) were checked and none applies while Wishes
   is off and its answers never reach her as the model's own text (`COMPLIANCE.md` appendix 42).
6. **If anything is ever sold** (support, a paid Wishes quota, a pre-installed computer, a paid
   edition), these documents are not enough: sales need their own terms (`COMPLIANCE.md` LIA-8,
   AI-8), a merchant of record, an LLC and insurance. Before AurOS is handed over on hardware:
   Mozilla's agreement for Firefox (or ship without it), Canonical's answer in writing, the GPLv3
   path for GRUB (a way to enroll her own key, or Secure Boot off), and the vendors' firmware terms.
   Codecs: `LIC-10` (the patent licensing questions get asked before any sale).
7. **Wording that must not come back** is listed in `tools/legal/check.py` (`BANNED`).
