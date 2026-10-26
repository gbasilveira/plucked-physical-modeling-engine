# SPDX-License-Identifier: MIT
"""Run the reciprocal near-field FDTD: one run per (lid state, microphone). Resumable at run granularity.
Usage: run_fdtd.py WORKDIR MODES.npz [--lids open,half,closed] [--mics closeL,...] [--steps 24000] [--fmax 2400]"""
import argparse, subprocess, sys, time, os
from pathlib import Path
import scene

ap = argparse.ArgumentParser()
ap.add_argument('work'); ap.add_argument('modes')
ap.add_argument('--lids', default='open,half,closed'); ap.add_argument('--mics', default=','.join(scene.MICS))
ap.add_argument('--steps', type=int, default=24000); ap.add_argument('--fmax', type=float, default=2400.0)
ap.add_argument('--tag', default='')
a = ap.parse_args()
work = Path(a.work)
for lid in a.lids.split(','):
    sd = work/f'scene_{lid}{a.tag}'
    if not (sd/'scene.txt').exists():
        print('building scene', lid, scene.build(lid, a.modes, a.fmax, sd), flush=True)
    for mic in a.mics.split(','):
        base = work/f'run_{lid}{a.tag}_{mic}'
        if Path(str(base)+'.meta.txt').exists():
            print('skip', base.name); continue
        cell = scene.mic_cell(scene.MICS[mic])
        t = time.time()
        cmd = [str(work/'grand_fdtd'), '--scene', str(sd), '--mode', 'recip', '--out', str(base), '--src', ','.join(map(str, cell)),
               '--rec', ','.join(map(str, cell)), '--steps', str(a.steps)]
        print(' '.join(cmd), flush=True)
        r = subprocess.run(cmd, stderr=open(str(base)+'.log', 'w'))
        print(f'{lid} {mic}: exit {r.returncode} in {time.time()-t:.0f}s', flush=True)
        if r.returncode: sys.exit(1)
