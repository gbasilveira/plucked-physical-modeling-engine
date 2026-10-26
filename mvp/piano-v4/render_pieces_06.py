#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""0.6: render both complete pieces (Chopin Op.28 No.4, Chopin Op.10 No.12) at Full / Live / Balanced.

Uses piano-midi-render --full --float (raw, unnormalised 32-bit float WAV) and writes, per piece, one common-gain
24-bit listening copy per mode (gain = -1 dBFS of the piece's Full render, identical for every mode so levels stay
comparable), the converted per-key-count event file, an audit JSON and an octave-band Live-vs-Full table.
Live and Balanced are the plug-in's defaults: Radiation Auto (= Baked), preset voice limit, Imperfection 1, adaptive
shedding off (piano-midi-render never enables it), so the output is deterministic.
Requires mido==1.3.3 only through prepare_*.py --check (run separately); numpy for analysis.

  render_pieces_06.py OUTDIR [piece ...] [--modes full,live,balanced]    pieces: chopin-prelude, chopin-etude-op10-12
"""
import hashlib, json, math, re, struct, subprocess, sys, time
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from render_full_piece import convert   # per-key count of overlapping same-key notes (shared with the 0.5.3 full render)

FS = 48000; TAIL_S = 4; VERSION = '0.6'
PIECES = {
    'chopin-prelude': dict(title='Chopin: Prelude in E minor, Op. 28 No. 4 (complete)', events='chopin-op28-no4.events',
                           audit='chopin-op28-no4.audit.json', tail_note='source piano-midi.de / Wikimedia Commons'),
    'chopin-etude-op10-12': dict(title='Chopin: Etude in C minor, Op. 10 No. 12 "Revolutionary" (complete)', events='chopin-op10-no12.events',
                                 audit='chopin-op10-no12.audit.json', tail_note='source piano-midi.de / Wikimedia Commons'),
}
MODES = {'full': ['--quality', 'full'], 'live': ['--quality', 'live'], 'balanced': ['--quality', 'balanced']}
BANDS = [(22, 44), (44, 88), (88, 177), (177, 355), (355, 710), (710, 1420), (1420, 2840), (2840, 5680), (5680, 11360), (11360, 22720)]

def read_float(path):
    b = Path(path).read_bytes()
    assert b[:4] == b'RIFF' and struct.unpack('<H', b[20:22])[0] == 3 and struct.unpack('<H', b[34:36])[0] == 32
    i = b.index(b'data'); n = struct.unpack('<I', b[i+4:i+8])[0]
    return np.frombuffer(b[i+8:i+8+n], dtype='<f4').reshape(-1, 2)

def write24(path, x, tags):
    q = np.clip(np.round(x*8388607.0), -8388608, 8388607).astype('<i4')
    raw = q.astype('<i4').view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    data = raw
    fmt = struct.pack('<HHIIHH', 1, 2, FS, FS*6, 6, 24)
    body = b'INFO'
    for tag, text in tags.items():
        t = text.encode()+b'\0'; body += tag.encode()+struct.pack('<I', len(t))+t+b'\0'*(len(t) % 2)
    chunks = b'fmt '+struct.pack('<I', 16)+fmt+b'data'+struct.pack('<I', len(data))+data+b'\0'*(len(data) % 2)+b'LIST'+struct.pack('<I', len(body))+body
    Path(path).write_bytes(b'RIFF'+struct.pack('<I', 4+len(chunks))+b'WAVE'+chunks)

def band_energy(x):
    m = x.astype(np.float64).mean(axis=1)
    n = 1 << int(math.ceil(math.log2(len(m)))); spec = np.abs(np.fft.rfft(m, n))**2; f = np.fft.rfftfreq(n, 1/FS)
    return [float(spec[(f >= lo) & (f < hi)].sum()) for lo, hi in BANDS]

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    args = [a for a in sys.argv[1:]]
    modes = list(MODES)
    if '--modes' in args:
        k = args.index('--modes'); modes = args[k+1].split(','); del args[k:k+2]
    directory = Path(args[0]).resolve(); names = args[1:] or list(PIECES)
    for name in names:
        info = PIECES[name]; src = (HERE/'listening'/info['events']).read_text()
        src_audit = json.loads((HERE/'listening'/info['audit']).read_text())
        events, overlaps, dropped = convert(src)
        ev = directory/f'{name}-{VERSION}.events'; ev.write_text(events)
        rows = {}
        for mode in modes:
            raw = directory/f'{name}-{VERSION}-{mode}-raw.wav'
            t0 = time.time()
            res = subprocess.run([str(directory/'piano-midi-render'), str(ev), str(raw), '--full', '--float', *MODES[mode]],
                                 check=True, capture_output=True, text=True)
            wall = time.time()-t0; line = res.stdout.strip(); print(name, mode, line, flush=True)
            g = lambda pat: float(re.search(pat, line).group(1))
            rows[mode] = dict(raw_file=raw.name, render_report=line, wall_seconds=wall, render_time_seconds=g(r'render time ([\d.eE+-]+) s'),
                              realtime_factor=g(r'realtime factor ([\d.eE+-]+)'), voice_steals=int(g(r'voice steals (\d+)')),
                              numerical_faults=int(g(r'numerical faults (\d+)')), max_active_voices=int(g(r'max active voices (\d+)')),
                              radiation=re.search(r'radiation (\w+) \((\w+ bank)\)', line).groups(), raw_sha256=sha(raw))
        # common normalisation per piece: Full's peak -> -1 dBFS (fallback: the loudest available mode)
        ref = 'full' if 'full' in rows else next(iter(rows))
        audio = {m: read_float(directory/rows[m]['raw_file']) for m in rows}
        peak_ref = float(np.abs(audio[ref]).max()); gain = 10**(-1/20)/peak_ref
        for m, a in audio.items():
            peak = float(np.abs(a).max()); rms = float(np.sqrt((a.astype(np.float64)**2).mean()))
            lp = directory/f'{name}-{VERSION}-{m}.wav'
            tags = {'INAM': f'{info["title"]} - PME {VERSION} - {m}', 'IART': 'MIDI performance: Bernd Krueger; synthesis: PME',
                    'ICOP': 'CC BY-SA 3.0 Germany; https://creativecommons.org/licenses/by-sa/3.0/de/',
                    'ICMT': f'Complete piece + {TAIL_S} s release; one gain per piece ({20*math.log10(gain):+.2f} dB, Full peak at -1 dBFS) for every mode; last 50 ms faded; experimental physical-model synthesis; mode {m}.'}
            write24(lp, np.clip(a*gain, -1, 1), tags)
            rows[m].update(raw_peak=peak, raw_rms=rms, raw_peak_dbfs=20*math.log10(peak), listening_file=lp.name, listening_sha256=sha(lp),
                           listening_peak=peak*gain, duration_seconds=len(a)/FS, frames=len(a), clipped_samples_in_raw=int((np.abs(a) > 1).sum()))
        bands = {m: band_energy(audio[m]) for m in audio}
        diff = {}
        if 'full' in bands:
            for m in bands:
                if m != 'full': diff[m] = [round(10*math.log10(max(b, 1e-30)/max(f, 1e-30)), 2) for b, f in zip(bands[m], bands['full'])]
        # null-free similarity: broadband level difference and correlation of the 1 s RMS envelopes
        env = {}
        if 'full' in audio:
            def e(a): n = len(a)//FS; return np.sqrt((a[:n*FS].astype(np.float64)**2).reshape(n, FS, 2).mean(axis=(1, 2)))
            ef = e(audio['full'])
            for m in audio:
                if m != 'full':
                    em = e(audio[m]); env[m] = dict(rms_db_diff=round(20*math.log10(np.sqrt((audio[m].astype(np.float64)**2).mean())/np.sqrt((audio['full'].astype(np.float64)**2).mean())), 2),
                                                    envelope_1s_mean_abs_db=round(float(np.mean(np.abs(20*np.log10((em+1e-9)/(ef+1e-9))))), 2),
                                                    envelope_1s_max_abs_db=round(float(np.max(np.abs(20*np.log10((em+1e-9)/(ef+1e-9))))), 2))
        ons = [l.split() for l in events.splitlines()]
        audit = dict(piece=info['title'], version=VERSION, source=src_audit, converted_event_schedule_sha256=hashlib.sha256(events.encode()).hexdigest(),
                     note_ons=sum(1 for l in ons if l[1] == '144' and int(l[3]) > 0), sustain_events=sum(1 for l in ons if l[1] == '176'),
                     overlapping_same_key_ons=[dict(frame=f, seconds=round(f/FS, 3), key=k) for f, k in overlaps],
                     dropped_note_offs=[dict(frame=f, seconds=round(f/FS, 3), key=k) for f, k in dropped],
                     overlap_handling='per-key count of sounding notes; second note-on re-strikes the key; note-off dropped unless it ends the last overlapping note',
                     release_tail_seconds=TAIL_S, final_fade='last 50 ms', sample_rate=FS, listening_gain_db=20*math.log10(gain),
                     normalisation='one gain per piece: Full raw peak -> -1 dBFS; the same gain for Live/Balanced',
                     adaptive_shedding='off (deterministic)', imperfection=1.0, radiation_auto='Baked for Live/Balanced, Modal for Full',
                     voice_limit_note='Full: 88 (one per key, no stealing offline); Live: preset 16; Balanced: preset 24 (plug-in defaults, Voices Auto)',
                     modes=rows, octave_bands_hz=BANDS, octave_band_energy=bands, octave_band_db_vs_full=diff, level_envelope_vs_full=env)
        (directory/f'{name}-{VERSION}-audit.json').write_text(json.dumps(audit, indent=2)+'\n')
        print(name, 'Live-vs-Full octave dB:', diff, env, flush=True)

if __name__ == '__main__': main()
