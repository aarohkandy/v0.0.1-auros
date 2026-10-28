# The sites she uses every day: a campaign sheet for the owner (stream PARITY, WP-56)

A VM cannot sign in to her bank or call her granddaughter. This is the sheet the owner (or a WP-43 recruit) runs on a real
AurOS PC with real accounts, 25 sites in about 90 minutes. Mark each row `ok`, `ok with a note`, or `broken`, add what she
saw, and file `broken` rows as issues (docs/issues). Nothing here sends anything anywhere: use accounts you already own.

Order of difficulty: browser basics first. Do the whole sheet once in Firefox as it ships; if a row is `broken`, repeat it in the
call browser (Flathub Chromium, WP-22) and note whether that fixes it.

| # | Site | Try | Why it might break here |
|---|---|---|---|
| 1 | google.com | search; click a result; open the "Images" tab | none expected |
| 2 | Gmail | sign in, read a message, send one to yourself, open an attachment | 2-step sign-in prompts; PDF/Office attachments open the wrong program (see office.types) |
| 3 | Outlook.com / Hotmail | sign in, read, reply | the same |
| 4 | Yahoo Mail | sign in, read | many ads; pop-up blocker must not hide the sign-in |
| 5 | Facebook | sign in, scroll 20 posts, play a video with sound, post a photo from the Pictures folder | video autoplay with sound is blocked by policy until she presses play; file picker through the portal |
| 6 | YouTube | play a video full screen for 5 minutes; change quality; captions on | H.264/VP9/AV1 decoding load on an old laptop (media-perf.md) |
| 7 | Netflix | sign in, play a film | Widevine DRM: needs the Widevine module; L3 gives 720p at most. Known v0.2 item |
| 8 | Disney+ / Prime Video | play one title | same |
| 9 | Zoom (web client, zoom.us/join) | join a meeting with camera and microphone | camera and microphone permission prompts; the camera light |
| 10 | Google Meet | start a call with a second device | WebRTC; screen share needs the red Stop bar (WP-55) |
| 11 | WhatsApp Web | scan the QR code, send a message and a photo, a voice message | the microphone; photo import from the phone |
| 12 | Facebook Messenger / Skype successor (Teams) | a video call | as 9 |
| 13 | Amazon | search, add to cart (do not buy), view an order | none expected |
| 14 | Walmart / a grocery site | add to a cart, choose a pickup time | address autofill off by policy: she types |
| 15 | eBay | search, view an item with 10 photos | none expected |
| 16 | Her bank (Chase, Bank of America, Wells Fargo, a credit union) | sign in, view a statement PDF, download it, print it | the site may reject an unfamiliar browser; the PDF opens in the viewer; HTTPS-only mode must not block the bank's own http redirect |
| 17 | PayPal / Venmo | sign in, view activity | the same |
| 18 | Medicare.gov | sign in or use "find a plan" | login.gov / ID.me camera step uses WebRTC |
| 19 | Social Security (ssa.gov, my Social Security) | sign in, view a statement | ID.me or login.gov: a photo of her ID with the webcam |
| 20 | IRS.gov | download a fillable form PDF, fill it, save, print | PDF forms: see pdf-forms.md |
| 21 | Google Maps | search a place, street view, directions | WebGL in software mode on an old laptop: speed |
| 22 | Weather (weather.com or her TV station) | the 10-day page | heavy ads |
| 23 | Wikipedia | read a long page, print it to PDF | printing |
| 24 | A news site (CNN, NYT, her local paper) | read three articles | paywall pop-ups, autoplay video; notifications are blocked by policy, which is right |
| 25 | Her photos online (Google Photos or iCloud) | view an album, download one photo, upload one | the file picker; HEIC photos from an iPhone (WP-25 adds the thumbnailer) |

Also try, once: open a PDF from a site (opens in the page), download a .docx (what opens it, WP-25), click a `mailto:` link
(opens her webmail, WP-22), and click "print" on any page (the printer list, driverless printing: see `tools/parity` check (c)).

## Result sheet (copy per machine)

`machine / date / who / Firefox version / sites ok / sites with a note / sites broken / notes`. Results go to
`docs/results/sites-<date>-<machine>.md`; this page keeps only the sheet.
