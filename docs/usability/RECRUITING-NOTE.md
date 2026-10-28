# Recruiting: the note, the screening call, and the checks

For the owner. Five people for WP-43; three for WP-92. Everyone is aged 70
or over, uses a Windows computer at home, and can say "yes" for herself. **Do
not enrol anyone who has trouble remembering things, or who has someone else
who decides things for her.** That is not about whether she is capable; it is
about whether she can agree to something that changes her computer.

## 1. The note (send it, or read it out)

Send this from your own address, to people you know or whom someone you
know trusts. Print a copy for anyone who prefers paper.

> **Subject: Would you help me test something? A cup of tea, and about two
> hours.**
>
> Dear [name],
>
> I'm building a free program called AurOS. It gives an ordinary Windows
> computer a simpler way of working, with bigger buttons, and the words
> in plain English. It is meant for people exactly like you, who just want the
> computer to be easy. Before anyone else gets it, I would like to watch
> someone try it, so that I can find out where it is confusing. **You would be
> testing the program, not me and not you.** If something confuses you, that
> is my mistake, and it is just what I need to know.
>
> What it would involve:
> - About two and a half hours at your place, or mine, with a break and tea.
> - Putting AurOS on your own computer, finding a photo, making a video call,
>   making the words bigger, and putting everything back the way it was.
> - I would not help while you try. That is on purpose. Someone you trust can
>   sit in the room.
> - I would copy your photos and documents onto a drive of mine first, so that
>   they are safe, and check at the end that they are all where they were. Your
>   Windows stays on the computer, and you can go back to it whenever you like.
> - I would record what is on the screen (not your face). I would never ask
>   for your passwords.
> - You can stop at any time, with no explanation.
>
> It's a new program, so I'll be honest: something could go wrong, though I have
> built it so that it doesn't. That is why I copy your files first.
>
> If you'd like to help, reply here, or ring me on [number], and I'll ask you a
> few simple questions about your computer to make sure it is one the program
> can go on. If it isn't, I'd still like your help, on a computer of mine.
>
> Thank you, truly.
> [name]

[A thank-you, chosen by the owner: say what it is in the note, and give it
whether or not she finishes.]

## 2. The screening call (10 minutes, in plain words)

Read each question as written. Write down the answer. **The answers decide which
computer she uses and what the observer must be ready for.**

| # | Ask | Why (the observer's reason) | Write |
|---|---|---|---|
| 1 | "How old are you, if you don't mind telling me?" | 70 or over; note the band. | |
| 2 | "Do you wear glasses to read, and does your hand shake at all?" | Presbyopia and tremor shape the whole session. Do not make anything of the answer. | |
| 3 | "Is the computer you use a laptop, or does it sit on a desk?" | Laptop: charger. Desktop: cable. | |
| 4 | "When you switch it on, what does it say or show before you see your desktop?" (a logo name) | The maker. A Windows version often shows on the lock screen or in Start. | |
| 5 | "Is there a little shield or a program that protects you from viruses? Which one?" (Norton, McAfee, Avast, AVG, "Windows Security") | The antivirus is the commonest reason the file is held (finding B-02). | |
| 6 | "Which email do you read on this computer? Can you open it on the computer now?" | The link arrives by email (default). If it is on her phone only, use PHONE-LINK. | |
| 7 | "Can you show me a photo on the computer that you'd know anywhere? Of whom?" | The photo for T2, in her words; a family member's name for T3. | |
| 8 | "Who would you call on a video call? How do you call them now?" | T3: the grandson's name, and the app she knows (FaceTime, WhatsApp, Zoom, Messenger). | |
| 9 | "Do you have photos or files you couldn't bear to lose that aren't anywhere else?" | If yes, the backup in `SCRIPT.md` section 7 is not optional: tell her. | |
| 10 | "Are you the person who set up this computer, or does someone else look after it?" | An account that is not an administrator will ask for a password at the Windows permission box (finding B-05). | |

## 3. The check that decides whether her computer is one we can use

When `AurOS-Setup.exe --report` exists (WP-32; **not yet landed**), ask her, or
her trusted person, or yourself on a first visit, to run it and read out the
one-line result. It only looks. It changes nothing. A computer it refuses is
not wasted: use a lab computer for her, and note "refused PC".

Until then, the screening answers are the check. **Do not use, for the real test:**
- a computer that is not Windows 10 or 11, or is 32-bit or ARM;
- one with less than about 50 GB of free room on C: (the Properties page of the
  drive shows it; ask someone to read it to you);
- one with BitLocker or "Device encryption" switched on, **until the pause path
  ships (D13, WP-50)**: Start, then "Device encryption" (Windows 10 Home) or
  "Manage BitLocker" (Pro). If it shows On, use a lab computer.

## 4. After the call

Say exactly what will happen: "I'll bring a drive to copy your photos, and my
laptop. It'll take about two and a half hours. We'll have tea first." Write
the date, who will sit with her, and a number she can ring if she changes her
mind. Never leave a person who has agreed without a time and a phone number.

## 5. Who

Five people for the real test; three for the early check on the owner's
laptop. For both: at least one who uses glasses and has a tremor, one who
is still using Windows 10 at speed, and one who mainly uses a phone (the three
people in `docs/research/people-walkthrough-2026-10.md`, section 1). Not all
from one family or one street.
