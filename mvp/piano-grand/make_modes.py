# SPDX-License-Identifier: MIT
"""Compute the production in-vacuo mode set of the grand soundboard and the per-key participation table.
Usage: make_modes.py H FMAX OUT.npz   (H: mesh size in m; modes up to FMAX Hz are kept)"""
import sys, time, json
import numpy as np
from scipy.interpolate import LinearNDInterpolator
import fem, geometry as geo

def main(h, fmax, out, theta_k=None):
    t0 = time.time()
    m = fem.build_board(h, theta_k=theta_k)
    nm = int(0.105*fmax*1.25) + 20
    f, V, ts = fem.solve_modes(m, nmodes=nm)
    keep = f <= fmax
    f = f[keep]; W = V[0::3, :][:, keep]          # w at nodes, mass-normalised eigenvector (x^T M x = 1)
    p = m['p']
    kp = geo.key_points()
    interp = LinearNDInterpolator(p, W)
    phi = interp(np.stack([kp['x'], kp['y']], 1))   # (88, nmodes)
    assert np.isfinite(phi).all()
    np.savez(out, f=f, nodes=p, tri=m['s'], W=W.astype(np.float32), key=kp['key'], kx=kp['x'], ky=kp['y'],
             kbridge=kp['bridge'], ks=kp['s'], phi=phi, info=json.dumps(m['info']), h=h, theta_k=-1 if theta_k is None else theta_k)
    print(f'h={h}: {len(f)} modes up to {f[-1]:.0f} Hz, f1={f[0]:.2f} Hz; build+solve {time.time()-t0:.0f}s (solve {ts:.0f}s)')
    print(m['info'])

if __name__ == '__main__':
    main(float(sys.argv[1]), float(sys.argv[2]), sys.argv[3])
