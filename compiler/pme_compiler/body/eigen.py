# SPDX-License-Identifier: MIT
"""Sparse generalised eigen-solve K phi = Omega^2 M phi up to f_max, mass-normalised (B2-R5).

Shift-invert Lanczos (SciPy ``eigsh``) over successive frequency windows: each window is centred on
the upper end of the previous one and returns the ``nev`` eigenvalues nearest its shift, so the
union of the windows covers [shift_0 - r_0, f_max] without gaps; eigenvalues found twice are
removed. Small systems use a dense solve. (SPEC-BODY "Modal data (offline output)"; ADR-0002)
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
import scipy.linalg as sla
import scipy.sparse as sp
import scipy.sparse.linalg as spla
from numpy.typing import NDArray

Array = NDArray[np.float64]

# Modes below this frequency (or below RIGID_REL of the largest kept Omega) are rigid-body modes.
RIGID_HZ = 1.0
RIGID_REL = 1.0e-6


@dataclass(frozen=True)
class Modes:
    """Eigenpairs sorted by ascending Omega; ``vectors[:, k]`` is M-orthonormal."""

    omega: Array  # [K] rad/s
    vectors: Array  # [n, K]
    n_rigid: int  # rigid-body (near-zero) modes dropped
    windows: int


def mass_normalise(vectors: Array, mass: sp.spmatrix) -> Array:
    """M-orthonormalise the columns of ``vectors`` so that Phi^T M Phi = I.

    # SPEC-BODY: mass normalisation, int rho h Phi_k Phi_l dA = delta_kl, with the full mass matrix.
    Each eigenvector is already M-orthogonal in exact arithmetic except inside degenerate clusters
    split across windows; a Cholesky re-orthonormalisation removes that residual.
    """
    gram = vectors.T @ (mass @ vectors)
    gram = 0.5 * (gram + gram.T)
    # normalise each column first (keeps the factorisation well conditioned), then fix the rest
    scale = 1.0 / np.sqrt(np.diag(gram))
    vectors = vectors * scale
    gram = gram * np.outer(scale, scale)
    off = np.max(np.abs(gram - np.eye(len(gram))))
    if off > 1.0e-13:
        lower = np.linalg.cholesky(gram)
        vectors = np.linalg.solve(lower, vectors.T).T
    return np.asarray(vectors, dtype=np.float64)


def _dense(k: sp.spmatrix, m: sp.spmatrix, lam_max: float) -> tuple[Array, Array, int]:
    lam, vec = sla.eigh(k.toarray(), m.toarray())
    keep = lam <= lam_max
    return lam[keep], vec[:, keep], 1


def _windowed(
    k: sp.csc_matrix, m: sp.csc_matrix, lam_max: float, nev: int, shift0: float
) -> tuple[Array, Array, int]:
    n = k.shape[0]
    nev = min(nev, n - 2)
    lams: list[Array] = []
    vecs: list[Array] = []
    sigma = shift0
    covered = -np.inf  # all eigenvalues <= covered are already collected
    windows = 0
    while True:
        windows += 1
        vals, vv = spla.eigsh(k, k=nev, M=m, sigma=sigma, which="LM", tol=0.0)
        dist = np.abs(vals - sigma)
        r = float(dist.max())
        # An eigenvalue at the window edge may have a partner (degenerate pair) that was cut off:
        # shrink the window slightly so that edge values are re-found by the next window.
        r_eff = r * (1.0 - 1.0e-6)
        inside = (vals > covered) & (np.abs(vals - sigma) <= r_eff)
        lams.append(vals[inside])
        vecs.append(vv[:, inside])
        top = sigma + r_eff
        covered = top
        if top >= lam_max:
            break
        if r_eff <= 0.0 or not math.isfinite(r_eff):
            raise RuntimeError("eigen-solve stalled: window of zero width")
        sigma = top
    lam = np.concatenate(lams)
    vec = np.concatenate(vecs, axis=1)
    order = np.argsort(lam, kind="stable")
    lam, vec = lam[order], vec[:, order]
    keep = lam <= lam_max
    return lam[keep], vec[:, keep], windows


def solve_modes(
    stiffness: sp.spmatrix,
    mass: sp.spmatrix,
    f_max: float,
    nev: int = 40,
    dense_threshold: int = 400,
) -> Modes:
    """All modes with Omega/2pi <= f_max, mass-normalised, rigid-body modes dropped and counted.

    # SPEC-BODY, B2-R5: Omega_k^2 are the eigenvalues of K phi = Omega^2 M phi.
    Modes with Omega/2pi < RIGID_HZ (or Omega < RIGID_REL * largest kept Omega) are dropped.
    Raises ``ValueError`` if the system has a significantly negative eigenvalue (compressive
    prestress beyond buckling) and ``RuntimeError`` if no mode lies in the band.
    """
    lam_max = (2.0 * math.pi * f_max) ** 2
    k = sp.csc_matrix(stiffness)
    m = sp.csc_matrix(mass)
    n = k.shape[0]
    if n <= dense_threshold:
        lam, vec, windows = _dense(k, m, lam_max)
    else:
        shift0 = -((2.0 * math.pi * RIGID_HZ) ** 2)
        lam, vec, windows = _windowed(k, m, lam_max, nev, shift0)
    if lam.size == 0:
        raise RuntimeError(f"no mode below f_max = {f_max} Hz")
    if np.any(lam < -1.0e-6 * lam_max):
        raise ValueError("the stiffness matrix has a negative eigenvalue (buckled prestress?)")
    omega_all = np.sqrt(np.maximum(lam, 0.0))
    rigid = (omega_all < 2.0 * math.pi * RIGID_HZ) | (omega_all < RIGID_REL * omega_all.max())
    if np.all(rigid):
        raise RuntimeError(f"no flexible mode between {RIGID_HZ} Hz and f_max = {f_max} Hz")
    omega = omega_all[~rigid]
    vectors = mass_normalise(vec[:, ~rigid], m)
    return Modes(omega=omega, vectors=vectors, n_rigid=int(rigid.sum()), windows=windows)
