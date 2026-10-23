# A company for AurOS: the checklist (owner-only steps)

Prepared by the AurOS agents without a lawyer (D15). `COMPLIANCE.md` LIA-6, BIZ-1/2/5. What an LLC
does and does not do: it protects the owner from the company's contracts and from other people's
mistakes; it does **not** protect him from his own negligence or from deceptive claims he makes, so
insurance (`INSURANCE-QUESTIONS.md`) matters as much. If the owner is **under 18**, tell the agents
first: a minor's contracts are voidable and an adult co-owner or a fiscal sponsor is needed.

Hard gate before charging money and before Wishes goes beyond the family; before the public link is
strongly advised. In order:

1. **Name.** Check the company name on the Secretary of State site of the home state and the
   USPTO search. The product name is a separate problem (`NAME-SEARCH.md`).
2. **Form a single-member LLC in the state where the owner lives** (Wyoming or Delaware add a second
   registration at home with no liability benefit). Costs: filing about $35-$500 once; then it
   varies: California $70 + $20 + $800 a year minimum franchise tax even with no income, New York can
   add $1,000+ publication, Massachusetts $500 + $500 annual report, Texas about $300 and a free
   report. Year one $200-$1,500 in most states (fees unverified; read the state's own page).
3. **EIN** at irs.gov, free. No SSN or EIN goes in the public repository.
4. **One-page operating agreement** (free templates are fine) with: the LLC owns all AurOS code,
   brand, domain and keys; a named successor if the owner dies or cannot act (a Delaware LLC with no
   member winds up unless continued within 90 days); funds are never mixed.
5. **Business bank account**, separate; every vendor bill moves onto an LLC card.
6. **IP assignment** from the owner to the LLC: `templates/IP-ASSIGNMENT.md`, sign and date it. Only
   then change the license line (`PROVENANCE.md`).
7. **Capitalize** the LLC (put in money; an empty LLC is easier to pierce) and get the insurance quote.
8. **Move accounts**: the GitHub repository to a free GitHub organization with a **second owner** (the
   second responder), with the new web address in the same step (a repository transfer redirects
   git links but not GitHub Pages: `docs/ops/DOMAIN.md`), then Cloudflare, Anthropic, the domain
   registrant, SignPath. Two hardware keys or passkeys for 2-step sign-in.
9. **Fill the name and contact into** `TERMS.md`, `PRIVACY.md`, `DISCLAIMER.md`, `SECURITY.md`, the
   footer and `LICENSE`; a "Who we are" page if a registered-agent address replaces a home address.
10. **A dated calendar item** for the annual report and franchise tax once the state is known.
11. `docs/SUPPORT.md` line 3 says "a small team": change to "one founder plus automated tooling".
12. Add an LLC row to `docs/LAUNCH-READINESS.md` (owner-attested date; no EIN in the repo).
