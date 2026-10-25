#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reference-guided comparison, not a fitted soundboard IR or hammer measurement.

Requires NumPy and ffmpeg. First run fetch_reference.py. Keeps original FLACs
unchanged; measures onset-aligned stereo power, not sample-by-sample phase error.
"""
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import wave
import numpy as np

HERE=Path(__file__).resolve().parent
FS=48000
WINDOWS=[(.01,.1),(.05,.25),(.5,1),(2,2.5)]
BAND_WINDOWS={'attack_10_250ms':(.01,.25),'late_500_1500ms':(.5,1.5)}
BAND_EDGES=[62.5*2**k for k in range(8)]

def octave_shares(x,lo,hi):
    a=x[round(lo*FS):round(hi*FS)];a=a-a.mean(axis=0)
    power=np.sum(np.abs(np.fft.rfft(a*np.hanning(len(a))[:,None],axis=0))**2,axis=1)
    hz=np.fft.rfftfreq(len(a),1/FS)
    b=np.array([power[(hz>=e)&(hz<(e*2 if e<8000 else 1e9))].sum() for e in BAND_EDGES])
    return b/b.sum()

def audio(path):
    x=np.frombuffer(subprocess.check_output(['ffmpeg','-v','error','-i',str(path),
        '-f','f32le','-acodec','pcm_f32le','-ac','2','-ar',str(FS),'-']),dtype='<f4').reshape(-1,2).astype(float)
    assert np.isfinite(x).all() and len(x)>=FS*3
    envelope=np.sqrt(np.mean(x[:FS].reshape(-1,48,2)**2,axis=(1,2)))
    onset=int(np.argmax(envelope>envelope.max()*.03))*48
    return x[onset:],onset/FS

def features(x):
    rows=[]
    for lo,hi in WINDOWS:
        a=x[round(lo*FS):round(hi*FS)]
        a=a-a.mean(axis=0)
        power=np.sum(np.abs(np.fft.rfft(a*np.hanning(len(a))[:,None],axis=0))**2,axis=1)
        hz=np.fft.rfftfreq(len(a),1/FS)
        rows.append(dict(window_seconds=[lo,hi],rms_dbfs=float(10*np.log10(np.mean(a*a))),
            power_centroid_hz=float(np.sum(power*hz)/np.sum(power)),
            energy_above_2khz=float(power[hz>2000].sum()/power.sum())))
    return rows

def main():
    directory=Path(sys.argv[1] if len(sys.argv)>1 else 'out/piano-mvp-v4').resolve()
    cache=Path(sys.argv[2] if len(sys.argv)>2 else 'out/piano-reference-cache').resolve()
    source=json.loads((HERE/'reference/salamander-source.json').read_text())
    records=[];clips=[];clip_rows=[]
    for note,key in [('A2',45),('C4',60),('C6',84)]:
        # The pinned SFZ maps these layers to 37–43, 57–64, 89–96 respectively.
        # These are layer-selection velocities, not measured acoustic hammer speeds.
        for layer,velocity in [(4,40),(8,60),(12,92)]:
            path=cache/f'{note}v{layer}.flac'
            entry=next(e for e in source['files'] if Path(e['path']).name==path.name)
            data=path.read_bytes()
            assert hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()==entry['sha']
            ref,onset=audio(path)
            row=dict(note=note,key=key,layer=layer,selection_midi_velocity=velocity,
                     reference_sha256=hashlib.sha256(data).hexdigest(),reference_onset_seconds=onset,
                     reference=features(ref),models={})
            versions=[('0.4.9','v049'),('0.5.1','v051'),('0.5.2','previous'),('0.5.3','coupled')]
            comparison=[('reference',ref)]
            for version,mode in versions:
                rendered=directory/f'calibration-{note}-v{velocity}-{version}.wav'
                # 24-bit WAV: 16-bit rounding noise sits at the 8-16 kHz level of the reference.
                subprocess.run([str(directory/'piano-render'),str(rendered),str(key),mode,str(velocity/127),'--24bit'],
                               check=True,stdout=subprocess.DEVNULL)
                a,onset=audio(rendered)
                f=features(a)
                attack_errors=[abs(10*math.log10((f[i]['energy_above_2khz']+1e-8)/
                    (row['reference'][i]['energy_above_2khz']+1e-8))) for i in [0,1]]
                bands={name:{str(int(e)):float(10*np.log10((m+1e-15)/(r+1e-15))) for e,m,r in zip(BAND_EDGES,octave_shares(a,*w),octave_shares(ref,*w))}
                       for name,w in BAND_WINDOWS.items()}
                row['models'][version]=dict(onset_seconds=onset,features=f,octave_band_db_vs_reference=bands,
                    mean_attack_high_band_error_db=float(np.mean(attack_errors)))
                if version in ('0.5.2','0.5.3'): comparison.append((version,a))
            if note=='C4':
                for label,a in comparison:
                    a=a[:int(2.5*FS)].copy()
                    # Diagnostic only: equal initial RMS to compare timbre, preserving
                    # each clip's subsequent decay. Music exports keep fixed gain.
                    rms=np.sqrt(np.mean(a[2400:12000]**2));gain=10**(-24/20)/rms
                    a*=gain
                    a[:480]*=np.linspace(0,1,480)[:,None]
                    a[-960:]*=np.linspace(1,0,960)[:,None]
                    clips.extend([a,np.zeros((12000,2))])
                    clip_rows.append(dict(velocity=velocity,label=label,start_seconds=(len(clip_rows))*2.75,
                                          gain_db=float(20*np.log10(gain))))
            records.append(row)
            print(note,velocity,'attack high-band errors (dB):',
                  {v:round(d['mean_attack_high_band_error_db'],2) for v,d in row['models'].items()},flush=True)
    result=dict(source=source,method='onset: first 1 ms RMS block >3% of first-second maximum; stereo Hann-windowed power; DC removed per window',
        limitations=['Close-miked Yamaha C5 differs from the nominal far-field model.',
                     'MIDI layer selection is not a measured hammer velocity.',
                     'Nine notes/layers are a small target set, not an independent validation set.',
                     'Calibration renders are 24-bit; octave shares are band energy over total energy, so low-band deficits raise all other bands.',
                     'A softer attack does not identify soundboard geometry or uniquely determine physical parameters.'],
        records=records,diagnostic_clip_order=clip_rows)
    result['mean_attack_high_band_error_db']={v:float(np.mean([r['models'][v]['mean_attack_high_band_error_db'] for r in records])) for v in ['0.4.9','0.5.1','0.5.2','0.5.3']}
    (directory/'reference-calibration-0.5.3.json').write_text(json.dumps(result,indent=2)+'\n')
    out=np.concatenate(clips)
    assert np.abs(out).max()<1 and len(out)/FS<30
    with wave.open(str(directory/'C4-reference-0.5.2-0.5.3.wav'),'wb') as f:
        f.setparams((2,2,FS,0,'NONE','not compressed'));f.writeframes(np.rint(out*32767).astype('<i2').tobytes())
    print('Mean errors:',result['mean_attack_high_band_error_db'],flush=True)

if __name__=='__main__': main()
