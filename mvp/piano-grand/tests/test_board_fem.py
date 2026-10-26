# SPDX-License-Identifier: MIT
"""Piano 0.7a tests G-T1 (mesh/mode convergence regression, small mesh) and G-T2 (element verification).
Needs numpy and scipy; exits 77 (ctest SKIP) when scipy is missing. Usage: test_board_fem.py [--quick]"""
import json, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
try:
    import numpy as np, scipy  # noqa
except ImportError:
    print('numpy/scipy not available: skipped'); sys.exit(77)
import fem, fem_verify, fem_beam_verify, geometry as geo

def main():
    ref = json.loads((HERE/'board_reference_modes.json').read_text())
    ok = True
    # G-T2: element against the exact simply supported Mindlin plate (thin and thick) and the embedded beam against a thick strip
    E, nu, rho, a = 70e9, 0.3, 2700.0, 1.0
    for h in (0.002, 0.1):
        ex = sorted(fem_verify.exact_ss(a, h, E, nu, rho, m, n) for m in range(1, 4) for n in range(1, 4))[:4]
        f = fem_verify.ss_fem(a, h, E, nu, rho, 24, 4)
        err = np.abs(f/np.array(ex) - 1).max()
        print(f'G-T2 SS plate h/a={h}: max error {100*err:.2f} % (24x24x2 mesh)'); ok &= err < 0.02
    f = fem_beam_verify.modes(60, 0.5, 'beam'); r = fem_beam_verify.modes(60, 0.5, 'strip')
    err = np.abs(f/r - 1).max(); print(f'G-T2 embedded beam vs thick strip: max error {100*err:.2f} %'); ok &= err < 0.06
    # G-T1: geometry and a coarse-mesh regression against the production (h = 7.5 mm) frequencies
    area = geo.area(); print(f'board area {area:.3f} m2'); ok &= abs(area - ref['area']) < 1e-3
    m = fem.build_board(0.03)
    f, V, _ = fem.solve_modes(m, nmodes=24)
    fr = np.array(ref['f'][:24]); err = np.abs(f/fr - 1)
    print(f'G-T1 h = 0.03 m: first 24 modes vs production mesh: max {100*err.max():.2f} %, median {100*np.median(err):.2f} % (limits 8 % / 3.5 %)')
    ok &= err.max() < 0.08 and np.median(err) < 0.035
    ok &= abs(m['info']['plate_mass'] + m['info']['beam_mass'] - ref['mass']) < 0.35
    print('mass', m['info']['plate_mass'] + m['info']['beam_mass'], 'kg (reference', ref['mass'], ')')
    print('PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)

if __name__ == '__main__': main()
