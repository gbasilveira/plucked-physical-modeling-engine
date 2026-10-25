#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render the complete pinned Chopin Op.28 No.4 MIDI (not the 40-64 s excerpt).

Requires mido==1.3.3 (via prepare_classical.py --check, which re-validates the pinned
source SHA-256 and regenerates/compares the full event schedule). The MIDI is not modified.

Overlapping same-key notes (a note-on for a key still sounding): the engine re-strikes the
key's strings on the second note-on (Piano::noteOn). Piano::noteOff only clears a boolean,
so the first note-off would release the second note. This script tracks a per-key count of
sounding notes and drops every note-off except the one that ends the LAST overlapping note.
The note-ons, velocities, timing and pedal are unchanged.
"""
from array import array
import collections, hashlib, json, math, shutil, struct, subprocess, sys, wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
VERSION = '0.5.3'
FS = 48000
GAIN = 4.0          # fixed +12.04 dB listening gain, identical to the excerpt renders
SAFETY_GAIN = 2.0   # documented -6.02 dB relative to GAIN, used only for the -safe copy
TAIL_S = 4
LISTEN_DB = 20*math.log10(GAIN)

def convert(src_events):
    count = collections.Counter(); out = []; dropped = []; overlaps = []
    for line in src_events.splitlines():
        frame, status, a, b = map(int, line.split())
        if status == 144 and b > 0:
            count[a] += 1
            if count[a] > 1: overlaps.append([frame, a])
        elif status in (128, 144):
            assert count[a] >= 1, 'unmatched note off'
            count[a] -= 1
            if count[a] > 0:
                dropped.append([frame, a]); continue
        out.append(f'{frame} {status} {a} {b}\n')
    assert not sum(count.values())
    return ''.join(out), overlaps, dropped

def export(path, raw, gain, tags):
    pcm = array('h', raw)
    out = array('h', (round(x*gain) for x in pcm))
    assert max(abs(x) for x in out) < 32767
    if sys.byteorder != 'little': out.byteswap()
    with wave.open(str(path), 'wb') as f:
        f.setparams((2, 2, FS, 0, 'NONE', 'not compressed')); f.writeframes(out.tobytes())
    body = b'INFO'
    for tag, text in tags.items():
        data = text.encode()+b'\0'
        body += tag.encode()+struct.pack('<I', len(data))+data+b'\0'*(len(data) % 2)
    with path.open('r+b') as f:
        f.seek(0, 2); f.write(b'LIST'+struct.pack('<I', len(body))+body)
        size = f.tell(); f.seek(4); f.write(struct.pack('<I', size-8))

def main():
    directory = Path(sys.argv[1] if len(sys.argv) > 1 else 'out/piano-mvp-v4').resolve()
    subprocess.run([sys.executable, str(HERE/'prepare_classical.py'), '--check'], check=True, stdout=subprocess.DEVNULL)
    src = (HERE/'listening/chopin-op28-no4.events').read_text()
    source_audit = json.loads((HERE/'listening/chopin-op28-no4.audit.json').read_text())
    events, overlaps, dropped = convert(src)
    ev_path = directory/'chopin-prelude-full-0.5.3.events'
    ev_path.write_text(events)
    rows = [l.split() for l in events.splitlines()]
    lines = [list(map(int, l)) for l in rows]
    ons = [l for l in lines if l[1] == 144 and l[3] > 0]
    pedal = [l for l in lines if l[1] == 176]
    raw_path = directory/f'chopin-prelude-full-{VERSION}-raw.wav'
    res = subprocess.run([str(directory/'piano-midi-render'), str(ev_path), str(raw_path), '--full'],
                         check=True, capture_output=True, text=True)
    report_line = res.stdout.strip(); print(report_line, flush=True)
    with wave.open(str(raw_path)) as f:
        assert (f.getframerate(), f.getnchannels(), f.getsampwidth()) == (FS, 2, 2)
        n = f.getnframes(); pcm = f.readframes(n)
    raw = array('h', pcm)
    if sys.byteorder != 'little': raw.byteswap()
    peak = max(abs(x) for x in raw)/32768
    rms = math.sqrt(sum(float(x)*x for x in raw)/len(raw))/32768
    last_event = lines[-1][0]
    tags = {'INAM': f'Chopin Prelude Op.28 No.4 (complete) - PME {VERSION}',
            'IART': 'MIDI performance: Bernd Krueger; synthesis: PME',
            'ICOP': 'CC BY-SA 3.0 Germany; https://creativecommons.org/licenses/by-sa/3.0/de/',
            'ICMT': 'Source piano-midi.de via Wikimedia Commons; complete piece + 4 s release; last 50 ms faded; fixed +12.04 dB gain; experimental physical-model synthesis.'}
    listen = directory/f'chopin-prelude-full-{VERSION}.wav'
    clipped = peak*GAIN >= 32767/32768
    export(listen, raw, GAIN, tags) if not clipped else None
    safe = None
    if clipped:
        safe = directory/f'chopin-prelude-full-{VERSION}-safe.wav'
        export(safe, raw, SAFETY_GAIN, {**tags, 'ICMT': tags['ICMT']+' SAFETY COPY: gain +6.02 dB instead of +12.04 dB because the fixed gain clips.'})
    else:
        shutil.copy2(listen, directory/'chopin-prelude-full-latest.wav')
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    import re
    m = lambda pat: float(re.search(pat, report_line).group(1))
    audit = dict(piece='Chopin: Prelude in E minor, Op. 28 No. 4 (complete)', version=VERSION,
        source_sha256=source_audit['source_sha256'], source_url=source_audit['source_url'],
        license=source_audit['license'], midi_author=source_audit['midi_author'],
        full_event_schedule_source_sha256=source_audit['event_schedule_sha256'],
        converted_event_schedule_sha256=hashlib.sha256(events.encode()).hexdigest(),
        note_ons=len(ons), note_offs_in_source=sum(1 for l in src.splitlines() if (lambda f: f[1]==128 or (f[1]==144 and f[3]==0))(list(map(int,l.split())))),
        note_offs_emitted=sum(1 for l in lines if l[1]==128), tempo_changes=source_audit['tempo_changes'],
        velocity_range=[min(l[3] for l in ons), max(l[3] for l in ons)],
        key_range=[min(l[2] for l in ons), max(l[2] for l in ons)],
        sustain_events=len(pedal), pedal_down_events=sum(1 for l in pedal if l[3] >= 64),
        pedal_up_events=sum(1 for l in pedal if l[3] < 64),
        first_event_frame=lines[0][0], last_event_frame=last_event,
        overlapping_same_key_ons=[dict(frame=f, seconds=round(f/FS, 3), key=k) for f, k in overlaps],
        overlap_handling='per-key count of sounding notes; second note-on re-strikes the key (engine behaviour); note-off dropped unless it ends the last overlapping note',
        dropped_note_offs=[dict(frame=f, seconds=round(f/FS, 3), key=k) for f, k in dropped],
        duration_seconds=n/FS, frames=n, sample_rate=FS, release_tail_seconds=TAIL_S,
        final_fade='last 50 ms (raw and listening copies)', voice_limit=88,
        voice_steals=int(m(r'voice steals (\d+)')), numerical_faults=int(m(r'numerical faults (\d+)')),
        render_time_seconds=m(r'render time ([\d.eE+-]+) s'), realtime_factor=m(r'realtime factor ([\d.eE+-]+)'),
        raw_peak=peak, raw_rms=rms, listening_gain_db=LISTEN_DB,
        listening_peak=peak*GAIN, listening_rms=rms*GAIN, listening_gain_clips=clipped,
        safe_copy=None if safe is None else dict(file=safe.name, gain=SAFETY_GAIN, gain_db=20*math.log10(SAFETY_GAIN), sha256=sha(safe)),
        raw_file=raw_path.name, raw_wav_sha256=sha(raw_path), raw_pcm_sha256=hashlib.sha256(pcm).hexdigest(),
        listening_file=None if clipped else listen.name, listening_file_sha256=None if clipped else sha(listen),
        render_report=report_line)
    (directory/f'chopin-prelude-full-{VERSION}-audit.json').write_text(json.dumps(audit, indent=2)+'\n')
    print(json.dumps({k: audit[k] for k in ('duration_seconds','render_time_seconds','realtime_factor','raw_peak','raw_rms','listening_peak','listening_gain_clips','voice_steals','numerical_faults')}, indent=1))

if __name__ == '__main__': main()
