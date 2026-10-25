#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate, render and export the fixed Chopin excerpt for listening comparisons.

Requires only mido==1.3.3 and Python's standard library. Preserve versioned outputs.
"""
from array import array
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import wave

HERE = Path(__file__).resolve().parent
VERSION = '0.5.1'

def read(path):
    with wave.open(str(path)) as f:
        assert (f.getframerate(),f.getnchannels(),f.getsampwidth(),f.getnframes()) == (48000,2,2,26*48000)
        pcm = f.readframes(f.getnframes())
    samples = array('h',pcm)
    if sys.byteorder != 'little': samples.byteswap()
    return samples, hashlib.sha256(pcm).hexdigest()

def export(path,samples,gain,version):
    # Excerpt starts in an already-sounding phrase: a shared 10 ms boundary fade
    # avoids a discontinuity without changing the notes or the raw render.
    out = array('h',(round(x*gain*min(1.0,(i//2)/480)) for i,x in enumerate(samples)))
    assert max(abs(x) for x in out) < 32767
    if sys.byteorder != 'little': out.byteswap()
    with wave.open(str(path),'wb') as f:
        f.setparams((2,2,48000,0,'NONE','not compressed'));f.writeframes(out.tobytes())
    tags = {'INAM':f'Chopin Prelude Op.28 No.4 - PME {version} - 26 s',
            'IART':'MIDI performance: Bernd Krueger; synthesis: PME',
            'ICOP':'CC BY-SA 3.0 Germany; https://creativecommons.org/licenses/by-sa/3.0/de/',
            'ICMT':'Source piano-midi.de via Wikimedia Commons; excerpt 40-64 s + 2 s release; 10 ms start fade; common gain; experimental physical-model synthesis.'}
    body=b'INFO'
    for tag,text in tags.items():
        data=text.encode()+b'\0'
        body+=tag.encode()+struct.pack('<I',len(data))+data+b'\0'*(len(data)%2)
    with path.open('r+b') as f:
        f.seek(0,2);f.write(b'LIST'+struct.pack('<I',len(body))+body)
        size=f.tell();f.seek(4);f.write(struct.pack('<I',size-8))

def main():
    directory=Path(sys.argv[1] if len(sys.argv)>1 else 'out/piano-mvp-v4').resolve()
    subprocess.run([sys.executable,str(HERE/'prepare_classical.py'),'--check'],check=True,stdout=subprocess.DEVNULL)
    fixture=HERE/'listening/chopin-op28-no4.events'
    audit=json.loads((HERE/'listening/chopin-op28-no4.audit.json').read_text())
    clips=[];rows=[]
    for version,flags in [('0.5.0',['--previous']),(VERSION,[])]:
        path=directory/f'chopin-prelude-{version}-raw.wav'
        command=[str(directory/'piano-midi-render'),str(fixture),str(path),*flags]
        result=subprocess.run(command,check=True,capture_output=True,text=True)
        print(version,result.stdout.strip(),flush=True)
        samples,pcm_sha=read(path)
        peak=max(abs(x) for x in samples)/32768
        rms=math.sqrt(sum(float(x)*x for x in samples)/len(samples))/32768
        clips.append(samples)
        rows.append(dict(version=version,raw_file=path.name,raw_pcm_sha256=pcm_sha,
                         peak=peak,rms=rms,render_report=result.stdout.strip()))
    # One gain for both versions; no per-note or per-version loudness normalisation.
    gain=4.0  # Fixed +12.04 dB listening gain; keep this unchanged in later iterations.
    for samples,row in zip(clips,rows):
        path=directory/f'chopin-prelude-{row["version"]}.wav'
        export(path,samples,gain,row['version'])
        row.update(listening_file=path.name,listening_file_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    latest=directory/'chopin-prelude-latest.wav'
    shutil.copy2(directory/f'chopin-prelude-{VERSION}.wav',latest)
    report=dict(source_audit=audit,version=VERSION,common_gain_db=20*math.log10(gain),
                duration_seconds=26,normalisation='fixed common +12.0412 dB; no per-version normalisation',
                boundary_fades='10 ms start on listening copies; last 50 ms after release in raw and listening copies',renders=rows)
    (directory/f'chopin-prelude-{VERSION}-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    shutil.copy2(HERE/'listening/README.md',directory/'CHOPIN-ATTRIBUTION.md')
    print('Ready:',latest,flush=True)

if __name__=='__main__': main()
