#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reproduce the controlled single-note output-path experiment (NumPy only).

python3 mvp/piano-v3/compare.py out/piano-mvp-v3 [--skip-render]
RMS match over 0.05–3 s to -24 dBFS. No compression or peak normalisation.
"""
from pathlib import Path
import hashlib
import json
import math
import subprocess
import sys
import wave
import numpy as np

def read(path):
    with wave.open(str(path)) as f:
        assert f.getnchannels() == 2 and f.getsampwidth() == 2 and f.getframerate() == 48000
        return np.frombuffer(f.readframes(f.getnframes()),dtype="<i2").reshape(-1,2).astype(float)/32768

def write(path, audio):
    if not np.isfinite(audio).all() or np.abs(audio).max() >= 0.999:
        raise ValueError("refusing clipped or non-finite matched audio")
    with wave.open(str(path),"wb") as f:
        f.setparams((2,2,48000,0,"NONE","not compressed"))
        f.writeframes(np.rint(audio*32767).astype("<i2").tobytes())

def main():
    directory = Path(sys.argv[1] if len(sys.argv)>1 else "out/piano-mvp-v3").resolve()
    results = {"key":60,"velocity":0.75,"release_s":4,"sample_rate":48000,
               "match_window_s":[0.05,3],"target_rms_dbfs":-24,
               "warning":"RMS matching approximates equal level, not equal perceived loudness; no realism score."}
    raw, matched = {}, {}
    window = slice(2400,144000)
    target = 10**(-24/20)
    for mode in ["original","linear","soundboard"]:
        path = directory/f"middle-c-{mode}-raw.wav"
        if "--skip-render" not in sys.argv:
            subprocess.run([str(directory/"piano-render"),str(path),"60",mode],check=True)
        audio = read(path); raw[mode] = audio
        rms = float(np.sqrt(np.mean(audio[window]**2)))
        if rms <= 0: raise ValueError("silent comparison")
        gain = target/rms
        matched[mode] = audio*gain
        write(directory/f"middle-c-{mode}-matched.wav", matched[mode])
        mono = audio[:4800].mean(axis=1)*np.hanning(4800)
        power = np.abs(np.fft.rfft(mono))**2
        freq = np.fft.rfftfreq(4800,1/48000)
        results[mode] = dict(raw_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                            raw_peak=float(np.abs(audio).max()),raw_window_rms=rms,
                            match_gain_db=20*math.log10(gain),matched_peak=float(np.abs(audio*gain).max()),
                            attack_centroid_hz=float(np.sum(freq*power)/np.sum(power)),
                            attack_energy_above_2khz=float(np.sum(power[freq>=2000])/np.sum(power)))
    old, linear = matched["original"][window], matched["linear"][window]
    results["original_vs_linear_relative_rms_difference"] = float(np.sqrt(np.mean((old-linear)**2))/target)
    # Full first five seconds includes the key release and its tail; half-second separators.
    silence = np.zeros((24000,2))
    sequence = np.concatenate([matched["original"][:240000],silence,
                               matched["linear"][:240000],silence,matched["soundboard"][:240000]])
    write(directory/"middle-c-ABC.wav", sequence)
    results["abc_order"] = ["original: 0–5 s","linear baseline: 5.5–10.5 s","soundboard only: 11–16 s"]
    (directory/"comparison-metrics.json").write_text(json.dumps(results,indent=2)+"\n")
    print(json.dumps(results,indent=2))

if __name__ == "__main__": main()
