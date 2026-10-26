# SPDX-License-Identifier: MIT
"""Piano 0.7b test G-T9 (crossover continuity of the statistical board): a 20 mm FEM board gives the modes below 2.4 kHz. The statistical continuation
is built from the FEM modes below 1.8 kHz only (its mobility target is taken from 1.3-1.8 kHz) and must reproduce the held-out FEM modes of
1.9-2.3 kHz: modal density within 20 %, band-averaged driving-point mobility within 3 dB (median over the keys), and a step of the combined
mobility across the junction below 2 dB. Also checks the Mindlin dispersion limits (thin-plate bending branch below 300 Hz) and the mass
normalisation of the random shapes. Usage: test_hf_board.py"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    import numpy as np, scipy  # noqa
except ImportError:
    print('numpy/scipy not available: skipped'); sys.exit(77)
import fem, hfboard as H, geometry as geo

def main():
    ok = True
    # dispersion: isotropic thin plate limit  k^4 = omega^2 rho h / D  at low frequency (shear and rotary inertia negligible)
    h = 0.009; d = H.Dispersion(h, nth=13, kmax=300.0, nk=500)
    D_across = H.ER*h**3/12/(1 - H.NU_LR*H.NU_LR*H.ER/H.EL)        # across the grain (theta = 0), thin plate on the principal axis with the orthotropic coupling ignored
    om = 2*np.pi*150.0
    k0 = np.interp(om, d.og[0], d.kg)
    k_thin = (om**2*H.RHO*h/D_across)**0.25
    print(f'Mindlin k across grain at 150 Hz: {k0:.2f} rad/m, thin plate {k_thin:.2f}')
    ok &= abs(k0/k_thin - 1) < 0.1
    m = fem.build_board(0.02); f, V, _ = fem.solve_modes(m, nmodes=300)
    keep = f <= 2400.0; f = f[keep]
    W = V[0::3, :][:, keep]
    kp = geo.key_points()
    from scipy.interpolate import LinearNDInterpolator
    phi = LinearNDInterpolator(m['p'], W)(np.stack([kp['x'], kp['y']], 1)); assert np.isfinite(phi).all()
    cut = 1800.0; lo = f <= cut
    T0, f0, slope = H.mobility_target(f[lo], phi[:, lo], ref=(1300.0, 1800.0), fit_lo=700.0)
    sb = H.make_statboard(cut, 2400.0, 5, 40)
    h_k = geo.thickness(kp['x'], kp['y']); dist_k = geo.edge_distance(kp['x'], kp['y'])
    raw = np.zeros((88, len(sb.f)))
    for i0 in range(0, len(sb.f), 50):
        ii = np.arange(i0, min(len(sb.f), i0 + 50)); raw[:, ii] = sb.shape(ii, kp['x'], kp['y'], dist_k, h_k).T
    phis, _, _ = H.calibrate(sb, raw, T0, f0, slope)
    # density
    held = (f > 1900) & (f < 2300); n_fem = held.sum()/400.0
    n_stat = ((sb.f > 1900) & (sb.f < 2300)).sum()/400.0
    print(f'modal density 1.9-2.3 kHz: FEM {n_fem:.3f} /Hz, statistical {n_stat:.3f} /Hz')
    ok &= abs(n_stat/n_fem - 1) < 0.20
    fe = np.geomspace(1900, 2300, 12)
    Yfem = H.fem_mobility(f, phi, 0.01, fe); Ystat = H.fem_mobility(sb.f, phis, 0.01, fe)
    d_db = 10*np.log10(Ystat.mean(axis=1)/Yfem.mean(axis=1))
    print(f'mobility 1.9-2.3 kHz statistical over held-out FEM: median {np.median(d_db):.2f} dB, p10..p90 {np.percentile(d_db,10):.2f}..{np.percentile(d_db,90):.2f}')
    ok &= abs(np.median(d_db)) < 3.0
    # step across the junction: FEM below the cut + statistical above
    fa = np.geomspace(1500, 1790, 8); fb = np.geomspace(1810, 2100, 8)
    Ya = H.fem_mobility(f[lo], phi[:, lo], 0.01, fa) + H.fem_mobility(sb.f, phis, 0.01, fa)
    Yb = H.fem_mobility(f[lo], phi[:, lo], 0.01, fb) + H.fem_mobility(sb.f, phis, 0.01, fb)
    step = np.median(10*np.log10(Yb.mean(axis=1)/Ya.mean(axis=1)))
    print(f'combined mobility step across the junction: {step:.2f} dB'); ok &= abs(step) < 2.0
    # mass normalisation of the shapes (integral rho h phi^2 dA = 1 on an independent grid)
    X, Y, de, hl = H.surface_grid(0.015); S_ = sb.shape(np.arange(0, len(sb.f), 40), X, Y, de, hl)
    nrm = np.sum(H.RHO*hl[None, :]*S_**2*0.015**2, axis=1); print('shape norms', np.round(nrm, 3)[:5]); ok &= np.all(np.abs(nrm - 1) < 0.15)
    print('PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)

if __name__ == '__main__': main()
