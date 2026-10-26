#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Octave-band envelope error between a reference WAV and a test WAV (mono mix, 50 ms frames, 0.1-8 s).
   analyze_wav.py REF.wav TEST.wav [--json OUT]"""
import sys, json
import numpy as np
import bakelib as B
ref = B.read_wav_float(sys.argv[1])[1].mean(1)
tst = B.read_wav_float(sys.argv[2])[1].mean(1)
n = min(len(ref), len(tst)); ref, tst = ref[:n], tst[:n]
e = B.octave_envelope_error(ref, tst)
rel = np.sqrt(((ref - tst) ** 2).sum() / (ref ** 2).sum())
out = dict(rel_rms_db=20 * np.log10(rel), bands={str(int(c)): dict(rms_db=v[0], mean_db=v[1], frames=v[2]) for c, v in e.items()})
valid = [v[0] for v in e.values() if v[2] > 0]
out['band_rms_db_mean'] = float(np.mean(valid)); out['band_rms_db_max'] = float(np.max(valid))
print(json.dumps(out))
if '--json' in sys.argv: json.dump(out, open(sys.argv[sys.argv.index('--json') + 1], 'w'), indent=1)
