# SPDX-License-Identifier: MIT
"""High-frequency radiation of the board modes to a microphone: top-face baffled Rayleigh integral
(jw rho / 2 pi) int v_n e^{-jkr}/r dA plus one specular reflection from the lid (image of the microphone in
the lid plane, valid where the specular point lies on the panel), with line-of-sight tests through the rigid
solid mask of the FDTD scene (a blocked path contributes nothing: no diffraction model). Used above the
FDTD crossover. Per mode i the result is the pressure per unit modal velocity, G_i(f) = (j w rho / 2 pi) int phi_i e^{-jkr}/r dA.
"""
import math
import numpy as np
from scipy.interpolate import LinearNDInterpolator
import geometry as geo, scene

RHO, C = 1.2, 343.0

def surface_points(spacing=0.012):
    xs = np.arange(spacing/2, 1.4, spacing); ys = np.arange(spacing/2, geo.LEN_Y, spacing)
    X, Y = np.meshgrid(xs, ys, indexing='ij')
    ok = geo.inside(X, Y, 0.0)
    return X[ok], Y[ok], spacing*spacing

def los_clear(solid_kji, a, b, ignore=None):
    """a: (n,3) world points, b: (3,) or (n,3). True where the straight segment crosses no solid cell."""
    a = np.atleast_2d(a); b = np.broadcast_to(np.asarray(b, float), a.shape)
    nsteps = 160
    t = (np.arange(nsteps) + 0.5)/nsteps
    ok = np.ones(len(a), bool)
    for tt in t:
        P = a + (b - a)*tt
        i = ((P[:, 0] - scene.X0)//scene.DX).astype(int); j = ((P[:, 1] - scene.Y0)//scene.DX).astype(int); k = (P[:, 2]//scene.DX).astype(int)
        inb = (i >= 0) & (i < scene.NX) & (j >= 0) & (j < scene.NY) & (k >= 0) & (k < scene.NZ)
        s = np.zeros(len(a), bool); s[inb] = solid_kji[k[inb], j[inb], i[inb]] > 0
        if ignore is not None: s &= ~ignore(P)
        ok &= ~s
    return ok

def lid_geometry(lid):
    ang = math.radians(scene.LID_ANGLES[lid]); n = np.array([-math.sin(ang), 0.0, math.cos(ang)])
    h = np.array([scene.HINGE_X, 0.0, scene.Z_RIM_TOP])
    return ang, n, h

def kernels(lid, mic, freqs, solid_kji, Phi_pts, X, Y, dA, with_lid=True):
    """Returns K[f, mode] = (j w rho / 2 pi) sum_pts Phi_pts[mode, pt] w_pt e^{-jkr}/r  (including the lid path)."""
    P = np.stack([X, Y + scene.Y_BOARD, np.full_like(X, scene.KB*scene.DX)], 1)    # board points in world
    m = np.asarray(scene.MICS[mic] if isinstance(mic, str) else mic, float)
    paths = []     # list of (r, valid mask)
    r0 = np.linalg.norm(P - m, axis=1)
    # top face is the only radiator; a point whose direct path is blocked contributes nothing
    v0 = los_clear(solid_kji, P + np.array([0, 0, 0.5*scene.DX]), m)
    paths.append((r0, v0))
    if with_lid and lid != 'closed':
        ang, n, h = lid_geometry(lid)
        dm = np.dot(m - h, n)
        if dm < 0:                        # mic on the board side of the lid plane
            mi = m - 2*dm*n               # image of the microphone
            ri = np.linalg.norm(P - mi, axis=1)
            # specular point
            dP = (P - h) @ n; t = dP/(dP - np.dot(mi - h, n))
            S = P + (mi - P)*t[:, None]
            u = (S - h) @ np.array([math.cos(ang), 0.0, math.sin(ang)])
            yb = S[:, 1] - scene.Y_BOARD
            width = geo.right_edge(np.clip(yb, 0, geo.LEN_Y)) + scene.RIM_T - scene.HINGE_X
            onlid = (u >= 0) & (u <= width) & (S[:, 1] >= 0.35) & (S[:, 1] <= scene.Y_BOARD + geo.LEN_Y + scene.RIM_T)
            ign = lambda Q: np.abs((Q - h) @ n) < 2.0*scene.DX
            v1 = onlid & los_clear(solid_kji, P + np.array([0, 0, 0.5*scene.DX]), S, ign) & los_clear(solid_kji, S, m, ign)
            paths.append((ri, v1))
    K = np.zeros((len(freqs), Phi_pts.shape[0]), complex)
    for fi, f in enumerate(freqs):
        k = 2*math.pi*f/C
        acc = np.zeros(len(P), complex)
        for r, v in paths: acc += np.where(v, np.exp(-1j*k*r)/r, 0.0)
        K[fi] = (Phi_pts @ (acc*dA))*(1j*2*math.pi*f*RHO/(2*math.pi))
    return K
