# Locking the computer: what she sees (one page)

For the owner and the lead. Design: `docs/security/THEFT.md`. State:
`docs/handoff/streams/S4.md`. The words are the ones on the screens
(`docs/shots/theft/` has pictures of the start pages). Nothing here is
switched on yet. **Built and proven:** the start pages (PIN, wrong
PIN, too many tries, "this computer has changed", rescue code, choose a
PIN again) in `src/aurshell/lockpin.c`. **Not built yet** (aurshell):
the "Locking your drive" line, the first "Choose your PIN" + rescue sheet
pages after install, Print it, and the lock on wake.

## The idea, in her words

"If someone steals my computer, they get a computer, not my photos."
She is never asked to understand encryption. She is asked for a short
number, twice in her life and then every morning, and to keep one
piece of paper.

## When she is asked, and what she sees

| When | What is on the screen | Her one action |
|---|---|---|
| Right after she says "Yes, keep AurOS" | Nothing new. AurOS restarts once ("AurOS will restart once to lock the drive") and she carries on. A quiet line in the corner: "Locking your drive: 40%". She can use the computer. | None |
| When the locking is finished (5 to 30 minutes) | **Choose your PIN.** A big number pad. "Pick 4 to 6 numbers you will remember." She types it, then again ("So we know it is the one you meant."). | Type 4 to 6 numbers, twice |
| Straight after | **Your rescue code.** Six groups of four numbers, big. "If you ever forget your PIN, this code opens your computer. Write it down or print it, and keep it at home, away from the computer. Nobody else has a copy, not us either." Buttons: **Print it**, **I have written it down**. The page will not close until she presses one. | Write it down or print it |
| Every time the computer starts | A full-screen AurOS page, her colours, with the number pad. "Type your PIN to start AurOS." Dots fill as she types; the right length starts it by itself. | Type her PIN |
| She types it wrong | "That PIN didn't work. Try again." Never says how many tries are left (that helps a thief). | Try again |
| Too many wrong tries | "Too many tries. This computer is waiting before it lets anyone try again. You can try your PIN again in about 10 minutes, or use your rescue code now." One button: **Use my rescue code**. | Wait, or use the paper |
| The computer changed (a security update, a new part) | "This computer has changed. Since you last used it, something about this computer changed, so it needs your rescue code this once." Number pad for the code, then **Choose your PIN** again (it may be the same). Next start is the normal PIN. | Type the 24 numbers from the paper |
| She forgot her PIN | "I forgot my PIN" is under the number pad, always. It goes to the same rescue-code page. | Type the 24 numbers from the paper |
| The screen wakes up or went dark | The same number pad over a blurred desktop: "Type your PIN to carry on." (Not built yet.) | Type her PIN |
| A PC with no security chip | Same pages. One honest line when she chooses: "This computer has no security chip, so pick 6 numbers you will remember." | 6 numbers instead of 4 |

Every key and button is at least 44 pixels and the number keys on her
keyboard work too, with Num Lock off, on any keyboard. Nothing is timed.
No page needs a mouse, and none needs a keyboard.

## The paper

One sheet, printed or copied by hand. Big type. Nothing else on it.

```
        AurOS rescue code

   For:  Margaret's computer            Made:  23 October 2026

   If you forget your PIN, or AurOS says "This computer has
   changed", type these 24 numbers on the AurOS start page.

        4821   0937   1158
        6620   3745   9012

   Keep this paper at home, away from the computer.
   Nobody else has a copy. We cannot make a new one for you:
   in AurOS, Settings, "Make a new rescue code" (it asks for your PIN).
```

It is made once, shown once, never stored on the computer and never
sent anywhere (no accounts, no Microsoft: D7). Losing both the PIN and
the paper means the files cannot be opened by anyone, including us.
That is the product working, and the page says so at the start.

## What we are honest about (also written in Help)

- It keeps out a thief. It does not keep out someone who has her laptop
  and her PIN, or who watched her type it.
- Windows, while it is still on the computer, is as protected as before.
- A computer with no security chip is protected less: an expert with
  days and special equipment could guess a 6-number PIN. Said once.
- Between "Yes, keep AurOS" and "Choose your PIN" (about half an hour)
  the drive is encrypted but opens without a PIN. Her files are not
  copied over until locking has started.

## Decisions the lead should make

1. **PCR 7 or not.** With it, a Windows or firmware update that changes
   the computer's security settings asks her for the paper once. Without
   it that never happens and the protection is the same against a thief
   (the PIN, counted by the chip, is what protects). Recommendation:
   without. Both are built and proven (`THEFT_PCRS=7|none`).
2. **The wait before "Choose your PIN"** (locking must finish first).
   Alternative: ask for the PIN first and lock after, which needs the
   PIN kept in memory across a restart (worse).
3. **Print it.** Needs a printer set up; otherwise she writes it by hand.
   A memory stick is offered as a second copy, with the "away from the
   computer" warning.
