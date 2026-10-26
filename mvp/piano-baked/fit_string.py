#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""0.6 step 2: fit a cheap live string (attack table + damped complex sinusoids) to a Full bridge-force capture.
   fit_string.py CAPTURE_PREFIX KEY OUT.fit [--prune 1e-6] [--report OUT.json]"""
import sys, json, argparse
import numpy as np
import bakelib as B

ap = argparse.ArgumentParser()
ap.add_argument('prefix'); ap.add_argument('key', type=int); ap.add_argument('out')
ap.add_argument('--prune', type=float, default=1e-8)
ap.add_argument('--order', type=int, default=4)
ap.add_argument('--report')
ap.add_argument('--ta', type=float, default=0.1)
a = ap.parse_args()

F = np.fromfile(a.prefix + '.force.f64')
fit = B.fit_force(F, a.key, ta=a.ta, prune=a.prune, max_order=a.order)
comps = fit['components']
nfade = 960
Fh = B.synth_force(fit, F, nfade)

# ---- metrics ----
# (1) partial frequencies from an independent peak pick (1.5 s .. 6 s) of the Full force and of the resynthesis
def peaks(sig, parts, t0=1.5, t1=6.0, nfft=1 << 20):
    seg = sig[int(t0 * B.FS):int(t1 * B.FS)]
    mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft)); df = B.FS / nfft
    out = []
    for k, fk, _ in parts:
        hw = max(1.0, 0.002 * fk)
        lo, hi = int((fk - hw) / df), int((fk + hw) / df)
        i = lo + int(np.argmax(mag[lo:hi + 1]))
        out.append((B.parabolic(mag, i) * df, mag[i]))
    return out
pf, ph = peaks(F, fit['partials']), peaks(Fh, fit['partials'])
sel = np.array([f[1] >= max(x[1] for x in pf) * 10 ** (-50 / 20) for f in pf])   # partials within 50 dB of the strongest (1.5-6 s)
cents = np.array([1200 * np.log2(h[0] / f[0]) for f, h in zip(pf, ph)])
amp_db = np.array([20 * np.log10(h[1] / f[1]) for f, h in zip(pf, ph)])
# (2) per partial band envelope, slope (dB/s) over 0.3..6 s and rms envelope error over 0.1..8 s
slope_err, env_err, wts = [], [], []
Fw = F.copy(); Fhw = Fh.copy()
for (k, fk, mg), info in zip(fit['partials'], fit['info']):
    W = info['W']
    y, dt, _ = B.band_signal(F, fk, W); yh, _, _ = B.band_signal(Fh, fk, W)
    t = np.arange(len(y)) * dt
    ed = 20 * np.log10(np.abs(y) + 1e-12); eh = 20 * np.log10(np.abs(yh) + 1e-12)
    m = (t >= 0.3) & (t < 6.0)
    sl = np.polyfit(t[m], ed[m], 1)[0]; slh = np.polyfit(t[m], eh[m], 1)[0]
    slope_err.append(slh - sl)
    m2 = (t >= 0.1) & (t < 8.0) & (ed > ed.max() - 60)
    env_err.append(float(np.sqrt(((eh[m2] - ed[m2]) ** 2).mean())) if m2.sum() else float('nan'))
    wts.append(float((np.abs(y) ** 2).sum()))
slope_err = np.array(slope_err)[sel]; env_err = np.array(env_err)[sel]; wts = np.array(wts)[sel]; cents = cents[sel]; amp_db = amp_db[sel]
# (3) octave-band envelope error of the force itself
ob = B.octave_envelope_error(F, Fh)
rel = float(np.sqrt(((F - Fh) ** 2).sum() / (F ** 2).sum()))
rel_tail = float(np.sqrt(((F[int(0.2 * B.FS):] - Fh[int(0.2 * B.FS):]) ** 2).sum() / (F[int(0.2 * B.FS):] ** 2).sum()))
rep = dict(key=a.key, partials_scored=int(sel.sum()), prefix=a.prefix, partials=len(fit['partials']), components=len(comps), components_before_prune=fit['all_components'],
           f0=fit['f0'], B=fit['B'], prune=a.prune,
           cents_rms=float(np.sqrt((cents ** 2).mean())), cents_max=float(np.abs(cents).max()), cents_median_abs=float(np.median(np.abs(cents))),
           partial_amp_db_rms=float(np.sqrt((amp_db ** 2).mean())),
           decay_err_dbps_rms=float(np.sqrt((slope_err ** 2).mean())), decay_err_dbps_max=float(np.abs(slope_err).max()),
           env_err_db_median=float(np.nanmedian(env_err)), env_err_db_weighted=float(np.nansum(env_err * wts) / wts.sum()), env_err_db_p90=float(np.nanpercentile(env_err, 90)),
           octave_env_err_force={str(int(c)): v[0] for c, v in ob.items()},
           force_rel_rms_db=20 * np.log10(rel), force_rel_rms_after_200ms_db=20 * np.log10(rel_tail))
print(json.dumps(rep, indent=1))
if a.report: json.dump(rep, open(a.report, 'w'), indent=1)

# ---- export ----
ta = int(round(a.ta * B.FS))
with open(a.out, 'w') as f:
    f.write('FIT 1\n%d %d %d %d %d %d\n' % (a.key, int(B.FS), ta, nfade, ta + nfade, len(comps)))
    f.write('\n'.join('%.9g' % x for x in F[:ta + nfade]) + '\n')
    for k, fr, sg, am in comps:
        f.write('%.12g %.9g %.9g %.9g\n' % (fr, sg, am.real, am.imag))
