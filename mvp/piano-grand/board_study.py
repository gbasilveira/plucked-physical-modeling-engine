# SPDX-License-Identifier: MIT
"""Mesh convergence and model-sensitivity study of the grand soundboard FEM (Piano 0.7a, stage 1).
Usage: board_study.py convergence|variants [--work DIR]. Writes JSON to the work directory."""
import json, sys, time, math
from pathlib import Path
import numpy as np
import fem

WORK = Path('/home/gbas/trabalho/gbasilveira/piano-grand-work')
FMAX = 3500.0

def nmodes_for(fmax, margin=1.25): return int(0.105*fmax*margin) + 20

def convergence(hs=(0.04, 0.03, 0.02, 0.015, 0.01, 0.0075)):
    out = {}
    for h in hs:
        t = time.time(); m = fem.build_board(h); tb = time.time()-t
        f, V, ts = fem.solve_modes(m, nmodes=nmodes_for(FMAX))
        out[str(h)] = dict(info=m['info'], f=f.tolist(), build_s=tb, solve_s=ts)
        print(h, m['info'], f'build {tb:.1f}s solve {ts:.1f}s  f1={f[0]:.2f} f_last={f[-1]:.0f}', flush=True)
        (WORK/'convergence.json').write_text(json.dumps(out))
    return out

def variants(h=0.015):
    cases = {'baseline (clamped, Mindlin, ribs+bridges, variable h)': {},
             'Kirchhoff limit (shear x1e4)': dict(shear_scale=1e4),
             'edge rotation spring 2000 N m/rad/m': dict(theta_k=2000.0),
             'simply supported-like edge (spring 0)': dict(theta_k=0.0),
             'no ribs': dict(with_ribs=False),
             'no bridges': dict(with_bridges=False),
             'no ribs, no bridges (bare plate)': dict(with_ribs=False, with_bridges=False),
             'uniform thickness 8.2 mm': dict(thickness='uniform')}
    out = {}
    for name, kw in cases.items():
        m = fem.build_board(h, **kw)
        f, V, ts = fem.solve_modes(m, nmodes=nmodes_for(2500.0))
        out[name] = dict(f=f.tolist(), info=m['info'])
        print(f'{name:60s} f1..8 = {np.round(f[:8],1)}  N(<1kHz)={(f<1000).sum()} N(<2.5k)={(f<2500).sum()}', flush=True)
    (WORK/'variants.json').write_text(json.dumps(out))
    return out

if __name__ == '__main__':
    what = sys.argv[1]
    if what == 'convergence': convergence()
    elif what == 'variants': variants()
