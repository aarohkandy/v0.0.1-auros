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
