# AurOS Terms

Version 3, 23 October 2026. Written by the person who makes AurOS, with the help of AI software, and without a lawyer. These Terms are not legal advice. If anything here is unclear, write to us before you use AurOS: CONTACT_EMAIL.

**Plain version:** AurOS is free and has no guarantee. Changing a drive can go wrong, so copy what you can't replace to a memory stick first. We will never phone you or ask for card numbers, passwords or gift cards. The rest is detail.

These Terms are about the AurOS website, the Windows program **AurOS Setup** (the file AurOS-Setup.exe), and the AurOS system it puts on your computer, with its updates. We call all of that "AurOS". "We" is the person who makes AurOS: the owner of the AurOS project on GitHub, whose account is named on the AurOS project page. There is no company yet.

**The programs inside AurOS are not covered by these Terms.** Each one has its own license (section 3), and those licenses always win over these Terms.

## 1. In short

- AurOS itself is free. If anything for sale is ever added, its price will be shown clearly before you pay for anything.
- AurOS changes how your computer's drive is divided. It is made to keep Windows and your files and to let you go back, but no change to a drive is free of risk. **Copy anything you can't replace to a memory stick first.**
- AurOS is new. So far it has been tried only on pretend computers, not on a real one (the Disclaimer says more).
- AurOS comes as it is, without a guarantee (section 6).
- We will never phone you, and nobody from AurOS will ever ask to connect to your computer (section 9).
- AurOS is in English and made for people in the United States for now.

## 2. Who may use AurOS

You may put AurOS on a computer if you are allowed to change that computer: it is yours, or its owner has said yes. If someone is helping you, they must have the owner's permission to change this computer. Pressing Install on a computer that is not yours, or without its owner agreeing, is not allowed. If you press **Install AurOS** for someone else, you are telling us that they have read these Terms and agree to them.

AurOS is for adults. If you are under 18, ask a parent or guardian to help you, and have their permission.

## 3. The programs inside AurOS

AurOS's own programs are free software under the MIT license. AurOS also includes many programs made by other people, for example Firefox and the Linux system underneath, most of them from Ubuntu. Each one has its own license. You are free to use and share almost all of them, and most can be changed too. The page **Licenses and source code** says where to find each license and how to get the source code.

Nothing in these Terms limits the rights those licenses give you. If anything in these Terms disagrees with the license of a program, the license wins.

Some computer parts need small programs from the company that made them (firmware). Those have their maker's own license, and a copy is on your computer in the folder /usr/share/doc, under the name of the program that carries it. Those licenses are between you and the maker.

Some ways of playing video, music and photos are covered by patents in some countries (a patent is a company's claim on a way of doing something). AurOS includes programs that do this. The MIT license gives no patent license, and these programs come without a guarantee.

AurOS includes security software that some countries control when it is sent abroad. You are responsible for following the rules of your country.

## 4. What AurOS does to your computer

**While you read.** AurOS Setup looks at your computer. If it finds that AurOS can go on it, Setup starts downloading AurOS onto your computer's drive straight away, before you press anything: a large download, about 2 GB at the time of writing, from the AurOS releases page on GitHub. It checks that the download was not changed on the way. **If your internet has a data limit, or is slow or metered, close Setup now.** The download goes into a folder called AurOS on your Windows drive. If you close Setup before you press **Install AurOS**, Setup deletes what it downloaded. If Setup is stopped some other way, for example by a power cut, part of the download may stay in that folder; you may delete the folder. If Setup finds AurOS can't go on, it says so and does not download anything.

**When you press Install AurOS,** Setup, and after the restart AurOS's own start-up program:

- turn off Windows' Fast Startup and hibernation (the settings Windows uses to start faster). They stay off while AurOS is on your computer, and are put back if the install is taken back or AurOS is removed;
- make Windows smaller to free room for AurOS, and move Windows' files to do it. Depending on your computer, this happens while Setup runs or just after the restart. AurOS needs at least 28 GB of empty room and takes up to 40 GB. Setup tells you if there is not enough;
- put AurOS's start-up files in the small part of your drive that the computer reads first when it starts, and add a start-up entry, so that your computer starts AurOS first;
- leave a small Windows task called "AurOS put-back", with its files in the folder C:\ProgramData\AurOS. It runs as an administrator every time Windows starts. It does nothing until AurOS is removed. Then it gives drive C: its room back and saves in Windows the files you kept in AurOS;
- restart your computer once.

**The risk, plainly.** Making Windows smaller is the one step that cannot be undone half way. If the power fails or the computer crashes during it, Windows or your files could be damaged and might not come back. Plug the computer in, and copy anything you can't replace to a memory stick first. If your computer will not start afterwards, ask someone who knows computers to help you. They may need a second computer.

**After the restart.** Until you answer **Yes, it all works** on AurOS's first screen, Windows and your files stay on your computer. **No, go back to Windows** makes Windows start the next time. AurOS stays on the computer, about 28 GB of it, with anything you saved in it, until you remove it with **Put Windows back** in AurOS's Settings or have someone who knows computers do it. Put Windows back checks first, and says so if it can't. It also changes the drive, so the advice above applies to it too: copy anything you can't replace first. After **Yes**, AurOS starts every time.

**Things AurOS has not been tested with:** your computer maker's recovery tools, Windows' own "reset" option, and your computer's warranty. Ask the maker if that matters to you.

**Updates.** AurOS installs its updates on its own, without asking you, while your computer is online (section 8).

Using AurOS, including pressing **Install AurOS**, means you accept these Terms. If you don't accept them, don't use AurOS.

## 5. Your part

- **Keep copies of anything important**, such as photos and papers, somewhere else, for example on a memory stick. Do this before you install AurOS and keep doing it. It is good practice with any computer.
- Keep the computer plugged in while AurOS is being installed.
- Please use AurOS lawfully, and don't use it to get into other people's computers or accounts.
- Programs you add through **Get more programs** are made by other people, mostly from the Flathub catalog. Their own terms apply to them. We don't make them or check them.

## 6. No guarantee

AUROS IS PROVIDED FREE OF CHARGE, "AS IS" AND "AS AVAILABLE", WITHOUT WARRANTY OF ANY KIND, EITHER STATED OR IMPLIED, INCLUDING THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. WE DO NOT PROMISE THAT AUROS WILL WORK ON YOUR COMPUTER, WILL BE FREE OF FAULTS, OR WILL KEEP WORKING.

Nothing in these Terms affects consumer rights that cannot be excluded where you live.

## 7. Limits on our responsibility

TO THE EXTENT THE LAW ALLOWS, WE ARE NOT RESPONSIBLE FOR LOSS OF DATA, LOSS OF USE OF YOUR COMPUTER, OR ANY OTHER LOSS OR DAMAGE THAT COMES FROM INSTALLING, USING OR REMOVING AUROS, INCLUDING IF SOMETHING GOES WRONG WHILE YOUR DRIVE IS BEING CHANGED.

Nothing in these Terms limits our responsibility where the law does not let it be limited. That includes death or personal injury caused by negligence, fraud, gross negligence and willful misconduct.

## 8. Changes and stopping

While your computer is online, AurOS installs updates on its own, without asking you, as long as we keep AurOS going: security fixes and other changes from Ubuntu, Mozilla's Firefox updates, updates to the programs you added through Get more programs, and AurOS's own updates, which can change the desktop and the start-up files. Updates do not restart your computer by themselves. Fixes for some programs outside Ubuntu's main collection come more slowly, or not at all. We may change or stop AurOS, or these Terms. We will try to give at least 90 days' notice before we stop, and if we can't, we will say so as soon as we can. Changing or stopping AurOS does not end the offer of source code (see the page Licenses and source code), and it does not take away any right that the licenses of its programs give you.

If we change these Terms, we will say so on the website. Changes apply to later installs. We do not change the Terms of an install you have already done. A copy of these Terms is on your computer, in the folder /usr/share/doc/auros.

## 9. AurOS will never call you

We do not phone people. Nobody from AurOS will ever ask to connect to your computer, or ask you to call a number. We will never ask you for passwords, bank details, card numbers or gift cards, by phone, email or text. Anyone who does is **not** from AurOS. Please tell us about it.

## 10. Names

"AurOS" is the name of this project.

AurOS is built from Ubuntu 24.04 LTS software that we have put together and changed. It is not made, endorsed or supported by Canonical.

Windows, Microsoft, Edge, BitLocker, Defender and SmartScreen are trademarks of the Microsoft group of companies. Mac and macOS are trademarks of Apple Inc. Google Chrome and Safe Browsing are trademarks of Google LLC. Ubuntu is a registered trademark of Canonical Ltd. Firefox is a trademark of the Mozilla Foundation. Linux is the registered trademark of Linus Torvalds in the U.S. and other countries. Intel is a trademark of Intel Corporation or its subsidiaries. Other names are trademarks of their owners. AurOS is not made, approved or supported by any of them, and has not been authorized, sponsored, or otherwise approved by Apple Inc. The names are used only to say what a program is, what AurOS is made from, or what it works with.

## 11. Ending

You can stop using AurOS at any time, and **Put Windows back** removes it (and changes the drive again: see section 4). We may stop offering AurOS at any time (section 8). Sections 6, 7 and 10 stay in force after you stop.

## 12. Problems

If you have a problem with AurOS, write to us first at CONTACT_EMAIL and we will try to put it right. These Terms do not take away any right you have by law where you live.

## Contact

CONTACT_EMAIL. For help using AurOS, see the help on the website. To report a security problem, see the security page on the AurOS project page.
