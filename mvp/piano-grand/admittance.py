# SPDX-License-Identifier: MIT
"""Piano 0.7b stage 4: the full air-loaded bridge admittance (88 x 88) from the modal radiation impedance.

Method. In the FDTD the board is driven kinematically (grand_fdtd --mode kin): mode j carries a velocity pulse in its own time slot and the
generalised force of the air on every mode i is recorded. With e^{+j w t}, modal velocities u and mass-normalised vacuum modes,
        D(w) u + Z(w) u = F_ext ,   D = diag(j w + 2 sigma_i + w_i^2 /(j w)),   F_air = - Z u ,
so the air-loaded modal admittance is Y(w) = (D + Z)^-1 and the bridge-point admittance Y_port = Phi Y Phi^T (Phi: 88 x N, mass-normalised
participations). Z is complex symmetric (reciprocity of the wave equation) and Re Z is positive semi-definite (the air takes energy); both are
checked on the data (tests/test_admittance.py on a synthetic box, and the report on the real board).
Reduction for the strings' board table (omega, sigma, phi per mode, real oscillators): for every mode n the quadratic eigenproblem
(s^2 + s (2 Sigma + Z(w_n)) + Omega^2) x = 0 with Z frozen at the mode's frequency gives the loaded pole s = -sigma_L + j w_L and its modal
vector; the real oscillator keeps the pole and the port participation phi_L = Re(rotated Phi x sqrt(kappa)); the neglected imaginary part (the
non-proportional part of the loading) is reported as an error of the reconstructed Y_port against the exact one.
"""
import math, sys
from pathlib import Path
import numpy as np

SLOT = 1500

def load_kin_runs(work, prefix, starts, nrows, per_run=16):
    """Returns (Z_t[j] arrays of shape (SLOT, nrows) force on every mode per unit velocity pulse, the pulse samples, dt) keyed by driven mode j."""
    out = {}; dt = None
    for j0 in starts:
        base = Path(work)/f'{prefix}{j0:03d}'
        if not Path(str(base) + '.meta.txt').exists(): continue
        meta = dict(l.split('=') for l in open(str(base) + '.meta.txt').read().split()); dt = float(meta['dt'])
        steps, nm = int(meta['steps']), int(meta['nmodes'])
        F = np.fromfile(str(base) + '.fkin.f32', np.float32).reshape(steps, nm).astype(np.float64)
        u = np.fromfile(str(base) + '.usrc.f32', np.float32).astype(np.float64)
        for s in range(per_run):
            sl = slice(s*SLOT, (s + 1)*SLOT)
            if (s + 1)*SLOT > steps: break
            out[j0 + s] = (F[sl, :nrows], u[sl])
    return out, dt

def impedance_matrix(runs, dt, n, nfft=1 << 15):
    """Z(f) (nf, n, n) in the e^{+jwt} convention for the driven modes 0..n-1 (rows: force on mode i). Half-sample alignment of the staggered
    velocity (n + 1/2) and force (n) records included. (A DFT ratio of real records is the s = j w transfer function: no conjugation.)"""
    f = np.fft.rfftfreq(nfft, dt); w = 2*np.pi*f
    Z = np.zeros((len(f), n, n), complex)
    for j in range(n):
        F, u = runs[j]
        Fd = np.fft.rfft(F[:, :n], nfft, axis=0); Ud = np.fft.rfft(u, nfft)
        Zj = -Fd*np.exp(1j*w*dt/2)[:, None]/Ud[:, None]
        Z[:, :, j] = Zj                                                   # the DFT ratio is the engineering (s = j w) transfer function
    return f, Z

def symmetrise(Z):
    return 0.5*(Z + np.swapaxes(Z, 1, 2))

def loaded_admittance(f, Z, omega, sigma, Phi, fsel=None):
    """Y_port(f) = Phi (D + Z)^-1 Phi^T on the frequencies f (Hz) given; Z interpolated on the grid f_z. Returns (len(f), nport, nport)."""
    n = len(omega); w = 2*np.pi*np.asarray(fsel)
    out = np.zeros((len(w), Phi.shape[0], Phi.shape[0]), complex)
    for a, ww in enumerate(w):
        D = np.diag(1j*ww + 2*sigma + omega**2/(1j*ww))
        out[a] = Phi @ np.linalg.solve(D + Z[a], Phi.T)
    return out

def vacuum_admittance(omega, sigma, Phi, fsel):
    w = 2*np.pi*np.asarray(fsel)
    out = np.zeros((len(w), Phi.shape[0], Phi.shape[0]), complex)
    for a, ww in enumerate(w):
        out[a] = (Phi*(1j*ww/(omega**2 - ww**2 + 2j*sigma*ww))[None, :]) @ Phi.T
    return out

def passivity_report(Zs, Yp):
    """Smallest eigenvalue of the Hermitian part relative to its largest, and the asymmetry, for stacks of Z and Y_port matrices."""
    def stats(M):
        herm = 0.5*(M + np.conj(np.swapaxes(M, 1, 2))); ev = np.linalg.eigvalsh(herm)
        asym = np.linalg.norm(M - np.swapaxes(M, 1, 2), axis=(1, 2))/np.maximum(np.linalg.norm(M, axis=(1, 2)), 1e-300)
        return dict(min_over_max_eig=float(np.min(ev[:, 0]/np.maximum(ev[:, -1], 1e-300))), max_asym=float(asym.max()), median_asym=float(np.median(asym)))
    return dict(Z=stats(Zs), Yport=stats(Yp))

def reduce_modes(f, Z, omega, sigma, Phi, nuse=None):
    """Per mode loaded pole and real port participation (see module docstring). Returns omega_L, sigma_L, phi_L (nport, n), nonprop (n,)."""
    n = len(omega) if nuse is None else nuse
    N = len(omega); I = np.eye(N)
    oL = omega.copy(); sL = sigma.copy(); phiL = Phi.copy(); nonprop = np.zeros(N)
    for k in range(n):
        ia = int(np.argmin(np.abs(f - omega[k]/(2*np.pi)))); Zk = Z[ia]
        C = 2*np.diag(sigma) + Zk
        # companion: s [x; y] = A [x; y] with y = s x:  s^2 x + s C x + Om^2 x = 0
        A = np.block([[np.zeros((N, N)), I], [-np.diag(omega**2), -C]])
        ev, V = np.linalg.eig(A)
        cand = np.where(ev.imag > 0)[0]
        j = cand[np.argmin(np.abs(ev[cand].imag - omega[k]))]
        s = ev[j]; x = V[:N, j]
        sL[k] = -s.real; oL[k] = s.imag
        kappa = s/(x @ ((2*s*I + C) @ x))*(2j*s.imag/s)                     # residue scaled to the real-oscillator convention (-> 1 for Z = 0)
        w = (Phi @ x)*np.sqrt(kappa + 0j)
        th = 0.5*np.angle(np.sum(w**2)); w = w*np.exp(-1j*th)               # rotate so that sum w^2 is real positive: the best real direction
        phiL[:, k] = w.real; nonprop[k] = np.linalg.norm(w.imag)/max(np.linalg.norm(w), 1e-300)
    return oL, sL, phiL, nonprop
