#!/usr/bin/env python3
"""wishtest-sounds.py -- the five sounds exist, are valid, short, quiet and not silent."""
import os, struct, subprocess, sys, tempfile, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAMES = ["chime", "bell", "birds", "applause", "gentle"]
fails = 0
d = tempfile.mkdtemp(prefix="wishsnd-")
subprocess.run([sys.executable, os.path.join(ROOT, "src/aurwish/mksounds.py"), d], check=True)
for n in NAMES:
    p = os.path.join(d, n + ".wav")
    try:
        with wave.open(p) as w:
            frames = w.readframes(w.getnframes())
            ch, sw, rate, nf = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    except Exception as e:
        print("FAIL", n, e)
        fails += 1
        continue
    s = struct.unpack("<%dh" % nf, frames)
    peak = max(abs(x) for x in s) / 32767
    secs = nf / rate
    ok = ch == 1 and sw == 2 and rate == 22050 and 0.8 <= secs <= 2.5 and 0.3 <= peak <= 0.5 and \
        os.path.getsize(p) < 120000
    if not ok:
        print(f"FAIL {n}: ch={ch} width={sw} rate={rate} secs={secs:.2f} peak={peak:.2f} bytes={os.path.getsize(p)}")
        fails += 1
# deterministic: a second run writes the same bytes
d2 = tempfile.mkdtemp(prefix="wishsnd-")
subprocess.run([sys.executable, os.path.join(ROOT, "src/aurwish/mksounds.py"), d2], check=True)
for n in NAMES:
    if open(os.path.join(d, n + ".wav"), "rb").read() != open(os.path.join(d2, n + ".wav"), "rb").read():
        print("FAIL", n, "is not deterministic")
        fails += 1
print(f"wishtest-sounds: {len(NAMES)} sounds, {fails} problem(s)")
sys.exit(1 if fails else 0)
