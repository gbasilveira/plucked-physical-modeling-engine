#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fixed versus mobile bridge: same strings, hammer, soundboard and MIDI performance.

python3 mvp/piano-v4/compare.py out/piano-mvp-v4 [--skip-render]
NumPy only. Match initial 0.05–0.25 s, preserving subsequent decay differences.
"""
from pathlib import Path
import csv
import hashlib
import json
import math
import subprocess
import sys
import wave
import numpy as np

def read(path):
    with wave.open(str(path)) as f:
        assert (f.getframerate(),f.getnchannels(),f.getsampwidth()) == (48000,2,2)
        return np.frombuffer(f.readframes(f.getnframes()),dtype="<i2").reshape(-1,2).astype(float)/32768

def write(path,audio):
    if not np.isfinite(audio).all() or np.abs(audio).max() >= 0.999:
        raise ValueError("refusing clipped/non-finite comparison")
    with wave.open(str(path),"wb") as f:
        f.setparams((2,2,48000,0,"NONE","not compressed"))
        f.writeframes(np.rint(audio*32767).astype("<i2").tobytes())

def energy(audio,start,end):
    a = audio[int(start*48000):int(end*48000)]
    return float(np.mean(a*a))

def partial_bands(audio,row,start,end):
    a = audio[int(start*48000):int(end*48000)]
    spectrum = np.fft.rfft(a*np.hanning(len(a))[:,None],axis=0)
    power = np.sum(np.abs(spectrum)**2,axis=1)
    hz = np.fft.rfftfreq(len(a),1/48000)
    length,diameter,tension,density = (float(row[s]) for s in
        ["length_m","diameter_mm","tension_n","effective_density_kg_m3"])
    d = diameter*0.001
    mu = density*math.pi*d*d/4
    ei = 2.02e11*math.pi*d**4/64
    result = []
    for n in range(1,4):
        k = n*math.pi/length
        f = math.sqrt((tension*k*k+ei*k**4)/mu)/(2*math.pi)
        width = max(8.0,0.01*f)
        result.append(float(np.sum(power[np.abs(hz-f)<width])))
    return result

def main():
    directory = Path(sys.argv[1] if len(sys.argv)>1 else "out/piano-mvp-v4").resolve()
    source = Path(__file__).resolve().parent.parent/"piano-v2"/"report-appendix-a.csv"
    with source.open() as f: rows = {int(r["midi_key"]):r for r in csv.DictReader(f)}
    target = 10**(-24/20)
    results = {"velocity":0.75,"release_s":4,"match_window_s":[0.05,0.25],
               "target_rms_dbfs":-24,"ab_order":"fixed 0–5 s; mobile bridge 5.5–10.5 s",
               "warning":"Match initial level only; later decay changes are retained. Band energies are not a realism score.",
               "notes":{}}
    sequences = []
    for key,label in [(45,"A2"),(60,"C4"),(84,"C6")]:
        clips = []; info = {"midi_key":key}
        for mode in ["fixed","coupled"]:
            path = directory/f"{label}-{mode}-raw.wav"
            stdout = None
            if "--skip-render" not in sys.argv:
                stdout = subprocess.check_output([str(directory/"piano-render"),str(path),str(key),mode],text=True)
                print(stdout,end="")
            a = read(path)
            rms = math.sqrt(energy(a,0.05,0.25))
            if rms < 1e-8: raise ValueError("silent comparison")
            gain = target/rms
            clips.append(a[:240000]*gain)
            early,late = partial_bands(a,rows[key],0.25,0.75),partial_bands(a,rows[key],2,2.5)
            info[mode] = dict(raw_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                              raw_peak=float(np.abs(a).max()),initial_rms=rms,
                              match_gain_db=20*math.log10(gain),matched_peak=float(np.abs(a*gain).max()),
                              late_to_early_rms_db=10*math.log10(max(energy(a,2,2.5),1e-30)/energy(a,0.25,0.75)),
                              partial_neighbourhood_decay_db=[10*math.log10(max(l,1e-30)/max(e,1e-30)) for e,l in zip(early,late)],
                              render_log=stdout)
        ab = np.concatenate([clips[0],np.zeros((24000,2)),clips[1]])
        write(directory/f"{label}-bridge-AB.wav",ab)
        sequences.extend([ab,np.zeros((24000,2))])
        results["notes"][label] = info
    write(directory/"registers-bridge-AB.wav",np.concatenate(sequences[:-1]))
    control = directory.parent/"piano-mvp-v3"/"middle-c-soundboard-raw.wav"
    if control.exists():
        identical = control.read_bytes() == (directory/"C4-fixed-raw.wav").read_bytes()
        if not identical: raise ValueError("fixed-bridge control differs from MVP 3")
        results["fixed_C4_byte_identical_to_MVP3"] = True
    (directory/"comparison-metrics.json").write_text(json.dumps(results,indent=2)+"\n")
    print(json.dumps(results,indent=2))

if __name__ == "__main__": main()
