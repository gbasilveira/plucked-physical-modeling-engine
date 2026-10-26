# SPDX-License-Identifier: MIT
"""Comparison metrics against the Salamander (Yamaha C5, close AB) reference: octave-band energy shares (same bands, windows and
onset rule as mvp/piano-v4/calibrate_reference.py), decay rate per octave band, L/R magnitude-squared coherence per octave
band, attack spectrum. NumPy/SciPy only."""
import struct, subprocess
import numpy as np
from scipy.signal import coherence

FS = 48000
EDGES = [62.5*2**k for k in range(8)]            # 62.5 Hz ... 8 kHz lower edges, the last band to 16 kHz (as calibrate_reference.py)

def load_ref(path):
    x = np.frombuffer(subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'f32le', '-acodec', 'pcm_f32le', '-ac', '2', '-ar', str(FS), '-']), dtype='<f4').reshape(-1, 2).astype(float)
    return align(x)

def read_wav(path):
    b = open(path, 'rb').read(); pos = 12; fmt = None
    while pos < len(b):
        cid = b[pos:pos+4]; n = struct.unpack('<I', b[pos+4:pos+8])[0]
        if cid == b'fmt ': fmt = struct.unpack('<HHIIHH', b[pos+8:pos+24])
        if cid == b'data':
            tag, ch, fs, _, _, bits = fmt; raw = b[pos+8:pos+8+n]
            if tag == 3: x = np.frombuffer(raw, '<f4').astype(float)
            elif bits == 16: x = np.frombuffer(raw, '<i2').astype(float)/32768
            elif bits == 24:
                a = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32); v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16); v = np.where(v >= 1 << 23, v - (1 << 24), v); x = v/8388608.0
            else: raise ValueError(bits)
            return x.reshape(-1, ch)
        pos += 8 + n + (n & 1)

def onset(x):
    env = np.sqrt(np.mean(x[:FS].reshape(-1, 48, x.shape[1])**2, axis=(1, 2)))
    return int(np.argmax(env > env.max()*0.03))*48

def align(x):
    return x[onset(x):]

def octave_shares(x, lo, hi):
    a = x[round(lo*FS):round(hi*FS)]; a = a - a.mean(axis=0)
    P = np.sum(np.abs(np.fft.rfft(a*np.hanning(len(a))[:, None], axis=0))**2, axis=1); hz = np.fft.rfftfreq(len(a), 1/FS)
    b = np.array([P[(hz >= e) & (hz < (e*2 if e < 8000 else 1e9))].sum() for e in EDGES])
    return b/b.sum()

def band_decay(x, centres=(125, 250, 500, 1000, 2000, 4000), t0=0.4, t1=2.4):
    """dB/s decay of the band power (mono mix, zero-phase FFT band-pass, 50 ms blocks, linear fit t0..t1)."""
    m = x.mean(axis=1)[:int((t1 + 0.5)*FS)]
    n = 1 << int(np.ceil(np.log2(len(m)))); Xf = np.fft.rfft(m, n); hz = np.fft.rfftfreq(n, 1/FS)
    out = []
    for c in centres:
        s = (hz >= c/np.sqrt(2)) & (hz < c*np.sqrt(2))
        y = np.fft.irfft(np.where(s, Xf, 0), n)[:len(m)]
        blk = int(0.05*FS); nb = len(y)//blk
        p = (y[:nb*blk].reshape(nb, blk)**2).mean(1); t = (np.arange(nb) + 0.5)*0.05
        sel = (t >= t0) & (t < t1) & (p > 1e-20)
        out.append(np.polyfit(t[sel], 10*np.log10(p[sel]), 1)[0] if sel.sum() > 4 else np.nan)
    return np.array(out)

def coherence_bands(x, t0=0.1, t1=2.0, centres=(125, 250, 500, 1000, 2000, 4000)):
    a = x[int(t0*FS):int(t1*FS)]
    f, C = coherence(a[:, 0], a[:, 1], fs=FS, nperseg=4096, noverlap=2048)
    P = coherence_weights(a, f)
    out = []
    for c in centres:
        s = (f >= c/np.sqrt(2)) & (f < c*np.sqrt(2)); out.append(float(np.sum(C[s]*P[s])/np.sum(P[s])))
    return np.array(out)

def coherence_weights(a, f):
    from scipy.signal import welch
    _, P = welch(a.mean(axis=1), fs=FS, nperseg=4096, noverlap=2048); return P

def attack(x):
    """Spectral centroid (Hz) and energy share above 2 kHz in 0-30 ms and 30-100 ms after the aligned onset (mono)."""
    res = []
    for lo, hi in ((0.0, 0.03), (0.03, 0.1)):
        a = x[round(lo*FS):round(hi*FS)].mean(axis=1); a = a - a.mean()
        P = np.abs(np.fft.rfft(a*np.hanning(len(a)), 4096))**2; hz = np.fft.rfftfreq(4096, 1/FS)
        res += [float(np.sum(P*hz)/np.sum(P)), float(P[hz > 2000].sum()/P.sum())]
    return np.array(res)

def all_metrics(x):
    return dict(shares_attack=octave_shares(x, 0.01, 0.25), shares_late=octave_shares(x, 0.5, 1.5), decay=band_decay(x),
                coherence=coherence_bands(x), attack=attack(x))
