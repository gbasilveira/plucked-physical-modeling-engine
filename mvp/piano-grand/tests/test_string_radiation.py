# SPDX-License-Identifier: MIT
"""Piano 0.7b test G-T6 (string radiation geometry and delay): the line-dipole gain of string_radiation.py against the analytic point dipole
(short string), the phase advance with distance (delay r/c), the rigid-plane image rule (tangential doubled, normal cancelled near the plane), and
the dipole directivity. Usage: test_string_radiation.py"""
import sys, math
from pathlib import Path
try:
    import numpy as np
except ImportError:
    print("numpy not available: skipped"); sys.exit(77)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import string_radiation as SR

def analytic_dipole(F, e, src, mic, f):
    k = 2*math.pi*f/SR.C; d = mic - src; r = np.linalg.norm(d)
    return F*(d @ e)/r*(1 + 1j*k*r)*np.exp(-1j*k*r)/(4*np.pi*r**2)

def short_string_gain(L, a, f, e, src, mic, zref=None, n=1, npts=200):
    ds = L/npts; xi = (np.arange(npts) + 0.5)*ds
    phi = math.sqrt(2/L)*np.sin(n*math.pi*(L - xi)/L)
    P = np.tile(src, (npts, 1)); P[:, 0] += xi - L/2                       # string along x, centred on src
    return SR.line_gain(P, ds, phi, f, e, mic, zref, a), phi.sum()*ds

def test_point_dipole_limit():
    L, a, f = 0.004, 0.5e-3, 1500.0
    src = np.array([0.0, 0.0, 0.7]); mic = np.array([0.3, 0.2, 0.9]); e = np.array([0, 0, 1.0])
    G, int_phi = short_string_gain(L, a, f, e, src, mic)
    F = SR.RHO0*math.pi*a*a*1j*2*math.pi*f*int_phi                          # force per unit modal velocity
    ref = analytic_dipole(F, e, src, mic, f)
    err = abs(G - ref)/abs(ref)
    print(f'short-string gain vs analytic point dipole: relative error {err:.2e}'); assert err < 1e-3

def test_delay_and_far_field_phase():
    L, a, f = 0.004, 0.5e-3, 3000.0; e = np.array([0, 0, 1.0]); src = np.array([0.0, 0.0, 0.7])
    r1, r2 = 4.0, 4.5
    m1 = src + r1*np.array([0.6, 0, 0.8]); m2 = src + r2*np.array([0.6, 0, 0.8])
    G1, _ = short_string_gain(L, a, f, e, src, m1); G2, _ = short_string_gain(L, a, f, e, src, m2)
    k = 2*math.pi*f/SR.C
    dphi = np.angle(G2/G1); expect = -k*(r2 - r1)
    d = (dphi - expect + math.pi) % (2*math.pi) - math.pi
    print(f'far-field phase advance error {d:.3f} rad; amplitude ratio {abs(G2/G1):.3f} (1/r: {r1/r2:.3f})')
    assert abs(d) < 0.03 and abs(abs(G2/G1) - r1/r2) < 0.01

def test_image_and_directivity():
    L, a, f = 0.004, 0.5e-3, 200.0; h = 0.01                                  # a very low frequency: the source sits 1 cm above the plane
    src = np.array([0.0, 0.0, 0.5 + h]); mic = np.array([2.0, 0, 0.5 + 1.0]); zref = 0.5
    ez = np.array([0, 0, 1.0]); ex = np.array([0, 1.0, 0])
    Gz0, _ = short_string_gain(L, a, f, ez, src, mic); Gzi, _ = short_string_gain(L, a, f, ez, src, mic, zref)
    Gh0, _ = short_string_gain(L, a, f, ex, src, mic + np.array([0, 1.0, 0])); Ghi, _ = short_string_gain(L, a, f, ex, src, mic + np.array([0, 1.0, 0]), zref)
    print(f'vertical: |with image| / |free| = {abs(Gzi)/abs(Gz0):.3f}; horizontal: {abs(Ghi)/abs(Gh0):.3f}')
    assert abs(Ghi)/abs(Gh0) > 1.85 and abs(Ghi)/abs(Gh0) < 2.1               # tangential force doubles
    assert abs(Gzi)/abs(Gz0) < 0.5                                              # normal force nearly cancels (kh << 1, not zero at this distance)
    # directivity: a vertical dipole radiates nothing on the horizontal plane through it (far field, free space)
    G90, _ = short_string_gain(L, a, 4000.0, ez, np.array([0, 0, 0.7]), np.array([3.0, 0, 0.7]))
    G0, _ = short_string_gain(L, a, 4000.0, ez, np.array([0, 0, 0.7]), np.array([0, 0, 3.7]))
    assert abs(G90)/abs(G0) < 1e-6

if __name__ == '__main__':
    test_point_dipole_limit(); test_delay_and_far_field_phase(); test_image_and_directivity(); print('ok')
