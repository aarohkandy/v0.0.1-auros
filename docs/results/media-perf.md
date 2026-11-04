# Media performance (stream PARITY, check (b))

Proof: `tools/parity/guest/checks/film.sh` (nightly, `parity.yml`, `--opt perf`). Test media are generated once with ffmpeg
(`testsrc2` picture plus a 440 Hz tone, H.264 High + AAC-LC) and committed under `tools/parity/assets/`: a 15 s 640x360 MP4,
a 10 s 1280x720 MP4, and a 15 s DASH stream (two video sizes and an audio track, 2 s fragmented-MP4 segments) played through
Media Source by a 40-line page (`guest/www/film.html`), which is what YouTube-style sites do.

## What is measured

Firefox in the booted image plays 9 s of each film in a 4-vCPU KVM guest with NO GPU (the compositor and Firefox both draw
in software). `getVideoPlaybackQuality()` gives frames shown and dropped; the sound is what comes out of the decoder
(Web Audio analyser peak above silence); CPU is the whole machine's use over 6 s of playback from `/proc/stat`.

| film | frames | dropped | machine CPU | sound |
|---|---|---|---|---|
| 640x360 H.264/AAC MP4 | 234 | 28 | 10 % | out of the decoder |
| 640x360 DASH (Media Source) | 233 | 26 | 7 % | out of the decoder |
| 1280x720 H.264/AAC MP4 | 280 | 46 | 6 % | out of the decoder |

(CI run 40015793857, build 2e04025, with the first-run card and Firefox's Terms-of-Use window dismissed so the film really is
on screen: `film.*-visible` pass with 56,000 saturated pixels in 6 hues. Earlier numbers, taken while a card covered the
desktop or the compositor still dropped subsurfaces, were 90-150 dropped of ~250 and are not comparable. Run to run the
dropped count moves by tens: the runner shares its CPU.)

## Reading them honestly

- The plan's bar is "fewer than 5 % dropped frames and under 80 % CPU on a 2013 laptop". These numbers are a 2024 server core
  in software mode, so they say the decode works and costs about a third of four cores; they do not say a 2013 laptop passes.
  The 2013-laptop number needs the owner's lab PC (WP-33) or a CPU-throttled VM; the harness is ready for it (`--smp 2`, a
  `cpulimit`ed qemu).
- 26 to 46 dropped frames out of 233 to 280 (11 to 16 %) is above the 5 % bar even on a fast core; the machine is 90 % idle,
  so frames are lost to scheduling and to software drawing, not to decoding. Check with `--smp 2` and a CPU limit, and
  against the compositor's frame callbacks (stream WL).
- Widevine/Netflix is not tested: it needs the Widevine CDM (a download from Google, `media.gmp-widevinecdm`) and an
  `EncryptedMediaExtensions` policy decision; v0.2 (WP-56).

## The nightlies since (2026-11-04: all seven `parity` runs, `gh release view parity-<run>`)

Frames shown / dropped in 9 s, the same three films. Same method; each night is a different runner.

| night (parity run) | build | 360p MP4 | 360p DASH | 720p MP4 |
|---|---|---|---|---|
| 40176517727 (10-25) | 40148012698 | 249 / 113 | 251 / 73 | 292 / 152 |
| 40295632841 (10-26) | 40148012698 | 244 / 126 | 228 / 57 | 282 / 137 |
| 40477392945 (10-27) | 40148012698 | 237 / 114 | 232 / 95 | 280 / 121 |
| 40665951771 (10-28) | 40148012698 | 242 / 123 | 239 / 124 | 281 / 147 |
| 41434074065 (11-02) | 41329982398 | 233 / **0** | 231 / 39 | 280 / **2** |
| 41622813565 (11-03) | 41591240360 | 233 / 122 | 251 / 131 | 288 / 177 |
| 41817427536 (11-04) | 41780979384 | 242 / 117 | 245 / 118 | 288 / 138 |

Six nights of seven drop between a quarter and 61 % of the frames, the same build four nights running; one night dropped none.
The cause of the clean night is not found. Only two commits touched the compositor or the shell between its build and the
next (the night light of the Wishes, 10-24, which v0.1 does not ship), and the first four nights, whose build predates
them, drop as many frames as the last two. Treat the runner as the first suspect and the older "11 to 16 %" above as the best
case, not the usual one. Sound was out of the decoder every night. Nobody has a number for a real old laptop with a
graphics chip (WP-33): until then "YouTube plays smoothly" is **unproven**, and "YouTube has sound" is proven only as far as
the decoder.
