#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""0.5.2 tension-modulation measurements: pitch glide and inharmonic (phantom) sideband energy.

Needs NumPy and out/piano-mvp-v4/piano-tension-measure. Works on the bridge force a key puts on the
shared board (not the radiated sound), so board resonances and the microphone model do not enter.
"""
import json
import subprocess
import sys
import numpy as np
from pathlib import Path

FS = 48000
BIN = Path(sys.argv[1] if len(sys.argv) > 1 else 'out/piano-mvp-v4').resolve() / 'piano-tension-measure'
TMP = Path(sys.argv[2] if len(sys.argv) > 2 else '/tmp').resolve()

def run(key, vel, mode, seconds=3.0):
    path = TMP / f'tm-{key}-{vel}-{mode}.f64'
    subprocess.run([str(BIN), str(key), str(vel / 127), str(seconds), mode, str(path)], check=True)
    return np.fromfile(path, dtype='<f8')

def partials(key):
    out = subprocess.run([str(BIN), str(key), 'modes'], check=True, capture_output=True, text=True).stdout
    return np.array([float(x) for x in out.split()])

def inst_freq(x, fc, sigma):
    """Instantaneous frequency (Hz) of the component near fc: Gaussian band-pass, analytic signal, phase slope."""
    n = len(x)
    X = np.fft.fft(x)
    f = np.fft.fftfreq(n, 1 / FS)
    h = np.where(f > 0, 2.0, 0.0) * np.exp(-0.5 * ((f - fc) / sigma) ** 2)
    z = np.fft.ifft(X * h)
    ph = np.unwrap(np.angle(z))
    lag = 240   # 5 ms
    return (ph[lag:] - ph[:-lag]) / (2 * np.pi) * FS / lag, np.arange(len(ph) - lag) / FS + lag / 2 / FS

def window_mean(freq, t, lo, hi):
    m = (t >= lo) & (t < hi)
    return float(np.mean(freq[m]))

def cents(a, b):
    return 1200 * np.log2(a / b)

def glide(key, vel):
    on, off = run(key, vel, 'on'), run(key, vel, 'off')
    p = partials(key)
    # a partial near 500-1000 Hz tracks the pitch with a usable time resolution (sigma 10 Hz ~ 16 ms)
    idx = int(np.argmin(np.abs(p - 700)))
    spec = np.abs(np.fft.rfft(off[int(.2 * FS):int(1.2 * FS)] * np.hanning(FS)))
    fr = np.fft.rfftfreq(FS, 1 / FS)
    near = (fr > p[idx] - 6) & (fr < p[idx] + 6)
    fc = fr[near][np.argmax(spec[near])]
    fo, t = inst_freq(off, fc, 10.0)
    fn, _ = inst_freq(on, fc, 10.0)
    row = dict(key=key, velocity=vel, partial=idx + 1, tracked_hz=float(fc))
    for lo, hi in [(.03, .06), (.06, .10), (.10, .25), (.25, .5), (1.0, 1.5)]:
        row[f'on_minus_off_cents_{int(lo*1000)}_{int(hi*1000)}ms'] = float(cents(window_mean(fn, t, lo, hi), window_mean(fo, t, lo, hi)))
    # glide inside the "on" note: early frequency relative to its own late frequency
    row['on_glide_cents_30_100ms_vs_1_1.5s'] = float(cents(window_mean(fn, t, .03, .10), window_mean(fn, t, 1.0, 1.5)))
    row['off_glide_cents_30_100ms_vs_1_1.5s'] = float(cents(window_mean(fo, t, .03, .10), window_mean(fo, t, 1.0, 1.5)))
    return row

def sidebands(key, vel):
    p = partials(key)
    res = dict(key=key, velocity=vel)
    spectra = {}
    for mode in ('off', 'on'):
        x = run(key, vel, mode)
        a = x[int(.3 * FS):int(2.3 * FS)]
        P = np.abs(np.fft.rfft(a * np.hanning(len(a)))) ** 2
        fr = np.fft.rfftfreq(len(a), 1 / FS)
        band = (fr >= 30) & (fr <= 3000)
        near = np.zeros_like(band)
        for f in p:
            near |= np.abs(fr - f) <= max(2.0, 0.0006 * f)
        # string and its unison detune: each partial is also allowed to sit +-0.8 cent around f
        outside = band & ~near
        res[f'{mode}_inharmonic_fraction_db'] = float(10 * np.log10(P[outside].sum() / P[band].sum()))
        res[f'{mode}_total_power_db'] = float(10 * np.log10(P[band].sum()))
        spectra[mode] = (fr, P, outside)
    fr, Pn, out = spectra['on']
    _, Pf, _ = spectra['off']
    res['extra_inharmonic_power_on_vs_off_db'] = float(10 * np.log10(Pn[out].sum() / Pf[out].sum()))
    # strongest component outside the partial grid
    i = np.argmax(np.where(out, Pn, 0)); top = float(np.max(Pn[spectra['on'][0] > 0]))
    res['strongest_inharmonic_peak_hz_on'] = float(fr[i])
    res['strongest_inharmonic_peak_db_re_strongest_partial_on'] = float(10 * np.log10(Pn[i] / top))
    j = np.argmax(np.where(out, Pf, 0))
    res['strongest_inharmonic_peak_hz_off'] = float(fr[j])
    res['strongest_inharmonic_peak_db_re_strongest_partial_off'] = float(10 * np.log10(Pf[j] / top))
    return res

if __name__ == '__main__':
    result = dict(glide=[glide(k, v) for k in (33, 45, 60) for v in (40, 92)],
                  sidebands=[sidebands(k, v) for k in (24, 33, 45) for v in (92, 127)])
    for r in result['glide']: print(json.dumps(r))
    for r in result['sidebands']: print(json.dumps(r))
    out = Path(__file__).resolve().parents[2] / 'out/piano-mvp-v4/tension-measurements-0.5.2.json'
    out.write_text(json.dumps(result, indent=2) + '\n')
