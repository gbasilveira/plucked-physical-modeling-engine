# SPDX-License-Identifier: MIT
"""Piano 0.7c: fit the 0.7b port-to-microphone impulse responses E[mic, port, lag] (one lid state) to the real-time bank.

Model (what piano-v4/grand_live.cpp runs, per lid):
  E[m, k, n] ~ Re sum_p R[m, k, p] lam_p^n                       (low range, up to ~4 kHz: complex one-pole resonators)
             + sum_j phi_hf[k, j] Re[ W[m, j] lam_j^(n - D[m]) ]   (high range: the exact statistical modes of hfboard.py)
The low range is E - E_hf (E_hf: the statistical part, saved by assemble_b.py with the sidecar option). Poles come from a subspace
(ESPRIT / matrix pencil) estimate on the 8 kHz-decimated low range of all ports and microphones; residues from a linear least-squares fit on the
DFT bins of the 1 s responses (basis exact for the truncated sequence); the high range is not fitted, its weights follow from the stored
amplitudes of the statistical modes (assembly sidecar), see hf_weights().
Usage: fit_live.py WORK LID NPOLES2     (NPOLES2 = real order of the Hankel subspace: 2 x pole pairs)"""
import sys, time, json
from pathlib import Path
import numpy as np
import scipy.signal as sg

FS = 48000.0
FMAX_LF = 4000.0

def hankel_gram(x, L):
    """sum over channels of H H^T for the Hankel matrices (L rows) of the rows of x (channels, n)."""
    n = x.shape[1] - L + 1
    idx = np.arange(L)[:, None] + np.arange(n)[None, :]
    G = np.zeros((L, L))
    for ch in range(x.shape[0]):
        H = x[ch][idx]; G += H @ H.T
    return G

def esprit_poles(Elf, order, dec=6, L=1800, port_step=1):
    """Continuous poles (rad/s, Im > 0) from the low range Elf (mics, ports, n at 48 kHz)."""
    G = np.zeros((L, L))
    for m in range(Elf.shape[0]):
        x = sg.decimate(Elf[m, ::port_step].astype(np.float64), dec, ftype='fir', axis=1, zero_phase=True)
        G += hankel_gram(x, L)
    ev, U = np.linalg.eigh(G); U = U[:, np.argsort(ev)[::-1]][:, :order]
    Psi = np.linalg.lstsq(U[:-1], U[1:], rcond=None)[0]
    s = (FS/dec)*np.log(np.linalg.eigvals(Psi).astype(complex))
    a = s[s.imag > 0]
    a = np.where(a.real > 0, -a.real + 1j*a.imag, a)            # a few weak poles come out marginally unstable: reflect
    a = a[a.imag/(2*np.pi) <= FMAX_LF*1.02]
    return a[np.argsort(a.imag)]

def basis(lam, N, b):
    """DFT-bin responses of Re[R lam^n], n < N, per unit complex R: (B R + C conj R)/2 with B, C below."""
    th = 2*np.pi*b/N
    ph = np.exp(-1j*th)
    B = (1 - lam[None, :]**N)/(1 - lam[None, :]*ph[:, None])
    C = (1 - np.conj(lam)[None, :]**N)/(1 - np.conj(lam)[None, :]*ph[:, None])
    return B, C

def fit_residues(Elf, lam, fmax=FMAX_LF):
    """Least squares complex residues R[m, k, p] of the real model Re sum R lam^n against Elf (mics, ports, N) on the DFT bins 1..fmax."""
    M, K, N = Elf.shape
    F = np.fft.rfft(Elf.astype(np.float64), axis=2)
    b = np.arange(1, int(fmax*N/FS) + 1)
    B, C = basis(lam, N, b)
    P = len(lam)
    A = np.concatenate([np.concatenate([((B + C)/2).real, (1j*(B - C)/2).real], 1), np.concatenate([((B + C)/2).imag, (1j*(B - C)/2).imag], 1)], 0)
    Y = F[:, :, b].reshape(M*K, -1).T
    rhs = np.concatenate([Y.real, Y.imag], 0)
    cs = np.linalg.norm(A, axis=0)
    X = np.linalg.lstsq(A/cs, rhs, rcond=1e-13)[0]/cs[:, None]
    R = (X[:P] + 1j*X[P:]).T.reshape(M, K, P)
    return R

def synth_lf(R, lam, N):
    """Time response Re sum R lam^n, n < N: (mics, ports, N) float32 (memory friendly: loop over mics and port groups)."""
    M, K, P = R.shape
    n = np.arange(N)
    out = np.zeros((M, K, N), np.float32)
    # lam^n for all poles (P, N) complex128 would be big for N = 48000; use blocks of poles
    for p0 in range(0, P, 50):
        pw = lam[p0:p0+50, None]**n[None, :]                    # (50, N)
        for m in range(M):
            out[m] += (R[m, :, p0:p0+50].real @ pw.real - R[m, :, p0:p0+50].imag @ pw.imag).astype(np.float32)
    return out

def hf_weights(side, fs=FS):
    """Weights of the statistical modes (sidecar of assemble_b.py): E_hf[m, k, n] = sum_j phi[k, j] Re[W[m, j] lam_j^(n - D[m])].
    One-sided spectrum A Y(w) exp(-j w tau0) with Y = j w/(w_j^2 - w^2 + 2j sigma w), sigma = 0.01 w_j, rendered by irfft: for a narrow resonance
    the analytic part gives x(t) = Re[(A a/(j w_d)) exp(a (t - tau0))] and irfft returns T x(nT)."""
    A = side['A']; tau0 = side['tau0']; f = side['f_hf']
    om = 2*np.pi*f; sg_ = 0.5*0.02*om; wd = np.sqrt(om**2 - sg_**2); a = -sg_ + 1j*wd
    T = 1/fs
    D = np.rint(tau0*fs).astype(int); delta = tau0 - D*T
    W = T*(A*a[None, :]/(1j*wd[None, :]))*np.exp(-a[None, :]*delta[:, None])
    lam = np.exp(a*T)
    # a velocity-type resonance has a zero at DC: the first sample of each mode carries the feedthrough that makes the response sum to zero,
    # y[n] = Re[W X[n]] + e delta[n] with e = -Re[W/(1 - lam)] (X[n] = lam X[n-1] + delta[n], so h[0] = Re W + e, h[n>=1] = Re[W lam^n])
    e = -(W/(1 - lam[None, :])).real
    return lam, W, D, e

def synth_hf(phi_hf, lam, W, D, N, e=None):
    """(mics, ports, N) float32 of the high-range model."""
    M = W.shape[0]; K = phi_hf.shape[0]
    out = np.zeros((M, K, N), np.float32)
    for m in range(M):
        d = int(D[m]); n = np.arange(N - d)
        for j0 in range(0, len(lam), 100):
            pw = (lam[j0:j0+100, None]**n[None, :])*W[m, j0:j0+100, None]
            pr = pw.real.copy()
            if e is not None: pr[:, 0] += e[m, j0:j0+100]
            out[m, :, d:] += (phi_hf[:, j0:j0+100] @ pr).astype(np.float32)
    return out

BANDS = [(20, 62.5), (62.5, 125), (125, 250), (250, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000)]

def band_report(ref, test, fs=FS):
    """Octave-band relative error (dB) of test against ref over all ports and mics, and the level difference (dB)."""
    Fr = np.fft.rfft(ref.astype(np.float64), axis=-1); Ft = np.fft.rfft(test.astype(np.float64), axis=-1)
    f = np.fft.rfftfreq(ref.shape[-1], 1/fs); out = []
    for lo, hi in BANDS:
        k = (f >= lo) & (f < hi)
        er = np.sum(np.abs(Fr[..., k])**2); ee = np.sum(np.abs(Fr[..., k] - Ft[..., k])**2); et = np.sum(np.abs(Ft[..., k])**2)
        out.append(dict(lo=lo, hi=hi, err_db=float(10*np.log10(ee/er + 1e-30)), level_diff_db=float(10*np.log10((et + 1e-30)/(er + 1e-30)))))
    return out

def main(work, lid, order, step=1):
    work = Path(work); t0 = time.time()
    E = np.load(work/f'irc_{lid}.npz')['E']; side = np.load(work/f'irc_{lid}_hf.npz')
    Elf = (E - side['Ehf']).astype(np.float64)
    a = esprit_poles(Elf, order, port_step=step)
    lam = np.exp(a/FS)
    print(lid, 'poles', len(a), f'{time.time()-t0:.0f}s', flush=True)
    R = fit_residues(Elf, lam)
    lf = synth_lf(R, lam, Elf.shape[2])
    print('lf fit vs E - E_hf (bands):', ' '.join('%.1f' % r['err_db'] for r in band_report(Elf, lf)), f'{time.time()-t0:.0f}s', flush=True)
    np.savez(work/f'fit/lf_{lid}_{order}.npz', a=a, R=R)
    hfd = np.load(work/'hfboard_0.7b16.npz', allow_pickle=True)
    lamh, W, D, e = hf_weights(side)
    hf = synth_hf(hfd['phi'], lamh, W, D, E.shape[2], e)
    rep = band_report(side['Ehf'], hf)
    print('hf model vs E_hf (bands):', ' '.join('%.1f' % r['err_db'] for r in rep), f'{time.time()-t0:.0f}s', flush=True)
    tot = lf + hf
    rep = band_report(E, tot)
    print('total vs E (bands):', ' '.join('%.1f' % r['err_db'] for r in rep))
    print('level diff       :', ' '.join('%.2f' % r['level_diff_db'] for r in rep))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]))
