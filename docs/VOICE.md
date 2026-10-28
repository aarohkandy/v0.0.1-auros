# Voice: how she is heard, and how the computer answers

Stream W2. Code: `src/aurwish/voice.c` (the `aurvoice` program), the client in
`src/aurshell/wish.c`. Tests: `tools/voicetest.sh`. Measuring: `tools/voicebench.sh`.
The contract is `docs/AI.md` section 4 and section 7 change 9: tap to start, tap to stop,
at most 30 seconds, no wake word, audio in memory only, and the text box always works.

## What is and is not known

Everything below was measured on a build server, with synthetic speakers. Two numbers
the owner needs are **not** known yet, and this page says so rather than guess:

1. **Seconds per sentence on a real 2013-class machine.** The server is a fast modern
   core that was also busy (load average 20 to 30) while measuring, so the figures are in
   CPU seconds of one thread, which is steadier than the clock. A 2013 laptop core is
   perhaps two to four times slower than this. The number to trust is the one from
   `tools/voicebench.sh` run on such a laptop (below).
2. **How well it understands a real elderly voice.** The test clips are piper's synthetic
   voices (clean, then slowed, thinned and noised to stand in for a slow, quiet speaker in
   a room). They are *not* an elderly person. Accuracy on a real one can only come from
   real recordings (below).

Until those two are measured, "acceptable" in this page means "acceptable on stand-ins".

## Which recogniser

whisper.cpp v1.9.5 (commit `d1be6fde11ac6e0407606b4e42fe72d34add8037`), English-only
models from `ggerganov/whisper.cpp` on Hugging Face. Run as a command-line program on a
WAV that lives only in an anonymous memory file; no network is opened.

| model (8-bit) | file | RAM at work | CPU seconds per 2 s clip, AVX2 core | on the baseline build (no AVX) | word error, stand-ins |
|---|---|---|---|---|---|
| tiny.en | 44 MB | 120 MB | not measured | 1.8 s (with the speed-up below) | 1.3 % clean, 6.7 % slow, 12 % quiet and noisy |
| base.en | 82 MB | 190 to 216 MB | 2.2 s | 3.8 s (with the speed-up) | 1.3 %, 0 %, 12 % |
| small.en | 264 MB | 530 MB | 6.7 s | not worth it | 1.3 %, 0 %, 5.3 % |

What was chosen, and why:

- **Where the processor has AVX2 and FMA: `base.en` 8-bit with the AVX2 build.** About
  2 s a sentence on a modern core, 82 MB, 200 MB of RAM. `small` is better in noise
  (5 % against 12 %) but 3 times slower and 2.5 times the memory; that trade is not worth
  making blind.
- **Everywhere else: `tiny.en` 8-bit with the baseline build** (built with no AVX, no FMA,
  so it runs on anything made since 2008). It is the only one that answers in a few seconds
  on an old processor.
- **8-bit, not 5-bit.** The 5-bit models are smaller, but on a processor without AVX they
  are *slower*: tiny 5-bit took 10 CPU seconds a clip on the baseline build, tiny 8-bit
  about 4. Bytes saved are not worth four times the waiting.
- **The speed-up (`-ac 750`).** whisper pads every clip to a 30 second window. Telling it
  to look at about 15 seconds (`-ac 750`) roughly halves the time. Shorter is a trap: a
  window of "the clip plus two seconds" or `-ac 500` made tiny get 13 to 29 % of words
  wrong. 750 cost nothing on base, nothing on tiny's normal-pace clips, and took tiny's
  slow-pace clips from 0 % to 6.7 % wrong. `aurvoice` uses it only when the recording is 14 seconds or less, and lets
  whisper use its full window for the longer ones.
- **Which build runs is decided at run time** from `/proc/cpuinfo`: AVX2 and FMA present
  means `whisper-cli-avx2` and the larger model; anything else, or the AVX2 files missing,
  means the baseline pair. `AURVOICE_*` macros set the paths (the test points them at
  stand-ins).
- **Silence and hallucination.** whisper invents "Thank you." and "[BLANK_AUDIO]" from a
  quiet room. `aurvoice` answers `unsure` for those, for under 0.4 seconds of sound, and
  for near-silence, and the panel asks her to say it again or type it. A real "Thank you."
  said out loud is believed. (All three are tested.)

If the owner's recording session finds tiny too weak for the oldest voices, the two
levers are: use `base` everywhere (it is 2 times the time of tiny on the same processor)
or ship `small` as an optional download for machines that can take it.

## Which voice speaks the answers

Piper (rhasspy release 2023.11.14-2, onnxruntime 1.14.1, 52 MB with its libraries) with a
medium voice, and espeak-ng if piper or its voice is not there. Measured with six
sentences of a wish confirmation:

| voice | file | RAM | CPU seconds per second of speech | licence |
|---|---|---|---|---|
| **en_US ljspeech medium** (chosen) | 64 MB | 251 MB | 0.42 | public domain |
| en_GB alba medium | 63 MB | 229 MB | 0.36 | CC BY 4.0 (attribution required) |
| en_US lessac low | 63 MB | 189 MB | 0.29 | Blizzard 2013 licence (restrictive) |
| en_US ryan low | 63 MB | not run | not run | CC BY-NC-SA (non-commercial: unusable) |
| espeak-ng | 0.8 MB | 8 MB | about 0 | GPL, a system package |

- **ljspeech** is chosen because its dataset is public domain: nothing to attribute,
  nothing that can be withdrawn, and the owner may sell the product. It is a plain,
  clear American woman's voice. alba is a little warmer and faster, but its licence needs
  an attribution line somewhere she can read it.
- **espeak-ng** is the fallback and is already in the image's package set. It is
  immediate and unmistakably a machine. It is for the case where piper is not installed
  yet, not a choice.
- Quality was judged by listening to the synthesised clips on the server, so it is a
  judgement from one listener, not a study. The owner should listen to the ljspeech and
  alba clips and pick (`docs/shots/wishes/` has none; run `piper --model ... --output_file`).
- On an old machine speaking runs at roughly speech length times 0.4 to 1.5. The panel
  shows the words first and speaks second, and "Read it to me" is a button, so a slow
  voice costs a pause, not a failure.

Everything speaks only text the engine wrote (`docs/AI.md` section 7), never the cloud's.
`aurvoice say` takes the text as one argument and hands it to piper on stdin, so nothing
she or the cloud says can become a command (tested).

## Putting it in the image (needs the owner, forge and release)

`aurvoice` does not download anything. The files it looks for:

| path | what | size |
|---|---|---|
| `/usr/lib/auros/whisper-cli` | baseline build (no AVX/FMA) | 2.9 MB |
| `/usr/lib/auros/whisper-cli-avx2` | the AVX2 build | 3.3 MB |
| `/usr/share/auros/voice/ggml-tiny.en-q8_0.bin` | small model | 44 MB |
| `/usr/share/auros/voice/ggml-base.en-q8_0.bin` | larger model | 82 MB |
| `/usr/lib/auros/piper/piper` (with its `.so` files and `espeak-ng-data`) | the voice program | 52 MB |
| `/usr/share/auros/voice/en_US-ljspeech-medium.onnx` and `.onnx.json` | the voice | 64 MB |

That is about 250 MB in total. Wishes are not in the v0.1 image (decision D14), so none of
it is needed there. Two ways when they are:

1. **Carried in the image** (+250 MB, works on the first boot, no internet needed to
   talk). The image build must then compile whisper.cpp twice, fetch the models and the
   voice, and check each against the hashes below.
2. **Fetched the first time she touches the microphone** (+0 MB on the stick), after
   asking her in a plain sentence, from the owner's own mirror (not Hugging Face at run
   time), checked against the same hashes, into `/var/lib/auros/voice/`. This needs a
   mirror and a small downloader (W2 has not written one: it is a network feature and
   belongs with the update stream). Until it exists, the microphone is hidden when the
   files are not there and the text box is all she sees.

Either way the pinned sources are:

| piece | source | sha256 |
|---|---|---|
| whisper.cpp | `github.com/ggml-org/whisper.cpp` tag v1.9.5, commit `d1be6fde11ac6e0407606b4e42fe72d34add8037` | build from source; baseline: `cmake -DGGML_NATIVE=OFF -DGGML_AVX=OFF -DGGML_AVX2=OFF -DGGML_FMA=OFF -DGGML_F16C=OFF -DGGML_BMI2=OFF -DGGML_AVX512=OFF -DBUILD_SHARED_LIBS=OFF`; AVX2: `-DGGML_NATIVE=OFF` only |
| tiny.en 8-bit | `huggingface.co/ggerganov/whisper.cpp` `ggml-tiny.en-q8_0.bin` | `5bc2b3860aa151a4c6e7bb095e1fcce7cf12c7b020ca08dcec0c6d018bb7dd94` |
| base.en 8-bit | the same repository, `ggml-base.en-q8_0.bin` | `a4d4a0768075e13cfd7e19df3ae2dbc4a68d37d36a7dad45e8410c9a34f8c87e` |
| piper | `github.com/rhasspy/piper` release 2023.11.14-2, `piper_linux_x86_64.tar.gz` | `a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992` |
| ljspeech voice | `huggingface.co/rhasspy/piper-voices` `en/en_US/ljspeech/medium/` | `.onnx` `6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22`, `.onnx.json` `141d612cc0a95ed7efc1ca936b845c2364967f2e9217c5dbfcf69fc4d6c65860` |

The hashes are of the files that were measured. A different file under the same name
means the numbers here need repeating.
Everything downloaded during this work was fetched into its own empty directory. The one
downloaded program that was run, piper, ran in a container with no network; whisper.cpp
was built from its source; models and voices are data.

## Running the real test: a recording session (about an hour)

This is the measurement that decides tiny against base, and whether voice ships on.

1. **Who.** Five or more people who are the product's users: aged 65 and over, a mix of
   voices (quiet, shaky, accented, a cold). Ask each to read these twelve sentences at
   their own pace, sitting where they would use the computer, with the machine's own
   microphone (not a headset) and the fan on.
   - Make the words bigger. / Make the words much bigger.
   - Remind me to take my pills at nine.
   - Fill my screen with potatoes when I type potato.
   - Make the screen brighter. / Make the screen darker in the evening.
   - Play birdsong when I switch on.
   - Say good morning to me at eight.
   - When the battery is low, tell me to plug in the charger.
   - Remind me to call my daughter on Sunday at four.
   - Turn the sound down.
   - Show me a message that says time for tea.
2. **Record** each as 16 kHz mono 16-bit WAV (`arecord -q -f S16_LE -r 16000 -c 1 -d 6 name.wav`).
   Ask first, keep them only on the test laptop, delete them after: they are people's
   voices.
3. **Run on the oldest machine you care about** (a real 2013 laptop, 4 GB, on mains power):

       sh tools/voicebench.sh /usr/lib/auros/whisper-cli /usr/share/auros/voice/ggml-tiny.en-q8_0.bin *.wav
       sh tools/voicebench.sh /usr/lib/auros/whisper-cli /usr/share/auros/voice/ggml-base.en-q8_0.bin *.wav

   (use `whisper-cli-avx2` if the machine has AVX2). It prints, per clip, seconds of
   speech, seconds to understand it, CPU seconds, peak memory and the words heard. Add
   `-ac 750` by editing the script's call to match the product.
4. **Read the words against what was said.** Count a wish as understood only if a person
   would have been happy to be asked "Is this what you meant?" with that text. Decide:
   - 9 in 10 understood for the group with tiny, and under 8 seconds a sentence: ship tiny.
   - Otherwise try base, and if base is under 12 seconds, ship base on that machine.
   - Otherwise voice stays off on that class of machine and the panel shows only the
     text box, which is already the case when the files are absent.
5. Write the three numbers (understood out of 60, median seconds, peak memory) into this
   page, replacing the paragraph "What is and is not known".

## What the tests prove

`tools/voicetest.sh` (run by `tests.yml`) uses stand-in recorders, recognisers, players
and a stand-in piper, and checks: the recording never reaches a named file; cancel kills
the recorder without letting go of her voice; under 0.4 seconds and silence are
`unsure`; the recogniser's `[BLANK_AUDIO]` and a lone "Thank you." from a quiet room are
`unsure`; the model and build are chosen by the processor; a recorder that fails is
`nomic`, a recogniser that fails or hangs is `broken` and is given up on; a WAV header is
not heard as sound; long odd output becomes one clean line of at most 300 bytes;
`say` never runs what it is given, gives piper the words on stdin, hands the player the
voice's own sample rate, falls back to espeak-ng, and speaks at most 300 bytes. It is run
against 17 deliberate breakages of `voice.c`; every one turns it red.

It does **not** prove the recogniser understands anyone. Only the recording session does.
