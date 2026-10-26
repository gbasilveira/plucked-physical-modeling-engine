# SPDX-License-Identifier: MIT
"""Piano 0.7a test G-T4 (energy / passivity of the coupled air + modal board scheme): in a closed rigid box (no sponge)
the discrete energy after a force pulse is conserved with a lossless board and decreases monotonically with a damped
board. Usage: test_fdtd_energy.py PATH/TO/grand_fdtd [WORKDIR]"""
import subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    import numpy as np
except ImportError:
    print("numpy not available: skipped"); sys.exit(77)
import test_fdtd_reciprocity as tr

def run_case(exe, d, sigma):
    d.mkdir(parents=True, exist_ok=True)
    tr.make_scene(d, sigma_val=sigma, sponge=0.0)
    steps = 5000
    subprocess.run([exe, '--scene', str(d), '--out', str(d/'e'), '--steps', str(steps), '--tau', '3e-4', '--mode', 'fwd', '--mode-index', '2', '--energy'],
                   check=True, stderr=subprocess.DEVNULL)
    e = np.fromfile(d/'e.energy.f32', np.float32).reshape(-1, 2).sum(1)
    return e

def windows(e, n=8):
    e = e[len(e)//6:]                                  # skip the pulse
    return np.array([w.mean() for w in np.array_split(e, n)]), e

def main():
    exe = sys.argv[1]; base = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(tempfile.mkdtemp())
    ok = True
    wm, e = windows(run_case(exe, base/'lossless', 0.0))
    spread = (e.max() - e.min())/e.mean(); drift = (wm[-1] - wm[0])/wm[0]
    print(f'lossless coupled box: mean energy {e.mean():.4e} J, air<->board oscillation spread {spread:.2e}, drift first->last window {drift:+.2e}')
    ok &= spread < 0.02 and abs(drift) < 2e-3
    wm2, e2 = windows(run_case(exe, base/'damped', 3.0))
    print(f'damped board: window means {np.array2string(wm2, precision=3)} J')
    ok &= bool(np.all(np.diff(wm2) < 0)) and wm2[-1] < 0.9*wm2[0]
    print('PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)

if __name__ == '__main__': main()
