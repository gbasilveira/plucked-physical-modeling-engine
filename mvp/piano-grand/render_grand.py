# SPDX-License-Identifier: MIT
"""Offline 0.7a render: unchanged 0.6 Full strings + hammer coupled to the FEM board (piano-grand-render), radiation to
microphone pairs by convolving the bridge-force records with the port-space IRs of assemble_ir.py.
Usage as a library (validate_salamander.py, make_renders.py) or: render_grand.py WORK OUTDIR LID EVENTS NAME [preset ...]"""
import subprocess, sys, struct, wave
from pathlib import Path
import numpy as np
from scipy.signal import fftconvolve
import scene

FS = 48000

def board_table(work, modes_npz, lid, accept=True):
    """512-row table for the BoardOverride hook: loaded poles where the estimate is accepted, vacuum values otherwise."""
    d = np.load(modes_npz); p = np.load(Path(work)/f'ir_{lid}_poles.npz')
    om = p['omega'].copy(); sg = p['sigma'].copy(); ov = p['omega_vacuum']; sv = p['sigma_vacuum']
    if accept:
        bad = ~(p['ok'] & (om[:len(p['ok'])]/ov[:len(p['ok'])] > 0.8) & (om[:len(p['ok'])]/ov[:len(p['ok'])] < 1.2) &
                (sg[:len(p['ok'])]/sv[:len(p['ok'])] > 0.6) & (sg[:len(p['ok'])]/sv[:len(p['ok'])] < 3.0))
        om[:len(bad)][bad] = ov[:len(bad)][bad]; sg[:len(bad)][bad] = sv[:len(bad)][bad]
    n = len(om); assert n <= 512
    omega = np.full(512, 1.0e6); sigma = np.zeros(512); phi = np.zeros((88, 512))
    omega[:n] = om; sigma[:n] = sg; phi[:, :n] = d['phi']
    return omega, sigma, phi

def write_table(path, omega, sigma, phi):
    Path(path).write_bytes(np.concatenate([omega, sigma, phi.ravel()]).astype('<f8').tobytes())

def events_text(notes, tail_frames=0):
    """notes: list of (start_s, dur_s, key, velocity 1..127); one pedal-less event schedule."""
    ev = []
    for s, d, k, v in notes:
        ev.append((int(round(s*FS)), 144, k, v)); ev.append((int(round((s + d)*FS)), 128, k, 0))
    ev.sort(key=lambda e: (e[0], -e[1]))
    return ''.join(f'{f} {st} {a} {b}\n' for f, st, a, b in ev)

def run_render(exe, events_file, table_file, prefix, tail=4.0):
    r = subprocess.run([exe, str(events_file), str(table_file), str(prefix), '--full', '--tail', str(tail)], capture_output=True, text=True)
    if r.returncode: raise RuntimeError(r.stderr)
    return r.stdout.strip()

def radiate(prefix, E, mic_idx, normalise=1.0):
    """Convolve the force records with the port IRs for the selected microphones. Returns (len(mic_idx), frames + nT0)."""
    g = np.fromfile(str(prefix) + '.g.f32', np.float32).reshape(-1, 88)
    out = np.zeros((len(mic_idx), g.shape[0] + E.shape[2] - 1))
    for k in range(88):
        if not np.any(g[:, k]): continue
        gk = g[:, k].astype(np.float64)
        for j, m in enumerate(mic_idx):
            out[j] += fftconvolve(gk, E[m, k].astype(np.float64))
    return out*normalise

def write_wav24(path, x, fs=FS):
    x = np.clip(x, -1, 1); pcm = np.rint(x*8388607).astype(np.int32)
    b = bytearray()
    for v in pcm.reshape(-1):
        b += struct.pack('<i', int(v))[:3]
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(3); w.setframerate(fs); w.writeframes(bytes(b))

def write_wav_f32(path, x, fs=FS):
    x = x.astype('<f4'); n = x.shape[0]; ch = x.shape[1]
    hdr = b'RIFF' + struct.pack('<I', 36 + x.size*4) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 3, ch, fs, fs*ch*4, ch*4, 32) + b'data' + struct.pack('<I', x.size*4)
    Path(path).write_bytes(hdr + x.tobytes())
