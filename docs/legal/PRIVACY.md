# AurOS Privacy

Version 3, 23 October 2026. Written by the person who makes AurOS, with the help of AI software, and without a lawyer. This page is not legal advice.

Who is responsible: the person who makes AurOS, on the AurOS project page on GitHub. There is no company yet. To ask us anything about privacy, write to CONTACT_EMAIL.

## 1. In short

- AurOS does not collect information about you or your computer. There is no account, no sign-in and no tracking on the website, in AurOS Setup, or on your computer, and AurOS shows no adverts of its own. (If you write to us, we keep what you send: section 5.)
- AurOS Setup and AurOS send nothing about you or your computer to us. Your files, your passwords and what you do stay on your computer.
- To work, your computer does talk to **other organizations' servers**: GitHub, Ubuntu, Mozilla, Flathub, Google's Safe Browsing and others (section 4). They see what any website you visit sees, such as your internet address. We don't control their records, and each has its own privacy policy.
- If we ever add something that sends anything about you or your computer to us, we will say so on this page **before** the update that does it.

## 2. The website

The website uses no cookies, no analytics and no tracking, and it loads nothing from any other server: its fonts and pictures come from the same place as its pages.

It is hosted by **GitHub Pages**. GitHub, Inc. (owned by Microsoft) receives your internet (IP) address and details about your browser when you visit, and keeps them in its own records, under GitHub's privacy statement. We don't receive those records.

If the website, the downloads or the updates are ever served from a web address we own, the visit will pass through **Cloudflare**, which carries it to GitHub. Cloudflare sees internet addresses like any web host and keeps its own records under its own policy. We will count downloads there without keeping anyone's address.

## 3. AurOS Setup (AurOS-Setup.exe)

AurOS Setup runs on your Windows computer. To decide whether AurOS can go on it safely, it **looks at your computer**: the Windows version, the kind of firmware, how the drive is divided and how much room is free, whether the drive is locked, the drive's health, the name of the Wi-Fi network you are connected to, the printers, camera and Bluetooth devices attached, your language, keyboard and time zone, and similar facts. **It looks at these on your computer and does not send them anywhere.** If you press **Save a report for a helper**, Setup writes two files, AurOS-report.txt and AurOS-report.json, on your Desktop. They list your computer's maker and model, its parts and the names of your printers and other devices, not your Wi-Fi name. Setup sends them nowhere. If Windows backs up your Desktop (OneDrive often does), Windows may copy them there. They go to a helper only if you give them.

As soon as Setup finds that AurOS can go on your computer, it starts downloading AurOS (about 2 GB at the time of writing, and on the version offered today about 5 GB more when it is unpacked) from **GitHub Releases**, while you read the pages and before you press Install AurOS. GitHub sees your internet address, as it does for any download. The download goes into a folder called AurOS on your Windows drive. If you close Setup before you press Install AurOS, Setup stops the download and normally deletes it; if Setup is stopped some other way, for example by a power cut, part of it may stay there, and you may delete the folder.

While it works, Setup writes notes on your computer so that it can finish, or undo, the install after the restart. They stay on your computer. We see them only if you send them to us yourself.

## 4. AurOS on your computer

AurOS doesn't send us usage numbers, crash reports or a list of your programs. Ubuntu's usage counter ("popularity contest") is left out of AurOS on purpose. Ubuntu's paid "Ubuntu Pro" program is still on the computer, because other software needs it, but it is switched off, and so are Ubuntu's daily news messages.

To keep your computer working and safe, AurOS and the programs it comes with mainly connect to the services below while you are online. Each sees your internet address and what is being downloaded.

| Service | Run by | What AurOS uses it for |
|---|---|---|
| Ubuntu's software archive | Canonical Ltd | Security fixes for the system, and the software list |
| Mozilla's software archive | Mozilla (for some versions of AurOS, the Mozilla Team's archive on Launchpad, which Canonical runs) | Firefox and its updates |
| Firefox's own services | Mozilla | Add-ons (including uBlock Origin, from addons.mozilla.org), block lists and checking whether you are online. Firefox's usage reports (telemetry) are switched off. |
| uBlock Origin's lists | The list makers (several are hosted on GitHub) | Keeping the ad and scam block lists up to date |
| Safe Browsing, inside Firefox | Google | Warns before a dangerous website or download. Firefox checks addresses against a list it downloads. For some downloads it sends the file's web address, a fingerprint of the file and similar details (not the file itself) to Google to check it. This check is always on in AurOS's Firefox. |
| The search box in Firefox | The search engine Firefox is set to (Google, unless you change it) | Firefox sends what you type, and suggestions for it, to that search engine. AurOS does not change Firefox's search engine. |
| Flathub | Flathub, with the GNOME Foundation and partners | The catalog and downloads in **Get more programs**. The store may also ask the GNOME project's review service for ratings. |
| AurOS's update channel | GitHub, Inc. (GitHub Releases) | AurOS's own updates |
| Time servers | Ubuntu, the NTP pool, Cloudflare, Netnod, PTB | Keeping the clock right |

This list comes from how AurOS is built. We have not yet watched a running computer to see everything it contacts, so a small helper service may be missing from it. A test that watches is planned, and this list will be checked against it.

**Programs you add** through Get more programs, and **websites you visit**, have their own privacy policies. We don't control what they collect.

**Copying your Windows files.** If a version of AurOS offers to copy your documents, pictures and browser bookmarks from Windows, that copying happens entirely on your computer, from one part of the drive to another. Nothing is uploaded. Passwords saved in your Windows browser are never copied.

## 5. If you write to us

If you write to us, or open an issue on the AurOS project page on GitHub, we see what you write and the name you use there. Issues on GitHub are **public and stay there**, so never put passwords, recovery keys, private files or photos of your screen with private things on it in an issue. We use what you send only to answer you and to fix AurOS. We keep emails for as long as we need them to help you and fix AurOS, normally no more than 12 months, and then delete them. A report about a problem is kept for as long as it takes to fix the problem and to meet our legal duties.

## 6. Children

AurOS is for adults. It is not aimed at children, and it collects nothing from anyone.

## 7. Your rights

Because AurOS does not collect information about the people who use it, there is usually nothing for us to show, correct or delete. If you think we hold something about you, for example an email you sent, write to CONTACT_EMAIL. We will tell you what we hold and, if you wish, delete it, normally within 30 days. If you live where the law gives you more rights over information about you, for example in the United Kingdom or the European Union, you can use them the same way, and you can complain to the data protection authority where you live. AurOS is made for people in the United States for now.

## 8. Changes

If this page changes, the new version will be on the website with a new date and number, before the change takes effect. If AurOS ever starts to send anything new about you or your computer to anyone, we will say so on this page **before** the update that does it.
