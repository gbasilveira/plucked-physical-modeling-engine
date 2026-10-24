# SPDX-License-Identifier: MIT
"""B2-R5: the sign of every computed mode is deterministic."""

from __future__ import annotations

import numpy as np
import scipy.sparse as sp

from pme_compiler.body import eigen


def _problem(n: int) -> tuple[sp.csc_matrix, sp.csc_matrix]:
    # 1-D chain: K = tridiag(-1, 2, -1), M = diag(1..2)
    k = sp.diags([-np.ones(n - 1), 2.0 * np.ones(n), -np.ones(n - 1)], [-1, 0, 1], format="csc")
    m = sp.diags(1.0 + np.arange(n) / n, format="csc")
    return k, m


def test_b2_r5_mode_sign_is_deterministic() -> None:
    """B2-R5: flipping the signs of input vectors gives identical mass-normalised output."""
    n = 30
    k, m = _problem(n)
    _, vec = np.linalg.eigh(np.linalg.solve(m.toarray(), k.toarray()))
    vec = vec[:, :8]
    ref = eigen.mass_normalise(vec, m)
    rng = np.random.default_rng(7)
    for _ in range(5):
        flips = rng.choice([-1.0, 1.0], size=vec.shape[1])
        assert np.array_equal(eigen.mass_normalise(vec * flips, m), ref)
    # the rule: the first largest-magnitude component of each mode is positive
    peak = np.argmax(np.abs(ref) >= np.abs(ref).max(axis=0) * (1 - eigen.SIGN_TIE_REL), axis=0)
    assert np.all(ref[peak, np.arange(ref.shape[1])] > 0.0)


def test_b2_r5_mode_sign_tie_uses_lowest_index() -> None:
    """B2-R5: equal-magnitude components are tied and the lowest index decides."""
    v = np.array([[-1.0, 1.0], [1.0, -1.0], [0.5, 0.5]])
    out = eigen.fix_signs(v)
    assert out[:, 0].tolist() == [1.0, -1.0, -0.5]
    assert out[:, 1].tolist() == [1.0, -1.0, 0.5]


def test_b2_r5_solve_modes_signs_match_on_both_paths() -> None:
    """B2-R5: dense and windowed solves both return sign-fixed modes."""
    k, m = _problem(60)
    k = k * 1.0e6  # first modes at tens of Hz, above the rigid-body cut-off
    f_max = 300.0
    for threshold in (400, 10):  # dense, windowed
        modes = eigen.solve_modes(k, m, f_max=f_max, nev=12, dense_threshold=threshold)
        mag = np.abs(modes.vectors)
        first = np.argmax(mag >= mag.max(axis=0) * (1 - eigen.SIGN_TIE_REL), axis=0)
        assert np.all(modes.vectors[first, np.arange(mag.shape[1])] > 0.0)
