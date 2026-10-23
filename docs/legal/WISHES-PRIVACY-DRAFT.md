# Wishes: the privacy text that waits for the Wishes gate (NOT published)

Wishes (the AI helper, `docs/AI.md`) is switched off in v0.1 (`wishes="off"`, decision D14), so the
published privacy page (`PRIVACY.md`) says only that it is off and that the page will be updated
before it is ever switched on. This file keeps the earlier draft of the section for that day. **It
is not true of any shipped version and must not be linked or published as it stands.**

Before it can be used, the corrections in `COMPLIANCE.md` have to be made and checked against the
shipped service: PRI-3 (what the Workers keep), PRI-4 and AI-3/AI-4 (the exact consent text: it
is an AI, made by Anthropic; 30 days, 2 years for flagged requests, 7 years for safety scores; no
training; adults only; emergencies; controller name and contact; legal basis; transfer; rights),
PRI-5 (health words), PRI-6 (UK/EU), AI-1/AI-6 (kill switch, countries). The text of the consent
screen (`src/aurshell/wish.c` `CONSENT[]`), `docs/CLOUD.md` section 4, `docs/AI.md` section 1.4 and
this section must say the same thing; extend `tools/wishtest.sh` (or the lint of WP-49) to compare
them.

---

## 4a. Wishes (only if you switch it on)

[CHECK: this section follows the contract in `docs/AI.md` (sections 1.4,
1.8 and the red team's change 12) as of 2026-10-23. Re-check it against
the shipped service and consent screen before publishing; the consent
screen must link here.]

Wishes lets you tell your computer, in your own words, what you would
like it to do ("make the words bigger every evening"). It is **off until
you say yes** on a screen that explains it first. If you say no, nothing
is sent, and the rest of AurOS works the same.

When it is on, each time you make a wish or ask a question, your
computer sends:

- the **words of your wish or question**;
- your **language**;
- a **list of what your computer can do** with a wish: which looks and
  sensors it has, and which programs are installed;
- a **random number** your computer made up when you switched Wishes
  on, so that the number of wishes a day can be limited fairly. It is not
  your name and is not linked to you.

It never sends what you type anywhere else, your files, your contacts,
your passwords or where you are.

**Who receives it.** Our Wishes service, which runs on **Cloudflare**
(Cloudflare, Inc., USA), passes your words to **Anthropic** (Anthropic
PBC, USA), whose AI model, Claude, turns them into a wish. Your computer
then shows you the wish, in its own words, and does nothing until you
press Yes. Both companies are in the United States, so your words leave
your country. [LAWYER: the legal basis (consent?), the transfer
mechanism to the US, and the processor agreements with Cloudflare and
Anthropic. OWNER: sign Anthropic's data processing agreement and ask for
zero data retention; then make the next paragraph match what was agreed.]

**What is kept.** Our service keeps only counts: how many wishes, how
long they took and what they cost. It never keeps the words of a wish.
The daily counters are scrambled with a key that is thrown away after two
days. Anthropic may keep a request for up to 30 days, or longer (up to
two years) if its safety systems flag it, under its own policy. [CHECK:
Anthropic's retention terms at the time of publishing, and whether zero
data retention was granted.] The wishes you keep are stored **on your
computer**, and are left out of any backup that leaves it.

**Forgetting.** Turning Wishes off, or pressing **Forget everything**,
deletes your random number and its counts from our service, and deletes
your wishes from your computer. We cannot delete Anthropic's copy of
past requests; it expires on Anthropic's schedule.

