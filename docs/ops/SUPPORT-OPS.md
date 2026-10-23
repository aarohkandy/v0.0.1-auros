# Support mail: what to do with it (private; not the user-facing `docs/SUPPORT.md`)

Prepared by the AurOS agents without a lawyer (D15). `COMPLIANCE.md` PRI-7. Written for whoever
reads the mailbox.

1. **Use only what she sent, only to answer her and fix AurOS.** Do not forward it, paste it into
   a public issue, or keep it in an AI chat longer than the task.
2. **Never ask for passwords, recovery keys, card numbers or private files.** If she sent any,
   tell her to change the password and delete the message from the thread; delete it from the
   mailbox too (a bank number in an email is not ours to keep).
3. **Retention.** `PRIVACY.md` promises: normally no more than 12 months, then deleted; a problem
   report is kept as long as needed to fix it. On Gmail, filters act on incoming mail only, so the
   purge is a recurring calendar item (monthly) or an Apps Script time trigger. A data-loss case
   is kept longer on purpose, in a private issue (`INCIDENTS.md` playbook 5, step 6).
4. **Delete a thread on request** (she asks, or the 30 days of `PRIVACY.md` section 7 run out):
   find every message with her address, delete, empty the trash, note "deleted <date>" in the
   private ticket without her address.
5. **Turn on 2-step verification (a security key) for the mailbox**, Cloudflare and GitHub.
6. If a report Worker (`/v1/report`) is ever built, decide first whether it stores the report
   (a 90-day TTL, field allow-list, serials hashed or dropped, network name dropped, home path
   stripped) or only forwards it (the inbox rule above is then the only retention).
