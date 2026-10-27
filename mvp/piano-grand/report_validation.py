# SPDX-License-Identifier: MIT
"""Piano 0.7b: tables from grand-validation-0.7b.json (0.6 Full, A0 = 0.7a, A1..A4 ablation, A4 = 0.7b) against Salamander.
Usage: report_validation.py VALIDATION.json OUT.txt"""
import json, sys
import numpy as np

BANDS = ['62-125', '125-250', '250-500', '500-1k', '1k-2k', '2k-4k', '4k-8k', '8k+']
CB = ['125', '250', '500', '1k', '2k', '4k']
NAMES = {'0.6': '0.6 Full', 'A0': '0.7a', 'A1': '+HF board', 'A2': '+strings', 'A3': '+plate/fine/diffraction',
         'A3n': '  (A3 without Rayleigh level matching)', 'A4': '0.7b (+admittance)'}

def db(a, b): return 10*np.log10(np.maximum(a, 1e-30)/np.maximum(b, 1e-30))

def main(path, out):
    d = json.load(open(path)); rows = d['rows']; variants = [v for v in NAMES if v in next(iter(rows.values()))]
    L = []; keys = list(rows)
    def stack(v, metric): return np.array([rows[k][v][metric] for k in keys])
    ref = {m: stack('reference', m) for m in ('shares_late', 'shares_attack', 'decay', 'coherence', 'attack')}
    L.append('Late window (0.5-1.5 s) octave-band energy-share error, dB re Salamander (mean signed / mean absolute over %d notes: A2 C4 C6 x velocity 40 60 92)' % len(keys))
    L.append('%-26s' % 'model' + ' '.join('%8s' % b for b in BANDS) + '   mean|e| 62-500 / 500-2k / 2k-8k+ / all')
    agg = {}
    for v in variants:
        e = db(stack(v, 'shares_late'), ref['shares_late']); agg[v] = e
        L.append('%-26s' % (NAMES[v] + ' signed') + ' '.join('%+8.1f' % x for x in e.mean(0)))
        a = np.abs(e)
        L.append('%-26s' % (NAMES[v] + ' |.|') + ' '.join('%8.1f' % x for x in a.mean(0)) + '   %.1f / %.1f / %.1f / %.1f' % (a[:, :3].mean(), a[:, 3:5].mean(), a[:, 5:].mean(), a.mean()))
    L.append('')
    L.append('Attack window (0.01-0.25 s) share error, mean |dB|:')
    for v in variants:
        e = np.abs(db(stack(v, 'shares_attack'), ref['shares_attack'])); L.append('%-26s' % NAMES[v] + ' '.join('%8.1f' % x for x in e.mean(0)))
    L.append('')
    L.append('Decay rate (dB/s) difference to reference, mean over notes, bands ' + ' '.join(CB) + ' (reference mean: ' + ' '.join('%.0f' % x for x in np.nanmean(ref['decay'], 0)) + ')')
    for v in variants:
        e = stack(v, 'decay') - ref['decay']; L.append('%-26s' % NAMES[v] + ' '.join('%+8.1f' % x for x in np.nanmean(e, 0)) + '   mean |e| %.1f' % np.nanmean(np.abs(e)))
    L.append('')
    L.append('Attack spectrum: centroid 0-30 ms (Hz) and share above 2 kHz, mean over notes; reference: centroid %.0f, share %.3f' % (ref['attack'][:, 0].mean(), ref['attack'][:, 1].mean()))
    for v in variants:
        a = stack(v, 'attack'); L.append('%-26s centroid %7.0f   share>2k %.3f   centroid 30-100 ms %7.0f' % (NAMES[v], a[:, 0].mean(), a[:, 1].mean(), a[:, 2].mean()))
    L.append('')
    L.append('L/R coherence (0.1-2 s) by band ' + ' '.join(CB) + ' ; mean over notes, and mean |difference| to the reference (reference: ' + ' '.join('%.2f' % x for x in ref['coherence'].mean(0)) + ')')
    for v in variants:
        c = stack(v, 'coherence'); L.append('%-26s' % NAMES[v] + ' '.join('%8.2f' % x for x in c.mean(0)) + '   |diff| ' + ' '.join('%.2f' % x for x in np.abs(c - ref['coherence']).mean(0)))
    L.append('')
    L.append('Ablation: change of the mean absolute late-share error (dB) when each addition is made (negative = closer to the reference)')
    prev = None
    for v in variants:
        if v in ('0.6', 'A3n'): continue
        a = np.abs(agg[v])
        if prev is not None:
            L.append('%-26s' % ('-> ' + NAMES[v]) + ' '.join('%+8.1f' % x for x in (a.mean(0) - np.abs(agg[prev]).mean(0))) + '   all %+.2f' % (a.mean() - np.abs(agg[prev]).mean()))
        prev = v
    L.append('')
    L.append('Per note (late-share error, dB): ' + ' '.join(BANDS))
    for i, k in enumerate(keys):
        for v in ('0.6', 'A0', 'A4' if 'A4' in variants else variants[-1]):
            L.append('%-9s %-18s' % (k, NAMES[v]) + ' '.join('%+8.1f' % x for x in db(np.array(rows[k][v]['shares_late']), np.array(rows[k]['reference']['shares_late']))))
    open(out, 'w').write('\n'.join(L) + '\n'); print('\n'.join(L))

if __name__ == '__main__': main(*sys.argv[1:3])
