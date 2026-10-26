# SPDX-License-Identifier: MIT
"""Piano 0.7b high-frequency radiation of the board to a microphone: baffled Rayleigh integral over the top face with
  * the direct path and one specular reflection from the lid (image microphone, valid where the specular point lies on the panel),
  * a diffraction-aware weight on every leg instead of the 0.7a "blocked = zero" rule: the Kirchhoff / Fresnel knife-edge factor
        K(v) = (1+j)/2 * int_v^inf exp(-j pi t^2/2) dt ,   v = h sqrt(2 (d1 + d2) / (lambda d1 d2)),
    h the depth by which the straight leg is blocked beyond the nearest edge of the obstacle it crosses (rim top, lid edge, plate window edge), d1 and
    d2 the distances from the deepest blocked sample to the two ends. K -> 1 lit, 1/2 at the shadow line, ~ exp(-jk delta)/(j pi v sqrt 2) deep in shadow.
    It is exact for a rigid half-plane in the paraxial limit (test: tests/test_diffraction.py against the Sommerfeld solution).
Time convention exp(+j w t); the pressure per unit modal velocity is G_i(f) = (j w rho / 2 pi) sum_pts phi_i(pt) w_pt dA with w_pt the path sum.
"""
import math
import numpy as np
from scipy.special import fresnel
from scipy.ndimage import distance_transform_edt
import geometry as geo, scene_b as S

RHO, C = 1.2, 343.0
H_CLEAR = -1.0e3          # coefficient for an unobstructed leg (K = 1)

def knife_edge(v):
    """K(v) of the module docstring for real v (array)."""
    Sv, Cv = fresnel(np.asarray(v, float))
    return 0.5*(1 + 1j)*((0.5 - Cv) - 1j*(0.5 - Sv))

class Scene:
    """Obstacle labels on the fine grid plus what the diffraction depth needs."""
    def __init__(self, lid, plate=True, grid='fine'):
        self.g = S.GRIDS[grid]; self.lid = lid; self.plate = plate
        sol, inside, XX, YY = S.solid_mask(self.g, lid, plate)
        self.lab = np.ascontiguousarray(sol)                          # (NX, NY, NZ) uint8
        pm = S.plate_solid(XX, YY - S.Y_BOARD) if plate else np.zeros(XX.shape, bool)
        self.plate_dist = distance_transform_edt(pm)*self.g.dx        # distance (m) from a plate cell to the nearest open cell (in plane)
        ang = math.radians(S.LID_ANGLES[lid]); self.lid_n = np.array([-math.sin(ang), 0.0, math.cos(ang)])
        self.lid_ang = ang

    def lid_in_plane_distance(self, P):
        ca, sa = math.cos(self.lid_ang), math.sin(self.lid_ang)
        u = (P[:, 0] - S.HINGE_X)*ca + (P[:, 2] - S.Z_RIM_TOP)*sa
        yb = np.clip(P[:, 1] - S.Y_BOARD, 0, geo.LEN_Y)
        width = geo.right_edge(yb) + S.RIM_T - S.HINGE_X
        return np.maximum(0.0, np.minimum.reduce([u, width - u, P[:, 1] - 0.35, S.Y_BOARD + geo.LEN_Y + S.RIM_T - P[:, 1]]))

    def leg_coefficient(self, A, B, nsteps=200, ignore_lid=False):
        """c such that v = c / sqrt(lambda) for the leg A->B (arrays (n,3)); H_CLEAR where no solid cell is crossed."""
        A = np.atleast_2d(A); B = np.broadcast_to(np.asarray(B, float), A.shape)
        n = len(A); L = np.linalg.norm(B - A, axis=1); u = (B - A)/np.maximum(L, 1e-9)[:, None]
        hbest = np.zeros(n); tbest = np.full(n, 0.5); hit = np.zeros(n, bool)
        g = self.g
        for tt in (np.arange(nsteps) + 0.5)/nsteps:
            P = A + (B - A)*tt
            i = np.floor((P[:, 0] - g.X0)/g.dx).astype(int); j = np.floor((P[:, 1] - g.Y0)/g.dx).astype(int); k = np.floor(P[:, 2]/g.dx).astype(int)
            inb = (i >= 0) & (i < g.NX) & (j >= 0) & (j < g.NY) & (k >= 0) & (k < g.NZ)
            lab = np.zeros(n, np.uint8); lab[inb] = self.lab[i[inb], j[inb], k[inb]]
            if ignore_lid: lab[lab == 2] = 0
            if not lab.any(): continue
            h = np.zeros(n)
            m3 = lab == 3
            if m3.any(): h[m3] = (self.plate_dist[i[m3], j[m3]] + 0.5*g.dx)*np.abs(u[m3, 2])
            m2 = lab == 2
            if m2.any(): h[m2] = (self.lid_in_plane_distance(P[m2]) + 0.5*g.dx)*np.abs(u[m2] @ self.lid_n)
            m1 = lab == 1
            if m1.any(): h[m1] = (S.Z_RIM_TOP - P[m1, 2] + 0.5*g.dx)*np.sqrt(np.maximum(1 - u[m1, 2]**2, 0.0))
            better = (lab > 0) & (h >= hbest)
            hbest[better] = h[better]; tbest[better] = tt; hit |= lab > 0
        d1 = np.maximum(tbest*L, 0.01); d2 = np.maximum((1 - tbest)*L, 0.01)
        c = hbest*np.sqrt(2*(d1 + d2)/(d1*d2))
        return np.where(hit, c, H_CLEAR)

def surface_points(spacing):
    xs = np.arange(spacing/2, 1.4, spacing); ys = np.arange(spacing/2, geo.LEN_Y, spacing)
    X, Y = np.meshgrid(xs, ys, indexing='ij'); ok = geo.inside(X, Y, 0.0)
    return X[ok], Y[ok], spacing*spacing

class Paths:
    """Direct and lid-reflected paths from the board surface points to one microphone, with the obstruction coefficients of their legs."""
    def __init__(self, sc, mic, X, Y, with_lid=True):
        self.sc = sc; m = np.asarray(S.MICS[mic] if isinstance(mic, str) else mic, float); self.mic = m
        P = np.stack([X, Y + S.Y_BOARD, np.full_like(X, S.KB_Z)], 1) + np.array([0, 0, 0.5*sc.g.dx])
        self.r = [np.linalg.norm(P - m, axis=1)]
        self.legs = [[sc.leg_coefficient(P, m, ignore_lid=False)]]
        if with_lid and sc.lid != 'closed':
            n = sc.lid_n; h = np.array([S.HINGE_X, 0.0, S.Z_RIM_TOP]); dm = np.dot(m - h, n)
            if dm < 0:
                mi = m - 2*dm*n; ri = np.linalg.norm(P - mi, axis=1)
                dP = (P - h) @ n; t = dP/(dP - np.dot(mi - h, n)); Sp = P + (mi - P)*t[:, None]
                uu = (Sp - h) @ np.array([math.cos(sc.lid_ang), 0.0, math.sin(sc.lid_ang)]); yb = Sp[:, 1] - S.Y_BOARD
                width = geo.right_edge(np.clip(yb, 0, geo.LEN_Y)) + S.RIM_T - S.HINGE_X
                on = (uu >= 0) & (uu <= width) & (Sp[:, 1] >= 0.35) & (Sp[:, 1] <= S.Y_BOARD + geo.LEN_Y + S.RIM_T)
                c1 = sc.leg_coefficient(P, Sp, ignore_lid=True); c2 = sc.leg_coefficient(Sp, m, ignore_lid=True)
                self.r.append(np.where(on, ri, np.inf)); self.legs.append([c1, c2])
        self.P = P

    def weights(self, f):
        """Complex path sum  sum_paths K(v) exp(-jkr)/r  per surface point at frequency f."""
        k = 2*math.pi*f/C; lam = C/f; acc = np.zeros(len(self.r[0]), complex)
        for r, legs in zip(self.r, self.legs):
            w = np.ones(len(r), complex)
            for c in legs:
                w = w*np.where(c <= H_CLEAR + 1, 1.0, knife_edge(np.maximum(c, -50)/math.sqrt(lam)))
            fin = np.isfinite(r)
            acc[fin] += w[fin]*np.exp(-1j*k*r[fin])/r[fin]
        return acc

S.KB_Z = S.Z_BOARD
