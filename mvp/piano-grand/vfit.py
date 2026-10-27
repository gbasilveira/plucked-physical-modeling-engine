# SPDX-License-Identifier: MIT
"""Piano 0.7c: shared-pole vector fitting (one-sided complex Sanathanan-Koerner iterations with the relaxed non-triviality
constraint, Gustavsen and Semlyen 1999 / Gustavsen 2006; the QR trick of the fast multiport variant). The functions are the
port-to-microphone transfer functions on a positive-frequency grid; the poles are common to all of them.
Only numpy is used."""
import numpy as np

def basis(s, a):
    return 1.0/(s[:, None] - a[None, :])

def relocate(s, F, a, W=None):
    """One pole relocation. s: (nw,) 1j*omega; F: (nw, C) data (already weighted if weights is None); a: (P,) poles.
    Returns the new poles (stable, Im > 0 kept)."""
    P = len(a); nw, C = F.shape
    Phi = basis(s, a)
    Phi1 = np.concatenate([Phi, np.ones((nw, 1))], 1)           # (nw, P+1)
    blocks = []; rhs = []
    for c in range(C):
        f = F[:, c]; w = np.ones(nw) if W is None else W[:, c]
        A = w[:, None]*np.concatenate([Phi1, -f[:, None]*Phi1], 1)   # unknowns: r_c (P), d_c, rt (P), dt
        # real-valued unknowns are not required for the one-sided complex fit: complex unknowns
        Q, R = np.linalg.qr(A, mode='reduced')
        R22 = R[P + 1:, P + 1:]                                 # (P+1, P+1)
        blocks.append(R22)
        rhs.append(np.zeros(P + 1, complex))
    # non-triviality: sum over the grid of sigma(s) = nw  (relaxed): sum_i (Phi1[i] @ [rt, dt]) = nw
    cvec = Phi1.sum(0)                                         # (P+1,)
    scale = np.linalg.norm(F if W is None else W*F)/nw          # Gustavsen 2006
    Aall = np.concatenate([np.concatenate(blocks, 0), (scale*cvec)[None, :]], 0)
    bvec = np.concatenate([np.concatenate(rhs, 0), np.array([scale*nw])])
    # column scaling for conditioning
    cs = np.linalg.norm(Aall, axis=0); cs[cs == 0] = 1
    x = np.linalg.lstsq(Aall/cs, bvec, rcond=None)[0]/cs
    rt, dt = x[:P], x[P]
    if abs(dt) < 1e-8: dt = 1e-8 if dt == 0 else np.sign(dt.real)*1e-8
    H = np.diag(a) - np.outer(np.ones(P), rt)/dt                # eigenvalues = zeros of sigma
    # use the transpose arrangement for the standard form: poles = eig(diag(a) - ones * rt^T / dt)
    new = np.linalg.eigvals(H)
    new = np.where(new.real > 0, -new.real + 1j*new.imag, new)  # flip unstable
    return new

def residues(s, a, F, W=None):
    """Least-squares residues for fixed poles: F (nw, C) -> R (P, C), plus d (C)."""
    Phi = basis(s, a)
    Phi1 = np.concatenate([Phi, np.ones((len(s), 1))], 1)
    cs = np.linalg.norm(Phi1, axis=0)
    if W is None:
        X = np.linalg.lstsq(Phi1/cs, F, rcond=1e-14)[0]/cs[:, None]
    else:   # per-function frequency weights: separate solves
        X = np.zeros((Phi1.shape[1], F.shape[1]), complex)
        for c in range(F.shape[1]):
            X[:, c] = np.linalg.lstsq(W[:, c, None]*Phi1/cs, W[:, c]*F[:, c], rcond=1e-14)[0]/cs
    return X[:-1], X[-1]
