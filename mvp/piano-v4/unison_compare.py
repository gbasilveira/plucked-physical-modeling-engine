#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render single/unison C4 and match only their initial 50–250 ms RMS."""
import wave,json,sys,subprocess
from pathlib import Path
import numpy as np
p=Path(sys.argv[1] if len(sys.argv)>1 else 'out/piano-mvp-v4').resolve()
for mode,name in [('single','single-C4.wav'),('unisons','unisons-C4.wav')]:
 subprocess.run([str(p/'piano-render'),str(p/name),'60',mode,'0.75'],check=True)
a=[]; metrics=[]
for name in ['single-C4.wav','unisons-C4.wav']:
 with wave.open(str(p/name)) as f: x=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').reshape(-1,2)/32768.
 rms=float(np.sqrt(np.mean(x[2400:12000]**2)))
 gain=10**(-24/20)/rms
 a.append(x[:240000]*gain)
 metrics.append(dict(file=name,attack_rms=rms,gain_db=float(20*np.log10(gain)),early_late_db=float(10*np.log10(np.mean(x[96000:120000]**2)/np.mean(x[12000:36000]**2)))))
y=np.concatenate([a[0],np.zeros((24000,2)),a[1]])
assert np.max(np.abs(y))<1
with wave.open(str(p/'single-vs-unisons-C4.wav'),'wb') as f:
 f.setparams((2,2,48000,0,'NONE','not compressed')); f.writeframes(np.rint(y*32767).astype('<i2').tobytes())
(p/'unisons-comparison.json').write_text(json.dumps(dict(order='single 0–5 seconds; unisons 5.5–10.5 seconds',matching='RMS 0.05–0.25 s at -24 dBFS; decay left unchanged',measurements=metrics),indent=2)+'\n')
print(json.dumps(metrics,indent=2))
