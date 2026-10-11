# SPDX-License-Identifier: MIT
"""B4-T1 and B4-R3: vector fitting recovers synthetic modal parameters."""

from __future__ import annotations

import math

import numpy as np
import pytest
from numpy.typing import NDArray

from pme_compiler.importers._common import ImportDataError
from pme_compiler.importers.vector_fit import (
    BodyModalTable,
    fit_to_table,
    modal_rms_error,
    transfer_mobility,
    vector_fit,
)

F64 = NDArray[np.float64]

# 12 modes, 20 Hz to 2 kHz; two pairs closer than 3 % (120/123 Hz = 2.5 %, 310/318.7 Hz = 2.8 %).
# sigma rises with frequency (1 to 40 1/s) so that every resonance is resolved by the 4000
# log-spaced samples (the half-power width 2 sigma / 2 pi must exceed the grid pitch).
FREQ = np.array([20, 41, 83, 120, 123, 210, 310, 318.68, 560, 900, 1400, 2000.0])
SIGMA = np.array([1, 1.5, 2, 3, 4, 6, 8, 10, 15, 22, 30, 40.0])
N_PAIRS = 12  # model order: 12 conjugate pole pairs (24 poles), equal to the true mode count


def _truth() -> BodyModalTable:
    rng = np.random.default_rng(20261011)
    phi = rng.uniform(0.2, 0.6, (12, 3)) * rng.choice([-1.0, 1.0], (12, 3))
    phi[:, 0] = np.abs(phi[:, 0])
    return BodyModalTable(
        omega=2 * np.pi * FREQ, sigma=SIGMA, phi=phi, points=np.zeros((3, 3))
    )


def _data(noise: float) -> tuple[F64, dict[tuple[int, int], NDArray[np.complex128]]]:
    truth = _truth()
    f = np.geomspace(10.0, 2500.0, 4000)
    rng = np.random.default_rng(7)
    resp = {}
    for key in ((0, 0), (0, 1), (0, 2)):
        h = transfer_mobility(truth, key[0], key[1], 2 * np.pi * f)
        # Multiplicative complex Gaussian noise: relative standard deviation `noise` per sample.
        n = (rng.normal(size=h.size) + 1j * rng.normal(size=h.size)) / math.sqrt(2.0)
        resp[key] = h * (1.0 + noise * n)
    return f, resp


@pytest.mark.parametrize("noise", [0.0, 0.01])
def test_b4_t1_recovers_modal_parameters(noise: float) -> None:
    """B4-T1: 0.5 % in frequency, 5 % in damping and Phi products, with and without noise."""
    truth = _truth()
    f, resp = _data(noise)
    res = vector_fit(f, resp, N_PAIRS, f_min=15.0, f_max=2300.0)
    table = fit_to_table(res, truth.points)
    assert table.omega.size == N_PAIRS
    d_omega = np.abs(table.omega / truth.omega - 1.0)
    d_sigma = np.abs(table.sigma / truth.sigma - 1.0)
    assert d_omega.max() <= 0.005
    assert d_sigma.max() <= 0.05
    for b in (0, 1, 2):  # Phi_k(0) Phi_k(b), the products of the mobility formula
        want = truth.phi[:, 0] * truth.phi[:, b]
        got = table.phi[:, 0] * table.phi[:, b]
        assert np.max(np.abs(got / want - 1.0)) <= 0.05
    assert res.rms_relative_error < (1e-6 if noise == 0.0 else 0.02)
    assert modal_rms_error(table, f, resp, 15.0, 2300.0) < (1e-6 if noise == 0.0 else 0.02)


def test_b4_r3_requires_driving_point_and_transfer() -> None:
    """B4-R3: a point without a transfer response, or a missing driving point, is rejected."""
    f, resp = _data(0.0)
    res = vector_fit(f, {k: v for k, v in resp.items() if k != (0, 2)}, N_PAIRS, 15.0, 2300.0)
    with pytest.raises(ImportDataError, match="point 2"):
        fit_to_table(res, np.zeros((3, 3)))
    res2 = vector_fit(f, {(0, 1): resp[(0, 1)]}, N_PAIRS, 15.0, 2300.0)
    with pytest.raises(ImportDataError, match="driving-point"):
        fit_to_table(res2, np.zeros((2, 3)))


def test_b4_r3_rejects_bad_input() -> None:
    """B4-R3: non-increasing frequencies and too few samples are rejected."""
    f, resp = _data(0.0)
    with pytest.raises(ImportDataError):
        vector_fit(f[::-1], resp, N_PAIRS)
    with pytest.raises(ImportDataError):
        vector_fit(f[:20], {k: v[:20] for k, v in resp.items()}, N_PAIRS)
