#!/usr/bin/env python3
"""mksounds.py -- the five sounds a wish may play (docs/AI.md §2.2 play_sound).

    python3 src/aurwish/mksounds.py OUTDIR

Writes chime.wav, bell.wav, birds.wav, applause.wav, gentle.wav: 22.05 kHz,
mono, 16-bit, under two seconds, quiet (peak 0.45 of full scale) and made
from nothing but sines and noise, so there is no recording to license and
nothing to download. build/forge runs it only when the profile says
wishes="on". Deterministic: the same bytes every time.
"""
import math, os, random, struct, sys, wave

RATE = 22050


def write(path, samples):
    peak = max(abs(x) for x in samples) or 1.0
    k = 0.45 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(x * k * 32767)) for x in samples))


def tone(freq, dur, decay, attack=0.01, partials=((1.0, 1.0),)):
    n = int(dur * RATE)
    out = []
    for i in range(n):
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-t * decay)
        out.append(env * sum(a * math.sin(2 * math.pi * freq * m * t) for m, a in partials))
    return out


def mix(parts, total):
    out = [0.0] * int(total * RATE)
    for start, s in parts:
        o = int(start * RATE)
        for i, x in enumerate(s):
            if o + i < len(out):
                out[o + i] += x
    return out


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    rnd = random.Random(1)
    write(os.path.join(outdir, "chime.wav"),
          mix([(0.0, tone(659.3, 1.0, 4.0)), (0.28, tone(880.0, 1.2, 3.5))], 1.5))
    write(os.path.join(outdir, "bell.wav"),
          mix([(0.0, tone(440.0, 1.9, 2.2, 0.003, ((1, 1.0), (2.0, 0.6), (2.76, 0.4), (5.4, 0.25), (8.9, 0.1))))], 2.0))
    chirps = []
    for k, start in enumerate((0.0, 0.34, 0.62, 1.0)):
        n = int(0.13 * RATE)
        ph = 0.0
        s = []
        for i in range(n):
            t = i / n
            f = 2800 + 1800 * t * (1 if k % 2 == 0 else -1) + 140 * math.sin(2 * math.pi * 38 * t)
            ph += 2 * math.pi * f / RATE
            s.append(math.sin(ph) * math.sin(math.pi * t))
        chirps.append((start, s))
    write(os.path.join(outdir, "birds.wav"), mix(chirps, 1.4))
    claps = []
    for _ in range(70):
        start = rnd.random() * 1.5
        n = int(0.05 * RATE)
        claps.append((start, [(rnd.random() * 2 - 1) * math.exp(-i / (0.006 * RATE)) * rnd.uniform(0.3, 1) for i in range(n)]))
    write(os.path.join(outdir, "applause.wav"), mix(claps, 1.8))
    notes = [(0.0, 261.6), (0.3, 329.6), (0.6, 392.0), (0.9, 523.3)]
    write(os.path.join(outdir, "gentle.wav"), mix([(s, tone(f, 1.0, 2.2, 0.12)) for s, f in notes], 1.9))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: mksounds.py OUTDIR")
    main(sys.argv[1])
