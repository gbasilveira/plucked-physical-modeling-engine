# SPDX-License-Identifier: MIT
"""B4-R4: Helmholtz air-cavity mode."""

from __future__ import annotations

import math

import numpy as np
import pytest

from pme_compiler.body import modal_blob
from pme_compiler.importers._common import BodyModalTable, ImportDataError
from pme_compiler.importers.helmholtz import (
    effective_neck_length,
    helmholtz_frequency,
    merge_mode,
)


def test_b4_r4_helmholtz_frequency_matches_a_hand_computation() -> None:
    """B4-R4: V = 0.012 m^3, hole radius 0.04 m, t = 0.003 m, c0 = 343 m/s."""
    r, t, v = 0.04, 0.003, 0.012
    s = math.pi * r * r  # 5.0265482e-3 m^2
    l_eff = t + 1.7 * r  # 0.071 m
    assert effective_neck_length(s, t) == pytest.approx(0.071, rel=1e-12)
    # hand: S / (V L) = 5.0265482e-3 / (0.012 * 0.071) = 5.89961...; sqrt = 2.42892...
    # f = 343 / (2 pi) * 2.42892 = 132.60 Hz
    hand = 343.0 / (2 * math.pi) * math.sqrt(5.0265482e-3 / (0.012 * 0.071))
    assert helmholtz_frequency(v, s, t) == pytest.approx(hand, rel=1e-7)
    assert helmholtz_frequency(v, s, t) == pytest.approx(132.6, abs=0.05)
    assert helmholtz_frequency(v, s, neck_length=l_eff) == pytest.approx(
        helmholtz_frequency(v, s, t), rel=1e-15
    )
    with pytest.raises(ImportDataError):
        helmholtz_frequency(-1.0, s)


def _table() -> BodyModalTable:
    return BodyModalTable(
        omega=2 * np.pi * np.array([90.0, 200.0, 400.0]),
        sigma=np.array([3.0, 5.0, 9.0]),
        phi=np.array([[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]]),
        points=np.zeros((2, 3)),
    )


def test_b4_r4_merge_keeps_the_table_sorted() -> None:
    """B4-R4: the merged mode lands at its frequency; sigma = Omega / (2 Q); rows move together."""
    t = _table()
    for f_h, pos in ((50.0, 0), (130.0, 1), (300.0, 2), (500.0, 3)):
        m = merge_mode(t, f_h, 40.0, np.array([0.7, -0.8]))
        assert np.all(np.diff(m.omega) > 0) and m.omega.size == 4
        assert m.omega[pos] == pytest.approx(2 * math.pi * f_h)
        assert m.sigma[pos] == pytest.approx(2 * math.pi * f_h / 80.0)
        assert m.phi[pos].tolist() == [0.7, -0.8]
        rest = np.delete(np.arange(4), pos)
        assert np.array_equal(m.phi[rest], t.phi)
        assert modal_blob.validate(m) == []


def test_b4_r4_merge_rejections_and_alpha_beta() -> None:
    """B4-R4: wrong phi length, grids and missing alpha/beta are rejected; alpha/beta are merged."""
    t = _table()
    with pytest.raises(ImportDataError, match="phi must be 2"):
        merge_mode(t, 100.0, 10.0, np.array([1.0]))
    with pytest.raises(ImportDataError):
        merge_mode(t, -1.0, 10.0, np.array([1.0, 1.0]))
    ab = BodyModalTable(
        **{
            **t.__dict__,
            "alpha": np.ones((1, 3)),
            "beta": np.zeros((1, 3)),
            "channel_delay": np.zeros(1),
        }
    )
    with pytest.raises(ImportDataError, match="alpha and beta"):
        merge_mode(ab, 100.0, 10.0, np.array([1.0, 1.0]))
    m = merge_mode(ab, 100.0, 10.0, np.array([1.0, 1.0]), np.array([7.0]), np.array([8.0]))
    assert m.alpha is not None and m.beta is not None
    assert m.alpha[0].tolist() == [1.0, 7.0, 1.0, 1.0] and m.beta[0].tolist() == [
        0.0,
        8.0,
        0.0,
        0.0,
    ]
    grid = BodyModalTable(**{**t.__dict__, "grid_phi": np.ones((3, 4))})
    with pytest.raises(ImportDataError, match="radiation grid"):
        merge_mode(grid, 100.0, 10.0, np.array([1.0, 1.0]))
