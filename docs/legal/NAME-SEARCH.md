# Is the name "AurOS" free to use? A knockout search, 23 October 2026

Prepared by an AI agent without a lawyer (decision D15). This is a **knockout search**: a quick look
for names that would stop us, not a clearance. It cannot say "safe". It was run on 23 October 2026
and the facts below change from week to week (the most important one is 24 days old). Work packages TM-4 in
`COMPLIANCE.md`.

## The short answer

**There is a real problem, and it is new.** In the last week of September 2026 the company
UserTesting renamed itself **Auros**. At the same time it filed for the mark AUROS in the United
States, the United Kingdom, the European Union and Australia. On top of that, a company in Michigan
has owned a registered US mark AUROS for software since 2014. Our name is spelled differently only
in its capital letters: "AurOS" and "Auros" are the same word to the eye, the ear and the law.

**Risk, in plain words: high enough that the owner should decide on purpose before the public
link goes out.** Nobody has written to us, and the goods are different (see below), so this is not
a certain loss. But the rule written down on 23 October (`COMPLIANCE.md` TM-4: "any live class 9
or 42 AUROS ... for operating-system or installer software means rename before launch") is
triggered by the Michigan registration, and a company that has just spent a rebrand on the same
word, with lawyers and a fresh filing in four countries, is the opposite of a quiet neighbour.
Renaming now costs a day or two of mostly mechanical work. Renaming after thousands of start-up menus
carry the name costs a great deal more.

**The agents' recommendation: rename before the public link, or have a lawyer (even for one hour,
outside D15) look at the two points below. Do not file a trademark application for AUROS. Testers
and the family cohort can keep using the name meanwhile.** The decision is the owner's.

## What was found

Exact word AUROS (and AUROS inside longer marks), live or pending, in the classes that matter for
software (class 9: downloadable software; class 42: software as a service). Dead and expired
records are left out.

| Who | Mark, number | Where | What it covers | Why it matters |
|---|---|---|---|---|
| Emergent Systems Corporation (Livonia, Michigan; a Delaware company) | AUROS, reg. 4512239, serial 85849657, registered 8 Apr 2014, renewed Jan 2025 | United States, class 9 | "Computer software for the collection, editing, organizing, modifying, book marking, transmission, storage and sharing of data and information", but not software for the audio mixing industry | **Identical word, class 9 software, live.** An operating system stores and organizes data, so the wording can be read to reach it. Their product is described as knowledge-management software for engineering companies, which is far from a desktop for grandparents, and that distance is our best argument. |
| UserTesting Technologies, Inc. (San Francisco), now trading as Auros | AUROS, serial 50138003, filed 30 Sep 2026, awaiting examination | United States, class 42 | Software as a service for recruiting and managing research participants, and for testing, evaluating and reporting on products and **artificial intelligence models** | Identical word, a new, well-funded owner, software, AI. Different goods from ours, but it is the sort of company that opposes and sends letters, and it is still filing. |
| The same | AUROS, UK00004449942 (filed 23 Sep 2026), EUIPO 019426684 (23 Sep 2026), Australia 2702554 (16 Oct 2026) | UK, EU, Australia, class 42 | the same | shows they are filing worldwide, week by week |
| Optimum Investment Limited (China) | AUROS, serial 90260346 and others (registered 2021-23) | US class 9, and 9 and 11 in the UK, EU and Canada | household electronics (batteries, microphones, modems, scanners, 3D spectacles) and many other goods | Class 9 hardware, not software. Low, but it is another live AUROS in class 9. |
| metamorphosis GmbH | AUROS, EUIPO 019429169, filed 29 Sep 2026 | EU, classes 9 and 10 | not read in full | class 9 and filed this week: a medical-device flavour, probably unrelated, to be read |
| Auros Risk Management, LLC | AUROS, serial 98564948 and 98564949 | US, class 36 | insurance underwriting | unrelated |
| The Auros Group, Inc. (Pennsylvania) | AUROS GROUP, AUROS360, AUROS INSIGHTS | US, class 42 | building-performance analysis and software as a service for it | the leads the review already knew; not operating-system software |

Names that sound alike, class 9 or 42: none exact for "AURORA OS" in the US, UK, EU, WIPO, Canada
or Australia (TMview, exact search). Near names in use are in the next section.

## Other places

- **App stores (US App Store, by Apple's search service):** an iPhone app "auros" (a relaxation
  app, i6 Developments LLC, June 2026) and "Auros" (a sales tool, Paytime Fintech LTDA, Brazil).
  Not operating systems.
- **GitHub:** the account and organization `Auros` (a game-modding author, many small projects),
  nothing called AurOS that is an operating system; near names exist: Aurora OS.js (a game),
  AuraOS (software for AI agents), AuriOS (a hobby kernel), AurorOS (a hobby OS), Aurora OS for the
  Nintendo 3DS. No clash with an operating system of the same name was found.
- **Package names:** `auros` exists on npm (an empty 2022 placeholder). Not on PyPI or crates.io.
  Not on Flathub.
- **Domains:** auros.com, .io, .org, .dev and .ai all resolve to someone else already. A domain for
  this project cannot be `auros.<anything common>`; this matters for `AUROS_DOMAIN` (BIZ-5).
- **Russia's "Aurora OS"** (a phone operating system) is a different word and a different market;
  not a conflict for a US-first launch, but it is the name people will confuse us with in search.

## What could not be searched, and why

- **WIPO Global Brand Database** (branddb.wipo.int): the site answered with a "Verified" box, a
  bot check. The agents do not get past bot checks, so it was **not searched**. TMview (EUIPO's tool,
  which includes WIPO's international registrations and the US, UK, Canada and Australia) was
  searched instead. The owner can run the Global Brand Database by hand in five minutes:
  Brand name "AUROS", then look at classes 9 and 42.
- **USPTO details** of records other than the three above were temporarily unavailable ("status
  information is currently unavailable", the USPTO says its servers are strained). The search
  list was read in full; goods wording for the rest comes from the list's first line only.
- **Google Play** and other national offices (Japan, India, China, Brazil) were not searched.
- **Common-law use** (a product called Auros that is not registered) was looked for with web search
  only: UserTesting's rebrand (reported 29 Sep and 1 Oct 2026) and Emergent Systems' enterprise
  software were found.

## What to do

1. **Owner decision, before the public link:** keep "AurOS" and accept that a demand letter may come
   and the name may have to go later, or rename now. Testers and the family cohort do not need the
   decision.
2. If **renaming**: the name is in about 680 files (361 under `src`, `build`, `rootfs`, `profiles`,
   `website`, `tools`). The image already reads its name from the profile (`brand_name`,
   `brand_short` in `build/forge`), but the installer, the website, the shell and the help pages
   have it written in. Pick a **coined** word, then repeat this search for it first (steps below)
   before any work. A one-hour fixed-fee call to a trademark lawyer is cheaper than a second rename.
3. If **keeping**: do not file for AUROS; do not buy `auros.*` domains; keep the non-affiliation
   paragraph; write "AurOS is not made by or connected with any company named Auros" in the
   website footer only after the owner chooses this path (a disclaimer does not cure confusion, it
   only helps); watch the two US records (status changes are public) and re-run this search before
   any money is taken.
4. Whichever is chosen, tell the lead, and record the choice here.

## How to run it again (about an hour, free)

1. USPTO: https://tmsearch.uspto.gov, "Wordmark", type the name; open the live records' status
   pages (https://tsdr.uspto.gov, "Goods and Services").
2. TMview: https://www.tmdn.org/tmview, trade mark name, tick EUIPO, UKIPO, WIPO, CIPO, IP Australia
   (add the US), look at classes 9 and 42, decline the cookies.
3. WIPO Global Brand Database: https://branddb.wipo.int (by hand; it asks for a human).
4. Apple: `https://itunes.apple.com/search?term=NAME&entity=software&country=us`; Google Play by hand.
5. GitHub: `gh api -X GET search/repositories -f q='NAME in:name'`, and users; PyPI, npm,
   crates.io, Flathub by address.
6. Web search for "NAME" with "operating system", "software", "rebrand".
7. Write the result here with the date. A clean knockout is still not a clearance: before spending
   real money on the name (hardware, ads, a company name), pay for a full search.
