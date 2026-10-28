# Insurance: the questions to ask three brokers

Prepared by the AurOS agents without a lawyer (D15). `COMPLIANCE.md` LIA-7. Not a legal rule: risk
transfer. Tech errors-and-omissions (E&O) cover is usually **claims-made with a retroactive date**: cover
bought after launch may exclude what went wrong in the first week. Quotes first, after the LLC exists;
bind before wave 2 (100 downloads a day) and before any paid offer. Budget $1,000-$3,500 a year
(bundled E&O + cyber; medians from broker pages, not a quote; a free, zero-revenue product may be
declined).

Ask a specialist broker (Vouch, Hiscox, Coalition, Insureon, TechInsurance) and a local agent:

1. We ship a **disk-repartitioning installer** for Windows PCs, free. Is data loss from a disk
   operation covered?
2. Is a **free, no-revenue** product insurable? Many forms define "services" by fee.
3. Can the **retroactive date equal inception**? Is **extended reporting** (tail) available if the
   policy lapses?
4. A general-liability add-on: what does it cost, and does it exclude "electronic data"?
5. Are **Wishes** (a third-party AI service) and a later **paid tier** covered?
6. **Named insureds:** the LLC and the owner individually.
7. What must we **not** say or do after an incident (admissions, voluntary payments, releases)?
   (`docs/ops/INCIDENTS.md` playbook 5 step 6 assumes: no payment without the insurer's consent.)
8. Limits: $1M per claim. Deductible. Exclusions for prior acts, for software in beta, for AI.

**The private brief** (`INSURANCE-BRIEF.md`; keep it OUT of the public repository): what the software
does to disks; the safeguards (put-back, refusal rules, wave stop rules, e2e and real-PC evidence,
`docs/safety/FINDINGS.md`); data flows (`PRIVACY.md`); users per wave; revenue (zero); planned paid
services. Statements must trace to code and test evidence: an application is a warranty. Say plainly
that nothing is proven on a real, physical PC yet (`docs/results/hardware/CAMPAIGN.md`).

**If declined:** a written risk acceptance by the owner, plus the LLC, the disclaimers, the consent
screens, the wave caps and the stop rules. Do not describe general liability plus cyber as covering
installer data loss.
