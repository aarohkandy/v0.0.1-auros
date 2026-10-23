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
| 640x360 H.264/AAC MP4 | 243 | 93 | 37 % | out of the decoder |
| 640x360 DASH (Media Source) | 240 | 92 | 42 % | out of the decoder |
| 1280x720 H.264/AAC MP4 | 288 | 110 | 21 % | out of the decoder |

(CI run 40005389067, image of build 956af4a. Earlier runs on homebase: 3 to 25 dropped of ~250. The numbers move a lot
from run to run because the runner shares its CPU.)

## Reading them honestly

- The plan's bar is "fewer than 5 % dropped frames and under 80 % CPU on a 2013 laptop". These numbers are a 2024 server core
  in software mode, so they say the decode works and costs about a third of four cores; they do not say a 2013 laptop passes.
  The 2013-laptop number needs the owner's lab PC (WP-33) or a CPU-throttled VM; the harness is ready for it (`--smp 2`, a
  `cpulimit`ed qemu).
- 90 to 110 dropped frames out of ~250 in the VM is far above the bar. Part of it is software drawing of a window the compositor
  scales; the shell and Firefox take turns on the same cores. Check again after the compositor work (stream WL) and with
  `--smp 2`.
- Widevine/Netflix is not tested: it needs the Widevine CDM (a download from Google, `media.gmp-widevinecdm`) and an
  `EncryptedMediaExtensions` policy decision; v0.2 (WP-56).
