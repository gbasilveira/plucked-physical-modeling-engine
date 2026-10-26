#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Collect the step-2 fit results into one JSON and a table. report_fit.py CAPTURE_DIR OUT.json [--cost-json COST]"""
import sys, json, subprocess
import numpy as np
import bakelib as B
D, out = sys.argv[1], sys.argv[2]
rows = []
for k in (36, 60, 84):
    for v in (38, 76, 114):
        P = '%s/k%d_v%d' % (D, k, v)
        r = json.load(open(P + '.json'))
        ref = B.read_wav_float(P + '.wav')[1].mean(1)
        own = B.read_wav_float(P + '.floor.wav')[1].mean(1)      # Full force alone through the same baked radiation
        fit = B.read_wav_float(P + '.fit.wav')[1].mean(1)
        def env(a, b, floor):
            e = B.octave_envelope_error(a, b, floor_db=floor)
            vals = [x[0] for x in e.values() if x[2] > 0]
            return float(np.mean(vals)), float(np.max(vals))
        a60, a80 = env(own, fit, -60), env(own, fit, -80)
        b60 = env(ref, own, -60); c60 = env(ref, fit, -60)
        rows.append(dict(key=k, vel=v, partials=r['partials'], scored=r['partials_scored'], components=r['components'], components_unpruned=r['components_before_prune'],
                         cents_rms=r['cents_rms'], cents_max=r['cents_max'], decay_rms=r['decay_err_dbps_rms'], decay_max=r['decay_err_dbps_max'],
                         partial_env_median=r['env_err_db_median'], partial_env_weighted=r['env_err_db_weighted'],
                         force_rel_db=r['force_rel_rms_db'],
                         fit_env_mean60=a60[0], fit_env_max60=a60[1], fit_env_mean80=a80[0], fit_env_max80=a80[1],
                         missing_sympathetic_mean60=b60[0], missing_sympathetic_max60=b60[1], total_mean60=c60[0], total_max60=c60[1]))
json.dump(rows, open(out, 'w'), indent=1)
print('key vel | partials(scored) comps/unpruned | cents rms/max | decay dB/s rms/max | force dB | octave env (fit only) mean/max @-60 (@-80) | missing sympathetic mean/max | total mean/max')
for x in rows:
    print('%3d %3d | %3d(%2d) %3d/%3d | %.2f/%.2f | %.2f/%.2f | %.1f | %.2f/%.2f (%.2f/%.2f) | %.2f/%.2f | %.2f/%.2f' % (
        x['key'], x['vel'], x['partials'], x['scored'], x['components'], x['components_unpruned'], x['cents_rms'], x['cents_max'], x['decay_rms'], x['decay_max'],
        x['force_rel_db'], x['fit_env_mean60'], x['fit_env_max60'], x['fit_env_mean80'], x['fit_env_max80'], x['missing_sympathetic_mean60'], x['missing_sympathetic_max60'], x['total_mean60'], x['total_max60']))
