#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""'Box feels small' diagnostic (0.5.3): low-band attack energy and stereo width, model vs Salamander.

Requires NumPy and ffmpeg; run fetch_reference.py first. Usage: box_analysis.py [dir] [cache] [version...]
Reports, per note/layer and window: absolute power (dB re full scale^2, per band), mid/side power ratio,
and the mean magnitude-squared coherence between L and R per band. Close-miked references are not a
far-field measurement; this compares signals, it does not identify the board.
"""
import importlib.util, json, subprocess, sys
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cal',HERE/'calibrate_reference.py'); cal=importlib.util.module_from_spec(spec); spec.loader.exec_module(cal)
FS=48000
EDGES=[31.25,62.5,125,250,500,1000,2000,4000,8000]
WIN={'attack_10_250ms':(.01,.25),'late_500_1500ms':(.5,1.5)}
def band_power(x,lo,hi):
    a=x[round(lo*FS):round(hi*FS)];a=a-a.mean(axis=0)
    w=np.hanning(len(a))[:,None]
    f=np.fft.rfft(a*w,axis=0);hz=np.fft.rfftfreq(len(a),1/FS)
    p=np.sum(np.abs(f)**2,axis=1)/len(a)**2
    m=(f[:,0]+f[:,1])/2;s=(f[:,0]-f[:,1])/2
    pm=np.abs(m)**2/len(a)**2;ps=np.abs(s)**2/len(a)**2
    out=[]
    for lo_,hi_ in zip(EDGES[:-1],EDGES[1:]):
        k=(hz>=lo_)&(hz<hi_)
        out.append((p[k].sum(),pm[k].sum(),ps[k].sum()))
    return out
def coherence(x,lo,hi):
    # Welch magnitude-squared coherence, 4096-sample Hann segments, 50 % overlap, per band mean.
    a=x[round(lo*FS):round(hi*FS)];n=4096;step=n//2;w=np.hanning(n)
    segs=[a[i:i+n] for i in range(0,len(a)-n+1,step)]
    if len(segs)<2: return [float('nan')]*(len(EDGES)-1)
    F=np.array([np.fft.rfft(s*w[:,None],axis=0) for s in segs])
    pll=np.mean(np.abs(F[:,:,0])**2,axis=0);prr=np.mean(np.abs(F[:,:,1])**2,axis=0)
    plr=np.mean(F[:,:,0]*np.conj(F[:,:,1]),axis=0)
    c=np.abs(plr)**2/(pll*prr+1e-30);hz=np.fft.rfftfreq(n,1/FS)
    return [float(np.mean(c[(hz>=l)&(hz<h)])) for l,h in zip(EDGES[:-1],EDGES[1:])]
def db(v): return 10*np.log10(v+1e-20)
def main():
    directory=Path(sys.argv[1] if len(sys.argv)>1 else 'out/piano-mvp-v4').resolve()
    cache=Path(sys.argv[2] if len(sys.argv)>2 else 'out/piano-reference-cache').resolve()
    versions=sys.argv[3:] or ['0.5.2','0.5.3']
    result={}
    for note,key in [('A2',45),('C4',60)]:
        for layer,vel in [(8,60),(12,92)]:
            ref,_=cal.audio(cache/f'{note}v{layer}.flac')
            clips={'reference':ref}
            for v in versions: clips[v]=cal.audio(directory/f'calibration-{note}-v{vel}-{v}.wav')[0]
            for name,(lo,hi) in WIN.items():
                print(f'\n{note} v{vel} {name}: band lower edges',' '.join(f'{e:7.0f}' for e in EDGES[:-1]))
                for label,x in clips.items():
                    bp=band_power(x,lo,hi);co=coherence(x,lo,hi)
                    print(f'{label:>10} power dB ',' '.join(f'{db(p):7.1f}' for p,_,_ in bp))
                    print(f'{"":>10} side/mid dB',' '.join(f'{db(s)-db(m):7.1f}' for _,m,s in bp))
                    print(f'{"":>10} coherence  ',' '.join(f'{c:7.2f}' for c in co))
                    result[f'{note}-v{vel}-{name}-{label}']=dict(power_db=[float(db(p)) for p,_,_ in bp],
                        side_mid_db=[float(db(s)-db(m)) for _,m,s in bp],coherence=co)
    (directory/'box-analysis-0.5.3.json').write_text(json.dumps(dict(band_edges_hz=EDGES,windows=WIN,rows=result),indent=1)+'\n')
if __name__=='__main__': main()
