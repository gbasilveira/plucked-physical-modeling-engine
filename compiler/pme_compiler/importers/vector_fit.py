# SPDX-License-Identifier: MIT
"""Rational (vector) fitting of measured mobilities into modal tables (B4-R3).

(SPEC-BODY, SPEC-CONV)

Algorithm: vector fitting with relaxed non-triviality constraint and complex-conjugate pole pairs,
after B. Gustavsen and A. Semlyen, "Rational approximation of frequency domain responses by vector
fitting", IEEE Trans. Power Delivery 14(3), 1999 (pole relocation via the zeros of the scaling
function sigma(s)); the relaxed constraint follows B. Gustavsen, "Improving the pole relocating
properties of vector fitting", IEEE Trans. Power Delivery 21(3), 2006. Own implementation.

Convention (e^(+i omega t), s = i omega, SI-0012): the mobility (velocity per force) between
points a and b of a mass-normalised modal table is

    Y_ab(s) = sum_k s Phi_k(a) Phi_k(b) / (s^2 + 2 sigma_k s + Omega_k^2)
            = sum_k [ R_k / (s - p_k) + conj(R_k) / (s - conj(p_k)) ],
    p_k = -sigma_k + i omega_d,k,  omega_d,k = sqrt(Omega_k^2 - sigma_k^2),
    R_k = Phi_k(a) Phi_k(b) p_k / (2 i omega_d,k).

All poles are fitted as pairs with a common pole set for all responses. Optionally a constant and a
proportional term (d + e s per response) absorb out-of-band modes; they are reported but not part of
the modal table.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from numpy.typing import NDArray

from pme_compiler.importers._common import BodyModalTable, ImportDataError

F64 = NDArray[np.float64]
C128 = NDArray[np.complex128]
Pair = tuple[int, int]


@dataclass(frozen=True)
class FitResult:
    """Outcome of a vector fit (frequencies in rad/s)."""

    poles: C128  # [N], Im > 0, Re < 0
    residues: dict[Pair, C128]  # per response, residue at each pole [N]
    constant: dict[Pair, float]  # d per response (0 without constant terms)
    proportional: dict[Pair, float]  # e per response
    iterations: int
    converged: bool
    rms_relative_error: float  # RMS of |H_fit - H| / |H| over band and responses (incl. d, e)
    pole_change: list[float] = field(default_factory=list)


def _basis(s: C128, a: C128) -> C128:
    """Real-coefficient basis of conjugate pole pairs: columns (c1, c2) per pair."""
    cols = np.empty((s.size, 2 * a.size), dtype=np.complex128)
    for k, ak in enumerate(a):
        u = 1.0 / (s - ak)
        v = 1.0 / (s - np.conj(ak))
        cols[:, 2 * k] = u + v
        cols[:, 2 * k + 1] = 1j * (u - v)
    return cols


def _stack(m: C128) -> F64:
    return np.vstack([m.real, m.imag])


def _initial_poles(n_pairs: int, w_lo: float, w_hi: float) -> C128:
    # Gustavsen-Semlyen: lightly damped, imag parts spread over the band (log spacing here, since
    # the data of an acoustic body are log-spaced), real part = -imag / 100.
    imag = np.geomspace(w_lo, w_hi, n_pairs)
    return np.asarray(-imag / 100.0 + 1j * imag, dtype=np.complex128)


def _relocate(
    s: C128,
    a: C128,
    resp: list[C128],
    weights: list[F64],
    const_terms: bool,
) -> C128:
    """One pole-relocation step: solve the relaxed sigma problem, return the zeros of sigma."""
    n = a.size
    phi = _basis(s, a)
    ns = s.size
    nr = len(resp)
    per = 2 * n + (2 if const_terms else 0)
    ncols = nr * per + 2 * n + 1
    rows = 2 * ns * nr + 1
    mat = np.zeros((rows, ncols))
    rhs = np.zeros(rows)
    norm_h = 0.0
    for j, (h, w) in enumerate(zip(resp, weights, strict=True)):
        own = [phi]
        if const_terms:
            own.append(np.ones((ns, 1), dtype=np.complex128))
            own.append(s[:, None])
        blk = np.hstack(own) * w[:, None]
        shared = -np.hstack([phi, np.ones((ns, 1), dtype=np.complex128)]) * (h * w)[:, None]
        r0 = 2 * ns * j
        mat[r0 : r0 + 2 * ns, j * per : (j + 1) * per] = _stack(blk)
        mat[r0 : r0 + 2 * ns, nr * per :] = _stack(shared)
        norm_h += float(np.sum(np.abs(h * w) ** 2))
    scale = math.sqrt(norm_h)
    # Relaxation: mean over samples of Re sigma(s) = 1 (Gustavsen 2006).
    mat[-1, nr * per : nr * per + 2 * n] = scale * np.sum(phi.real, axis=0) / ns
    mat[-1, -1] = scale
    rhs[-1] = scale
    colnorm = np.linalg.norm(mat, axis=0)
    colnorm[colnorm == 0.0] = 1.0
    sol = np.linalg.lstsq(mat / colnorm, rhs, rcond=None)[0] / colnorm
    c_t = sol[nr * per : nr * per + 2 * n]
    d_t = float(sol[-1])
    if abs(d_t) < 1e-10:
        d_t = 1e-10 if d_t >= 0 else -1e-10

    # Zeros of sigma: eigenvalues of A - b c^T / d (A real block diagonal, b = [2, 0] per pair).
    amat = np.zeros((2 * n, 2 * n))
    bvec = np.zeros(2 * n)
    for k, ak in enumerate(a):
        amat[2 * k : 2 * k + 2, 2 * k : 2 * k + 2] = [[ak.real, ak.imag], [-ak.imag, ak.real]]
        bvec[2 * k] = 2.0
    eig = np.linalg.eigvals(amat - np.outer(bvec, c_t) / d_t)
    return _pairs_from_eigs(np.asarray(eig, dtype=np.complex128), n)


def _pairs_from_eigs(eig: C128, n: int) -> C128:
    tol = 1e-12
    cplx = [e for e in eig if e.imag > tol * max(1.0, abs(e))]
    real = sorted(float(e.real) for e in eig if abs(e.imag) <= tol * max(1.0, abs(e)))
    poles = [complex(-abs(e.real), e.imag) for e in cplx]
    # Real eigenvalues (an overdamped pair) are paired up and replaced by a weakly oscillating pair.
    while len(real) >= 2 and len(poles) < n:
        a1, a2 = real.pop(), real.pop()
        mag = math.sqrt(abs(a1 * a2))
        poles.append(complex(-abs(0.5 * (a1 + a2)), 0.1 * mag))
    while len(poles) < n:  # lone real eigenvalue or deficiency: restart that pole mildly damped
        poles.append(complex(-0.01, 0.5))
    arr = np.array(sorted(poles[:n], key=lambda z: z.imag), dtype=np.complex128)
    return arr


def _residues(
    s: C128, a: C128, resp: list[C128], weights: list[F64], const_terms: bool
) -> list[tuple[C128, float, float]]:
    phi = _basis(s, a)
    n = a.size
    out = []
    for h, w in zip(resp, weights, strict=True):
        cols = [phi]
        if const_terms:
            cols.append(np.ones((s.size, 1), dtype=np.complex128))
            cols.append(s[:, None])
        mat = _stack(np.hstack(cols) * w[:, None])
        rhs = np.concatenate([(h * w).real, (h * w).imag])
        cn = np.linalg.norm(mat, axis=0)
        cn[cn == 0.0] = 1.0
        x = np.linalg.lstsq(mat / cn, rhs, rcond=None)[0] / cn
        res = x[0 : 2 * n : 2] + 1j * x[1 : 2 * n : 2]
        d = float(x[2 * n]) if const_terms else 0.0
        e = float(x[2 * n + 1]) if const_terms else 0.0
        out.append((np.asarray(res, dtype=np.complex128), d, e))
    return out


def evaluate(omega: F64, poles: C128, residue: C128, d: float = 0.0, e: float = 0.0) -> C128:
    """H(i omega) = sum R/(s-p) + conj(R)/(s-conj p) + d + e s (physical units)."""
    s = 1j * omega[:, None]
    h = np.sum(residue / (s - poles) + np.conj(residue) / (s - np.conj(poles)), axis=1)
    return np.asarray(h + d + e * s[:, 0], dtype=np.complex128)


def vector_fit(
    freq_hz: F64,
    responses: dict[Pair, C128],
    n_pairs: int,
    f_min: float | None = None,
    f_max: float | None = None,
    max_iter: int = 40,
    tol: float = 1e-9,
    constant_terms: bool = True,
    relative_weighting: bool = True,
) -> FitResult:
    """Fit ``responses`` ((a, b) -> complex mobility, s/kg, at ``freq_hz``) with ``n_pairs`` pairs.

    Stopping rule: the largest relative pole change below ``tol`` or ``max_iter`` relocations.
    ``relative_weighting`` weights each sample by 1/|H| (relative error), else unweighted.
    """
    f = np.asarray(freq_hz, dtype=np.float64)
    if f.ndim != 1 or np.any(f <= 0) or np.any(np.diff(f) <= 0):
        raise ImportDataError("frequency_hz must be positive and strictly increasing")
    if not responses:
        raise ImportDataError("no mobility responses given")
    lo = f[0] if f_min is None else f_min
    hi = f[-1] if f_max is None else f_max
    sel = (f >= lo) & (f <= hi)
    if int(sel.sum()) < 4 * n_pairs + 8:
        raise ImportDataError("too few samples in the fit band for the model order")
    if n_pairs < 1:
        raise ImportDataError("n_pairs must be >= 1")
    keys = list(responses)
    omega = 2.0 * math.pi * f[sel]
    ws = float(omega[-1])
    s: C128 = np.asarray(1j * omega / ws, dtype=np.complex128)
    resp: list[C128] = []
    for k in keys:
        h = np.asarray(responses[k], dtype=np.complex128)[sel]
        if h.shape != omega.shape or not np.all(np.isfinite(h)):
            raise ImportDataError(f"mobility {k}: wrong length or non-finite values")
        resp.append(h)
    weights = [
        1.0 / np.maximum(np.abs(h), 1e-300) if relative_weighting else np.ones(h.size) for h in resp
    ]
    if relative_weighting:
        weights = [w / float(np.mean(w)) for w in weights]  # keep the LS well scaled

    a: C128 = _initial_poles(n_pairs, float(omega[0]) / ws, 1.0)
    changes: list[float] = []
    converged = False
    it = 0
    for _ in range(max_iter):
        it += 1
        new = _relocate(s, a, resp, weights, constant_terms)
        change = float(np.max(np.abs(new - a) / np.abs(a)))
        a = np.asarray(new, dtype=np.complex128)
        changes.append(change)
        if change < tol:
            converged = True
            break
    fits = _residues(s, a, resp, weights, constant_terms)

    poles = a * ws
    residues: dict[Pair, C128] = {}
    cdict: dict[Pair, float] = {}
    edict: dict[Pair, float] = {}
    err2 = 0.0
    cnt = 0
    for key, (r, d, e), h in zip(keys, fits, resp, strict=True):
        residues[key] = r * ws
        cdict[key] = d
        edict[key] = e / ws
        hf = evaluate(omega, poles, residues[key], d, edict[key])
        err2 += float(np.sum(np.abs(hf - h) ** 2 / np.abs(h) ** 2))
        cnt += h.size
    return FitResult(
        poles=poles,
        residues=residues,
        constant=cdict,
        proportional=edict,
        iterations=it,
        converged=converged,
        rms_relative_error=math.sqrt(err2 / cnt),
        pole_change=changes,
    )


def modal_rms_error(
    table: BodyModalTable,
    freq_hz: F64,
    responses: dict[Pair, C128],
    f_min: float | None = None,
    f_max: float | None = None,
) -> float:
    """RMS relative error of the modal-table mobilities (no d, e terms) against the data."""
    lo = freq_hz[0] if f_min is None else f_min
    hi = freq_hz[-1] if f_max is None else f_max
    sel = (freq_hz >= lo) & (freq_hz <= hi)
    w = 2.0 * math.pi * freq_hz[sel]
    err2 = 0.0
    cnt = 0
    for (a, b), h in responses.items():
        hm = transfer_mobility(table, a, b, w)
        err2 += float(np.sum(np.abs(hm - h[sel]) ** 2 / np.abs(h[sel]) ** 2))
        cnt += w.size
    return math.sqrt(err2 / cnt)


def transfer_mobility(table: BodyModalTable, a: int, b: int, omega: F64) -> C128:
    """Y_ab(w) = sum_k i w Phi_k(a) Phi_k(b) / (Omega_k^2 - w^2 + 2 i sigma_k w) (SPEC-BODY)."""
    w = omega[:, None]
    num = 1j * w * (table.phi[:, a] * table.phi[:, b])
    den = table.omega**2 - w**2 + 2j * table.sigma * w
    return np.asarray(np.sum(num / den, axis=1), dtype=np.complex128)


def fit_to_table(
    result: FitResult,
    points: F64,
    driving: int = 0,
    max_residue_imag: float = 0.25,
) -> BodyModalTable:
    """Convert pole pairs and residues to a ``BodyModalTable`` (source ``MeasuredMobilityFit``).

    Omega_k = |p_k|, sigma_k = -Re p_k; the product g_k(a, b) = Phi_k(a) Phi_k(b) =
    R_k 2 i omega_d / p_k (real for a physical response; the relative imaginary part above
    ``max_residue_imag`` is rejected). The driving point ``a0 = driving`` needs its own response
    (a0, a0), giving Phi_k(a0)^2 = g_k, taken with positive sign; every other point b needs the
    transfer response (a0, b) or (b, a0), giving Phi_k(b) = g_k(a0, b) / Phi_k(a0).
    """
    p_count = points.shape[0]
    poles = result.poles
    omega = np.abs(poles)
    sigma = -poles.real
    wd = poles.imag
    fac = 2j * wd / poles

    def product(key: Pair) -> F64:
        g = result.residues[key] * fac
        scale = np.maximum(np.abs(g), 1e-300)
        if np.any(np.abs(g.imag) > max_residue_imag * scale + 1e-12 * np.max(np.abs(g))):
            raise ImportDataError(
                f"response {key}: residues are not consistent with real mode shapes "
                "(imaginary part of Phi_a Phi_b too large; check the e^(+i omega t) convention)"
            )
        return np.asarray(g.real, dtype=np.float64)

    if (driving, driving) not in result.residues:
        raise ImportDataError(f"driving-point response ({driving}, {driving}) is required")
    g0 = product((driving, driving))
    if np.any(g0 < -1e-9 * np.max(np.abs(g0))):
        raise ImportDataError("driving-point residues give negative Phi^2 (sign or convention)")
    phi_d = np.sqrt(np.maximum(g0, 0.0))
    phi = np.zeros((poles.size, p_count))
    phi[:, driving] = phi_d
    safe = np.where(phi_d > 0.0, phi_d, np.inf)
    for b in range(p_count):
        if b == driving:
            continue
        key = (driving, b) if (driving, b) in result.residues else (b, driving)
        if key not in result.residues:
            raise ImportDataError(f"point {b}: need a transfer response ({driving}, {b})")
        phi[:, b] = product(key) / safe
    order = np.argsort(omega, kind="stable")
    return BodyModalTable(
        omega=omega[order], sigma=sigma[order], phi=phi[order], points=points.copy()
    )
