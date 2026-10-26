# SPDX-License-Identifier: MIT
"""Piano 0.7b job queue for the near-field FDTD (resumable at run granularity; one job at a time, all cores).
Jobs (in this order): coarse reciprocal runs with the plate for the open lid; fine (dx 12.5 mm) reciprocal runs of the close microphones with and
without the plate (open lid); kinematic radiation-impedance runs (open lid, plate); coarse reciprocal runs with the plate for the half and closed lids.
Usage: run_fdtd_b.py WORKDIR MODES.npz [--only prefix]"""
import argparse, subprocess, sys, time
from pathlib import Path
import scene_b as S

ap = argparse.ArgumentParser(); ap.add_argument('work'); ap.add_argument('modes'); ap.add_argument('--only', default='')
a = ap.parse_args(); W = Path(a.work); EXE = str(W/'grand_fdtd_b')

def scene(tag, lid, grid, plate, fmax):
    sd = W/f'scene_{tag}'
    if not (sd/'scene.txt').exists():
        t = time.time(); print('building scene', tag, S.build(lid, a.modes, fmax, sd, grid, plate), f'{time.time()-t:.0f}s', flush=True)
    return sd

def run(name, sd, args, steps):
    base = W/name
    if Path(str(base) + '.meta.txt').exists(): print('skip', name, flush=True); return
    if a.only and not name.startswith(a.only): return
    t = time.time(); cmd = [EXE, '--scene', str(sd), '--out', str(base), '--steps', str(steps)] + args
    print(' '.join(cmd), flush=True)
    r = subprocess.run(cmd, stderr=open(str(base) + '.log', 'w'))
    print(f'{name}: exit {r.returncode} in {time.time()-t:.0f}s', flush=True)
    if r.returncode: sys.exit(1)

def recip(name, sd, g, mic, steps):
    cell = ','.join(map(str, S.mic_cell(g, S.MICS[mic])))
    run(name, sd, ['--mode', 'recip', '--src', cell, '--rec', cell], steps)

def jobs():
    g = S.COARSE
    sd = scene('openP', 'open', 'coarse', True, 2400.0)
    for m in S.MICS: recip(f'run_openP_{m}', sd, g, m, 24000)
    gf = S.FINE
    sdf = scene('F_openP', 'open', 'fine', True, 3000.0)
    for m in ('closeL', 'closeR'): recip(f'runF_openP_{m}', sdf, gf, m, 48000)
    for j0 in range(0, 144, 16):
        run(f'runK_openP_{j0:03d}', sd, ['--mode', 'kin', '--kin-first', str(j0), '--kin-count', '16', '--kin-slot', '1500'], 24000)
    for lid in ('half', 'closed'):
        sdl = scene(lid + 'P', lid, 'coarse', True, 2400.0)
        for m in S.MICS: recip(f'run_{lid}P_{m}', sdl, g, m, 24000)
    sdn = scene('F_openN', 'open', 'fine', False, 3000.0)
    for m in ('closeL', 'closeR'): recip(f'runF_openN_{m}', sdn, gf, m, 48000)

jobs(); print('all jobs done', flush=True)
