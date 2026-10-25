#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render soft/medium/hard C4 with one common gain; never normalise each note.

Usage: python3 velocity_check.py out/piano-mvp-v4
Requires NumPy. The resulting 19 s clip plays velocities 32, 72, 112 in that order.
"""
from pathlib import Path
import json
import math
import subprocess
import sys
import wave
import numpy as np

def main():
    directory = Path(sys.argv[1] if len(sys.argv)>1 else "out/piano-mvp-v4").resolve()
    clips = []; rows = []
    for velocity in [32,72,112]:
        path = directory/f"C4-velocity-{velocity:03d}-raw.wav"
        subprocess.run([str(directory/"piano-render"),str(path),"60","coupled",str(velocity/127)],check=True)
        with wave.open(str(path)) as f:
            assert (f.getframerate(),f.getnchannels(),f.getsampwidth()) == (48000,2,2)
            a = np.frombuffer(f.readframes(f.getnframes()),dtype="<i2").reshape(-1,2).astype(float)/32768
        rms = float(np.sqrt(np.mean(a[2400:24000]**2)))
        attack = a[:4800]*np.hanning(4800)[:,None]
        power = np.sum(np.abs(np.fft.rfft(attack,axis=0))**2,axis=1)
        hz = np.fft.rfftfreq(4800,1/48000)
        rows.append(dict(midi_velocity=velocity,peak=float(np.abs(a).max()),rms=rms,
                         rms_dbfs=20*math.log10(rms),attack_centroid_hz=float(np.sum(hz*power)/np.sum(power)),
                         attack_energy_above_2khz=float(np.sum(power[hz>=2000])/np.sum(power))))
        clips.append(a[:288000])
    assert rows[0]["rms"] < rows[1]["rms"] < rows[2]["rms"], "flat velocity response"
    gain = 0.7/max(float(np.abs(a).max()) for a in clips)
    audio = np.concatenate([clips[0],np.zeros((24000,2)),clips[1],np.zeros((24000,2)),clips[2]])*gain
    with wave.open(str(directory/"velocity-soft-medium-hard.wav"),"wb") as f:
        f.setparams((2,2,48000,0,"NONE","not compressed"))
        f.writeframes(np.rint(audio*32767).astype("<i2").tobytes())
    result = dict(notes=rows,common_playback_gain_db=20*math.log10(gain),
                  hard_soft_rms_difference_db=20*math.log10(rows[2]["rms"]/rows[0]["rms"]),
                  order="32: 0–6 s; 72: 6.5–12.5 s; 112: 13–19 s",
                  keyboard_velocity_verified=False,
                  note="Core/renderer velocity audit. Shared gain preserves dynamics. Hardware input requires a separate MIDI capture.")
    (directory/"velocity-metrics.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result,indent=2))

if __name__ == "__main__": main()
