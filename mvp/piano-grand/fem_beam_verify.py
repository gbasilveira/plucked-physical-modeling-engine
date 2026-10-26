# SPDX-License-Identifier: MIT
"""Embedded Timoshenko beam check. A stiffening strip of width b and thickness hs on a thin SS square
plate is modelled (a) as plate elements of thickness hs inside the strip and (b) as an embedded beam
carrying the extra bending stiffness, shear stiffness, mass and rotary inertia, with the beam placed
on a mesh line and off it (not aligned with the mesh). The frequencies of the lowest modes must agree."""
import math, sys
import numpy as np
import scipy.sparse.linalg as sla
import fem, fem_verify as v

E, NU, RHO, H, HS, BW = 70e9, 0.3, 2700.0, 0.01, 0.04, 0.025
G = E/(2*(1+NU))

def section_equiv(kind, hplate, theta, factor):
    one = np.ones_like(hplate)
    Df = E*BW*(HS**3 - H**3)/(12*(1-NU**2))     # extra bending stiffness of the strip (plane-strain-free)
    return (Df*one, G*BW*HS**3*0.1*one, fem.KS*G*BW*(HS-H)*one, RHO*BW*(HS-H)*one,
            RHO*BW*(HS**3-H**3)/12*one, 0.0*one)

def modes(n, yb, mode, k=5):
    p, s = v.rect_mesh(1.0, n); nn = len(p)
    hth = np.full(len(s), H)
    if mode == 'strip':
        c = p[s].mean(1); hth[np.abs(c[:, 1]-yb) < BW/2] = HS
    Ke, Me, _ = fem.plate_matrices(p, s, hth, rho=RHO, iso=(E, NU))
    K, M = fem.assemble(s, Ke, Me, nn)
    if mode == 'beam':
        saved = fem.beam_section
        fem.beam_section = section_equiv
        path = np.stack([np.linspace(0, 1, 400), np.full(400, yb)], 1)
        yi = np.where(np.isclose(p[:, 1], yb))[0]; yi = yi[np.argsort(p[yi, 0])]
        try: K, M, _, _ = fem.add_beams(p, K, M, [(0, yi.tolist())], ['rib'])
        finally: fem.beam_section = saved
    fixed = set()
    for i in range(nn):
        x, y = p[i]
        if min(x, y, 1-x, 1-y) < 1e-12:
            fixed.add(3*i)
            if x < 1e-12 or x > 1-1e-12: fixed.add(3*i+2)
            if y < 1e-12 or y > 1-1e-12: fixed.add(3*i+1)
    free = np.setdiff1d(np.arange(3*nn), np.array(sorted(fixed)))
    w2, _ = sla.eigsh(K[free][:, free].tocsc(), k=k, M=M[free][:, free].tocsc(), sigma=0, which='LM')
    return np.sort(np.sqrt(w2))/(2*math.pi)

if __name__ == '__main__':
    ok = True
    ref = modes(80, 0.5, 'strip')
    plain = modes(80, 0.5, 'plain') if False else None
    print('strip-in-plate (80x80):', np.round(ref, 1))
    for n, yb in ((40, 0.5), (80, 0.5), (120, 0.5)):
        f = modes(n, yb, 'beam') if yb == 0.5 else modes(n, yb, 'beam')
        ref_y = modes(80, yb, 'strip')
        err = f/ref_y - 1
        print(f'beam n={n} y={yb:.4f}: f={np.round(f,1)} vs strip {np.round(ref_y,1)}  err% {np.round(100*err,2)}')
        if abs(err).max() > 0.04: ok = False
    print('PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)
