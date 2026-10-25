#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""tension-demo-0.5.2.wav: A1 and A2 at fortissimo, each 0.5.1 (no tension) then 0.5.2, level matched.

Needs NumPy. Each clip is the first 4 s of a note (velocity 1.0, key held), scaled so that the RMS of
its 100-400 ms is the same for the pair (a diagnostic match of the initial level only; the later decay
is untouched). Order: A1 0.5.1, A1 0.5.2, A2 0.5.1, A2 0.5.2, 1 s apart. Fixed 24-bit rendering.
"""
import subprocess
import sys
import tempfile
import wave
from pathlib import Path
import numpy as np

FS = 48000
directory = Path(sys.argv[1] if len(sys.argv) > 1 else 'out/piano-mvp-v4').resolve()

def render(key, mode, tmp):
    path = Path(tmp) / f'{key}-{mode}.wav'
    subprocess.run([str(directory / 'piano-render'), str(path), str(key), mode, '1.0', '--24bit'], check=True, stdout=subprocess.DEVNULL)
    raw = path.read_bytes()
    data = raw[raw.find(b'data') + 8:]
    b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
    v = np.where(v >= 1 << 23, v - (1 << 24), v) / 8388608.0
    return v.reshape(-1, 2)[:4 * FS]

clips = []
with tempfile.TemporaryDirectory() as tmp:
    for key in (33, 45):
        a, b = render(key, 'previous', tmp), render(key, 'coupled', tmp)
        rms = lambda x: np.sqrt(np.mean(x[int(.1 * FS):int(.4 * FS)] ** 2))
        target = 0.1
        for x in (a, b):
            x = x * (target / rms(x))
            x[:240] *= np.linspace(0, 1, 240)[:, None]; x[-4800:] *= np.linspace(1, 0, 4800)[:, None]
            clips += [x, np.zeros((FS, 2))]
out = np.concatenate(clips)
assert np.abs(out).max() < 1
with wave.open(str(directory / 'tension-demo-0.5.2.wav'), 'wb') as f:
    f.setparams((2, 3, FS, 0, 'NONE', 'not compressed'))
    q = np.rint(out * 8388607).astype(np.int32) & 0xffffff
    f.writeframes(np.stack([q & 255, (q >> 8) & 255, (q >> 16) & 255], axis=-1).astype(np.uint8).tobytes())
print('wrote', directory / 'tension-demo-0.5.2.wav', len(out) / FS, 's')
