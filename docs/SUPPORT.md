# Getting help with AurOS

AurOS is free and made by one person with the help of AI software. There is **no phone line**, and
nobody from AurOS will ever phone you, ask to connect to your computer,
or ask you for card numbers, passwords or gift cards. Anyone who does is
not from AurOS: hang up or close the page. (The in-computer help pages
"Someone phoned about my computer" and "A page says my computer is
infected" say the same thing, in more detail.)

## 1. On the computer itself

Press **Help** at the bottom-left of the screen. It is always there, in
every look. It explains the bar at the bottom, and its button **Show me
how to do things** opens the step-by-step help pages, which are on the
computer and work without the internet:

Each page answers one question, in her words, and is no longer than two
screens:

- Getting on the internet (Wi-Fi, a new home, a cable)
- My pictures and my Windows files
- Music, letters and email
- Getting a new program
- Printing
- Video calls
- Bigger words, louder sound
- Turning it off
- It is stuck, frozen or dark
- A page says my computer is infected
- Someone phoned about my computer
- Going back to Windows (and, under it, removing AurOS, and what the
  "Does everything work?" question does)
- I forgot my PIN
- Who do I ask?
- Licenses and the source code

Every page ends with a link to "Who do I ask?". The pages cannot be opened
before AurOS has started, so the PIN screen has its own buttons; the
"I forgot my PIN" page gives the same steps.

The pages name no web address on purpose: they are copied into the
image as they are, and an address written into them would go stale the
day the project moves. `tools/helptest.py` keeps every one of these
questions answered, and checks the buttons and sentences the pages name
against the desktop's own code.

## 2. On the website

- The front page, with pictures of every step of putting AurOS on a
  computer: <https://1os.vercel.app/>
- A page to print for whoever helps you:
  <https://1os.vercel.app/guide.html>
- For someone who knows computers (when the installer says AurOS can't
  go on a computer because of how it starts, or Intel's storage setting):
  <https://1os.vercel.app/advanced.html>

## 3. Asking us

Open an issue on the project page:
<https://github.com/aarohkandy/v0.0.1-auros/issues>
(it needs a free GitHub account; a family member or friend can do it
for you).

Please say:

- what you were trying to do, and what happened instead, in your own words;
- which step it was (the download, the installer, the restart, the
  first start of AurOS, or using AurOS);
- the computer's make and model, if you know it (it is often on a sticker
  underneath or on the back);
- a photo of the screen, taken with a phone, helps a lot.

**Never put passwords, recovery keys, bank details or private files in
an issue.** Issues are public: anyone can read them.

Answers come from volunteers, usually within a few days. There is no
guaranteed response time.

## 4. Something that cannot wait

- **Your files matter more than AurOS.** If anything looks wrong with
  the drive (the installer says the drive is failing, or the computer
  becomes very slow and makes clicking noises), copy your photos and
  documents to a memory stick first, then ask.
- **To get Windows back**, see the help page "Going back to Windows":
  Settings → **Put Windows back**, or choose **Windows** in the short list
  that appears when the computer switches on.
- **If you think someone has got into your computer or your accounts**:
  switch the computer off, call your bank using the number on your card,
  and change your passwords from another device. Then ask someone you
  trust to help.

## 5. Security problems

If you have found a way AurOS could be used to harm people, please
don't open a public issue: see [SECURITY.md](../SECURITY.md).

---

> **Maintainers:** the promises on this page (no phone line, issues as
> the channel, "a few days") must stay true. If a paid support service,
> an email address or a forum is added, update this page, the help pages
> `rootfs/usr/share/auros/help/never-call.html` and `ask-someone.html`, and
> `website/advanced.html` together.
