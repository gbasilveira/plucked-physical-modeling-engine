# SPDX-License-Identifier: MIT
"""Piano 0.7b stage 1: statistical continuation of the FEM soundboard above its converged band (3.6 kHz).

Physics (all from the same orthotropic Reissner-Mindlin plate as fem.py, nothing fitted to a recording):
 * dispersion  omega(k, theta) of the lowest branch of the homogeneous orthotropic Mindlin plate (3x3 Hermitian K(k) v = omega^2 M v, built from
   the plate-bending, twist and through-thickness-shear stiffness; shear moduli G_RT, G_LT of fem.py). The second branch starts at about 16 kHz.
 * modal density from the Weyl count of that dispersion (N(f) = A/(4 pi^2) oint k^2/2 dtheta - P k_mean/(4 pi) for a clamped edge); it is
   anchored to the FEM count in the last converged band (ribs and bridges change the constant, not the slope).
 * mobility: Re Y_k(f) = <phi_k^2> n_f / 4 for mass-normalised modes of density n_f (Cremer-Heckl / Skudrzyk: the modal sum of Lorentzians);
   the per-key target is the FEM band-averaged real part of the bridge-point mobility in the reference band, continued with the log-log slope the
   FEM shows over the converged octaves (clipped to [-1/2, 0]; -1/2 is the infinite-Euler-beam trend of the bridge, Cremer-Heckl-Petersson).
 * mode shapes: random superposition of P plane waves on the dispersion contour (direction pdf proportional to the density of states of the
   contour), random phases, amplitude weight (rho h(x,y))^(-1/2) with the local thickness, a clamped-edge taper sin(k d) within a quarter wavelength
   of the rim, mass-normalised on the surface grid. The participation phi at the 88 string points is the field itself (so neighbouring keys are
   correlated as a diffuse field is, J0(k r)), scaled per key and 1/3-octave band so that the realised mobility equals the target.
 * loss factor eta(f): flat 0.02 (as the FEM and 0.6; the continuity choice). A rising option is available (eta0 (f/3600)^p), flagged as an untested
   literature-direction (modal loss factors of soundboards are reported to rise with frequency; not verified against the papers here).
"""
import math
import numpy as np
from scipy.interpolate import interp1d
import geometry as geo
import fem

EL, ER, GLR, NU_LR = fem.EL, fem.ER, fem.GLR, fem.NU_LR
RHO, G_LT, G_RT = fem.RHO, fem.G_LT, fem.G_RT
KAPPA = 5.0/6.0
ETA0 = 0.02

def stiffness_matrix(k, theta, h):
    """Hermitian 3x3 K(k, theta) for the unknowns (W, Psi_x, Psi_y) of a plane wave exp(-j k n.r), n = (cos theta, sin theta); x = across grain."""
    c, s = np.cos(theta), np.sin(theta)
    Q = fem.plane_stress_Q()*h**3/12.0
    kk = np.asarray(k, float)
    # strain operator B (5x3): kx, ky, kxy, gx, gy
    B = np.zeros(np.broadcast(kk, c).shape + (5, 3), complex)
    B[..., 0, 1] = -1j*kk*c; B[..., 1, 2] = -1j*kk*s; B[..., 2, 1] = -1j*kk*s; B[..., 2, 2] = -1j*kk*c
    B[..., 3, 0] = -1j*kk*c; B[..., 3, 1] = 1.0; B[..., 4, 0] = -1j*kk*s; B[..., 4, 2] = 1.0
    C = np.zeros((5, 5)); C[:3, :3] = Q; C[3, 3] = KAPPA*G_RT*h; C[4, 4] = KAPPA*G_LT*h
    return np.einsum('...ji,jl,...lm->...im', B.conj(), C, B)

def omega_of_k(k, theta, h):
    """Lowest-branch angular frequency omega(k, theta) (vectorised over broadcast k, theta)."""
    K = stiffness_matrix(k, theta, h)
    Mm = np.diag([RHO*h, RHO*h**3/12, RHO*h**3/12])
    Mi = np.diag(1/np.sqrt(np.diag(Mm)))
    A = Mi @ K @ Mi
    w2 = np.linalg.eigvalsh(A)[..., 0]
    return np.sqrt(np.maximum(w2, 0))

class Dispersion:
    """k(omega, theta) by interpolation of the monotonic omega(k) for a thickness h."""
    def __init__(self, h, nth=181, kmax=900.0, nk=1600):
        self.h = h; self.th = np.linspace(0, 2*np.pi, nth)
        self.kg = np.concatenate([[0.0], np.geomspace(0.5, kmax, nk)])
        self.og = omega_of_k(self.kg[None, :], self.th[:, None], h)          # (nth, nk)
    def k(self, omega, theta=None):
        th = self.th if theta is None else np.asarray(theta)
        out = np.empty(np.broadcast(omega, th).shape)
        thi = self.th
        om = np.asarray(omega, float)
        for i, t in enumerate(np.atleast_1d(th)):
            j = int(np.argmin(np.abs(thi - (t % (2*np.pi)))))
            out.flat[i] = np.interp(om if om.ndim == 0 else om.flat[i], self.og[j], self.kg)
        return out

def weyl_density(h, f, area, perimeter, disp=None, df=1.0):
    """Modes per Hz: derivative of N(f) = A/(4 pi^2) oint k^2/2 dtheta - P <k>/(4 pi)  (a clamped rim; the perimeter term is small)."""
    d = disp or Dispersion(h)
    def N(ff):
        om = 2*np.pi*ff
        k = np.array([np.interp(om, d.og[j], d.kg) for j in range(len(d.th))])
        a = np.trapezoid(k**2/2, d.th)*area/(4*np.pi**2)
        return a - perimeter*np.mean(k)/(4*np.pi), a
    f = np.atleast_1d(f)
    n = np.array([(N(x + df/2)[0] - N(x - df/2)[0])/df for x in f])
    return n

def fem_mobility(f_modes, phi, sigma_scale=0.01, f_eval=None):
    """Real part of the bridge-point mobility per key from the modal sum, evaluated on f_eval (Hz). phi: (nkeys, nmodes). Returns (nkeys, nf)."""
    om = 2*np.pi*f_modes; sg = sigma_scale*om
    w = 2*np.pi*np.asarray(f_eval)
    out = np.zeros((phi.shape[0], len(w)))
    p2 = phi**2
    for j, ww in enumerate(w):
        lor = ww*ww*2*sg/((om**2 - ww*ww)**2 + 4*sg**2*ww*ww)
        out[:, j] = p2 @ lor
    return out

def third_octave_edges(f0, f1):
    n0 = int(np.floor(3*np.log2(f0/1000.0))); n1 = int(np.ceil(3*np.log2(f1/1000.0)))
    e = 1000.0*2**(np.arange(n0, n1 + 1)/3.0)
    return e[(e >= f0*0.999) & (e <= f1*1.001)]

# ------------------------------------------------------------------------------------------------------------------------------
class StatBoard:
    """A set of statistical board modes: frequencies f (Hz), plane-wave components (N, P) of wavevector kx, ky, phase psi, edge-layer wavenumber kbar,
    and the normalisation. field(i0, i1, X, Y) evaluates the mass-normalised mode shapes (N, npts)."""
    def __init__(self, f, kx, ky, psi, kbar, norm, beta_rib=0.0, rib_phase=None):
        self.f, self.kx, self.ky, self.psi, self.kbar, self.norm = f, kx, ky, psi, kbar, norm
        self.beta_rib, self.rib_phase = beta_rib, (np.zeros(len(f)) if rib_phase is None else rib_phase)
    def raw(self, idx, X, Y):
        """Unnormalised field of modes idx at points (X, Y) (board frame)."""
        idx = np.atleast_1d(idx)
        ph = self.kx[idx][:, :, None]*X[None, None, :] + self.ky[idx][:, :, None]*Y[None, None, :] + self.psi[idx][:, :, None]
        F = np.cos(ph).sum(axis=1)/np.sqrt(self.kx.shape[1])
        if self.beta_rib:
            F *= 1 + self.beta_rib*np.cos(2*np.pi*Y[None, :]/RIB_PERIOD + self.rib_phase[idx][:, None])
        return F
    def shape(self, idx, X, Y, d_edge, h_loc):
        """Mass-normalised shapes (len(idx), npts) using the stored norm: includes thickness weight and clamped edge layer."""
        idx = np.atleast_1d(idx)
        F = self.raw(idx, X, Y)
        kb = self.kbar[idx][:, None]
        layer = 1 - (1 + kb*d_edge[None, :])*np.exp(-kb*d_edge[None, :])
        return F*layer*(RHO*h_loc[None, :])**-0.5/self.norm[idx][:, None]

RIB_PERIOD = float(np.diff(geo.RIB_Y).mean())

def surface_grid(spacing):
    xs = np.arange(spacing/2, 1.4, spacing); ys = np.arange(spacing/2, geo.LEN_Y, spacing)
    X, Y = np.meshgrid(xs, ys, indexing='ij'); ok = geo.inside(X, Y, 0.0)
    X, Y = X[ok], Y[ok]
    return X, Y, geo.edge_distance(X, Y), geo.thickness(X, Y)

def make_statboard(f_lo, f_hi, seed, nplane=40, dens_scale=1.0, beta_rib=0.0, norm_spacing=0.01):
    """Draw the statistical modes between f_lo and f_hi (spacing: gamma(2) distributed around 1/n(f), the Wigner-like repulsion of an irregular plate)."""
    rng = np.random.default_rng(seed)
    X, Y, d_edge, h_loc = surface_grid(norm_spacing); dA = norm_spacing**2
    hbar = float(np.mean(h_loc)); disp = Dispersion(hbar)
    area = geo.area(); per = float(np.sum(np.hypot(*np.diff(geo.outline(0.01), axis=0, append=geo.outline(0.01)[:1]).T)))
    fg = np.arange(f_lo, f_hi + 50, 25.0); ng = weyl_density(hbar, fg, area, per, disp)*dens_scale
    f_modes = []
    f = f_lo
    while True:
        n = np.interp(f, fg, ng)
        f += rng.gamma(2.0, 1.0/(2.0*n))
        if f > f_hi: break
        f_modes.append(f)
    f_modes = np.array(f_modes); N = len(f_modes)
    kx = np.zeros((N, nplane)); ky = np.zeros((N, nplane)); psi = rng.uniform(0, 2*np.pi, (N, nplane)); kbar = np.zeros(N)
    th = disp.th
    for i, fm in enumerate(f_modes):
        om = 2*np.pi*fm
        k = np.array([np.interp(om, disp.og[j], disp.kg) for j in range(len(th))])
        dk = np.array([(np.interp(om*1.01, disp.og[j], disp.kg) - np.interp(om*0.99, disp.og[j], disp.kg))/(0.02*om) for j in range(len(th))])
        pdf = k*dk; cdf = np.cumsum(pdf); cdf /= cdf[-1]
        thp = np.interp(rng.uniform(0, 1, nplane), cdf, th)
        kp = np.interp(thp, th, k)
        kx[i] = kp*np.cos(thp); ky[i] = kp*np.sin(thp); kbar[i] = np.average(k, weights=pdf)
    sb = StatBoard(f_modes, kx, ky, psi, kbar, np.ones(N), beta_rib, rng.uniform(0, 2*np.pi, N))
    # mass normalisation  integral rho h phi^2 dA = 1  on the surface grid
    for i0 in range(0, N, 50):
        idx = np.arange(i0, min(N, i0 + 50))
        S = sb.shape(idx, X, Y, d_edge, h_loc)
        sb.norm[idx] = np.sqrt(np.sum(RHO*h_loc[None, :]*S**2*dA, axis=1))
    return sb

# ------------------------------------------------------------------------------------------------------------------------------
F_REF_LO, F_REF_HI = 2200.0, 3200.0      # FEM reference band for the mobility target (converged, away from the 3.6 kHz edge by the modal bandwidth)

def mobility_target(f_modes, phi_ports, eta=ETA0, ref=(F_REF_LO, F_REF_HI), fit_lo=1100.0):
    """Per-key target Re Y_k(f) above the FEM band: the FEM value averaged over the reference band (log mean over 1/3-octave points), continued
    with the median log-log slope over 1.1-3.2 kHz clipped to [-1/2, 0]. Returns (T0 (nkeys,), f0, slope)."""
    fe = np.geomspace(fit_lo, ref[1], 20)
    Y = fem_mobility(f_modes, phi_ports, eta/2, fe)
    sl = np.polyfit(np.log(fe), np.log(Y).T, 1)[0]
    slope = float(np.clip(np.median(sl), -0.5, 0.0))
    sel = (fe >= ref[0])
    f0 = float(np.exp(np.mean(np.log(fe[sel]))))
    T0 = np.exp(np.mean(np.log(Y[:, sel]) - slope*np.log(fe[sel] / f0), axis=1))
    return T0, f0, slope

def eta_of_f(f, eta0=ETA0, p=0.0, f_edge=3600.0):
    return eta0*np.maximum(f/f_edge, 1.0)**p

def calibrate(sb, phi_ports_raw, T0, f0, slope, eta_p=0.0, nb_edges=None):
    """Per key and 1/3-octave band scale c so that the realised mobility of the mode set equals the target. phi_ports_raw (nkeys, N).
    Returns phi (scaled) and the table of c."""
    f = sb.f; om = 2*np.pi*f
    edges = third_octave_edges(f[0], f[-1]) if nb_edges is None else nb_edges
    edges = np.concatenate([[f[0]*0.999], edges[(edges > f[0]) & (edges < f[-1])], [f[-1]*1.001]])
    phi = phi_ports_raw.copy(); ctab = np.ones((phi.shape[0], len(edges) - 1))
    for b in range(len(edges) - 1):
        lo, hi = edges[b], edges[b + 1]; fc = np.sqrt(lo*hi)
        evalf = np.geomspace(lo, hi, 7)
        sel = np.where((f >= lo/1.15) & (f < hi*1.15))[0]
        sg = 0.5*eta_of_f(f[sel], p=eta_p)*om[sel]
        w = 2*np.pi*evalf
        real = np.zeros(phi.shape[0])
        for ww in w:
            lor = ww*ww*2*sg/((om[sel]**2 - ww*ww)**2 + 4*sg**2*ww*ww)
            real += (phi_ports_raw[:, sel]**2) @ lor
        real /= len(w)
        target = T0*(fc/f0)**slope
        c = np.sqrt(target/np.maximum(real, 1e-30)); ctab[:, b] = c
        idx = np.where((f >= lo) & (f < hi))[0]
        phi[:, idx] = phi_ports_raw[:, idx]*c[:, None]
    return phi, ctab, edges

def build_hf_set(modes_npz, f_hi, seed=1, f_lo=None, nplane=40, eta_p=0.0, beta_rib=0.0, spacing=0.012, mobility_from=None):
    """Statistical mode set above the converged FEM band: returns dict with f, phi (88, N), the board parameters and the calibration."""
    d = np.load(modes_npz); fem_f = d['f']; phi_fem = d['phi']
    f_lo = float(fem_f[-1]) if f_lo is None else f_lo
    sb = make_statboard(f_lo, f_hi, seed, nplane, beta_rib=beta_rib)
    T0, f0, slope = mobility_target(fem_f, phi_fem) if mobility_from is None else mobility_from
    kx, ky = d['kx'], d['ky']
    # shape at the string points: no edge layer (all points are > 0.13 m from the rim), thickness weight at the point
    h_k = geo.thickness(kx, ky); dist_k = geo.edge_distance(kx, ky)
    idx = np.arange(len(sb.f))
    raw = np.zeros((88, len(idx)))
    for i0 in range(0, len(idx), 100):
        ii = idx[i0:i0 + 100]
        raw[:, ii] = sb.shape(ii, kx, ky, dist_k, h_k).T
    phi, ctab, edges = calibrate(sb, raw, T0, f0, slope, eta_p)
    return dict(sb=sb, phi=phi, phi_raw=raw, ctab=ctab, edges=edges, T0=T0, f0=f0, slope=slope)

def save_hf_set(path, hf):
    sb = hf['sb']
    np.savez(path, f=sb.f, kx=sb.kx, ky=sb.ky, psi=sb.psi, kbar=sb.kbar, norm=sb.norm, beta_rib=sb.beta_rib, rib_phase=sb.rib_phase,
             phi=hf['phi'], ctab=hf['ctab'], edges=hf['edges'], T0=hf['T0'], f0=hf['f0'], slope=hf['slope'])

def load_hf_set(path):
    d = np.load(path)
    sb = StatBoard(d['f'], d['kx'], d['ky'], d['psi'], d['kbar'], d['norm'], float(d['beta_rib']), d['rib_phase'])
    return dict(sb=sb, phi=d['phi'], ctab=d['ctab'], edges=d['edges'], T0=d['T0'], f0=float(d['f0']), slope=float(d['slope']))
