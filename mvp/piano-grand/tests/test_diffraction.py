# SPDX-License-Identifier: MIT
"""Piano 0.7b test G-T7 (diffraction): (1) the knife-edge factor K(v) of rayleigh_b against the exact Sommerfeld solution for a plane wave on a
rigid half-plane (sound-hard wedge of angle 0, in the paraxial region around the shadow line); (2) the ray-marching depth h, d1, d2 of
Scene.leg_coefficient on a synthetic wall, against the analytic knife-edge geometry. Usage: test_diffraction.py"""
import sys, math
try:
    import numpy as np
    from scipy.special import fresnel
except ImportError:
    print("numpy/scipy not available: skipped"); sys.exit(77)
sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1]))
import rayleigh_b as R, scene_b as S

def sommerfeld_hard(kr, phi, phip):
    """Total field (exp(-i w t) convention, unit plane wave arriving from angle phip, hard half-plane along phi = 0 and 2 pi)."""
    def G(a):
        Sv, Cv = fresnel(a*math.sqrt(2/math.pi))      # int_0^a exp(i t^2) dt = sqrt(pi/2) (C(a sqrt(2/pi)) + i S(...))
        integral = math.sqrt(math.pi/2)*(Cv + 1j*Sv) + 0.5*math.sqrt(math.pi)*np.exp(1j*math.pi/4)
        return np.exp(-1j*math.pi/4)/math.sqrt(math.pi)*integral
    U = lambda psi: np.exp(-1j*kr*math.cos(psi))*G(math.sqrt(2*kr)*math.cos(psi/2))
    return U(phi - phip) + U(phi + phip)

def test_knife_edge_vs_sommerfeld():
    worst = 0.0
    for kr in (60.0, 200.0, 800.0):
        for dphi in np.linspace(-0.25, 0.25, 11):
            phip = math.radians(60.0); phi = math.pi + phip + dphi                  # near the shadow boundary, away from the face
            inc = np.exp(-1j*kr*math.cos(phi - phip))                                    # undisturbed incident wave
            ex = abs(sommerfeld_hard(kr, phi, phip))/1.0
            v = math.sqrt(kr/math.pi)*math.sin(dphi)
            ap = abs(R.knife_edge(v))
            err = 20*math.log10(ap/ex)
            worst = max(worst, abs(err))
    print(f'knife edge vs Sommerfeld, worst |error| = {worst:.2f} dB (kr 60..800, +-14 deg around the shadow line)')
    assert worst < 1.2, worst      # Kirchhoff-Fresnel is paraxial: v uses sin(dphi) where the exact solution has 2 sin(dphi/2)

def test_ray_geometry():
    """A rigid wall (label 1) rising to the rim top across the whole domain at y = 0.5..0.6; source and receiver on both sides; the leg
    coefficient c must equal h sqrt(2 (d1+d2)/(d1 d2)) with h the depth below the wall top."""
    g = S.FINE
    sc = R.Scene.__new__(R.Scene); sc.g = g; sc.lid = 'open'; sc.plate = False
    sc.lab = np.zeros((g.NX, g.NY, g.NZ), np.uint8)
    j0 = int((0.5 - g.Y0)/g.dx); j1 = int((0.6 - g.Y0)/g.dx)
    k_top = int(S.Z_RIM_TOP/g.dx); sc.lab[:, j0:j1, :k_top] = 1
    sc.plate_dist = np.zeros((g.NX, g.NY)); sc.lid_n = np.array([0, 0, 1.0]); sc.lid_ang = 0.0
    A = np.array([[0.5, 0.2, 0.80]]); B = np.array([0.5, 1.2, 0.60])         # wall crossed at y = 0.55, ray height there ~ 0.8 - 0.2*(0.35/1.0) = 0.73
    c = R.Scene.leg_coefficient(sc, A, B)[0]
    L = np.linalg.norm(B - A[0]); u = (B - A[0])/L
    t = (0.55 - A[0, 1])/(B[1] - A[0, 1]); zc = A[0, 2] + t*(B[2] - A[0, 2])
    h = (S.Z_RIM_TOP - zc)*math.sqrt(1 - u[2]**2); d1, d2 = t*L, (1 - t)*L
    ce = h*math.sqrt(2*(d1 + d2)/(d1*d2))
    print(f'leg coefficient {c:.3f} vs analytic {ce:.3f}')
    assert abs(c - ce)/ce < 0.12, (c, ce)
    A2 = np.array([[0.5, 0.2, 1.3]]); c2 = R.Scene.leg_coefficient(sc, A2, np.array([0.5, 1.2, 1.2]))[0]
    assert c2 == R.H_CLEAR                                                                # clears the wall: lit

if __name__ == '__main__':
    test_knife_edge_vs_sommerfeld(); test_ray_geometry(); print('ok')
