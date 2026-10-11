# SPDX-License-Identifier: MIT
"""B2-R4 static prestress as geometric stiffness (0.5 % on the first 10 modes)."""

from __future__ import annotations

import math

import numpy as np
import pytest
from test_b2_common import isotropic_plate, point, ss_orthotropic_frequencies

from pme_compiler.body import compile as plate_compile
from pme_compiler.body.fem import Prestress


def _compile(n: Prestress | None, plate_kw: dict[str, float] | None = None) -> np.ndarray:
    plate = isotropic_plate(lx=0.6, ly=0.4, f_max=800.0, **(plate_kw or {}))
    c = plate_compile.compile_plate(
        plate,
        [point(0.3, 0.2)],
        boundary="simply_supported",
        prestress=n,
        mesh_size=0.06,
        grid_order=0,
    )
    return np.asarray(c.table.omega / (2.0 * math.pi))


@pytest.mark.parametrize("n_tension", [2000.0, 8000.0])
def test_b2_r4_uniform_tension_matches_the_analytic_frequencies(n_tension: float) -> None:
    # omega_mn^2 = [D (k_m^2 + k_n^2)^2 + N (k_m^2 + k_n^2)] / (rho h)
    plate = isotropic_plate(lx=0.6, ly=0.4, f_max=800.0)
    ref = ss_orthotropic_frequencies(plate, 10, prestress=n_tension)
    f = _compile(Prestress(nx=n_tension, ny=n_tension))
    rel = np.abs(f[:10] / ref - 1.0)
    print(f"B2-R4 N = {n_tension}: worst error {rel.max():.2e}")
    assert rel.max() < 0.005
    # tension raises every frequency
    f0 = _compile(None)
    assert np.all(f[:10] > f0[:10])


def test_b2_r4_compression_lowers_the_frequencies() -> None:
    f0 = _compile(None)
    f = _compile(Prestress(nx=-1500.0, ny=-1500.0))
    assert np.all(f[:6] < f0[:6])


def test_b2_r4_unequal_resultants_weight_the_two_directions() -> None:
    # omega^2 = [D k^4 + N_x k_m^2 + N_y k_n^2] / (rho h) for a simply supported rectangle
    plate = isotropic_plate(lx=0.6, ly=0.4, f_max=800.0)
    nx, ny = 6000.0, 500.0
    d = 70.0e9 * 0.002**3 / (12.0 * (1.0 - 0.09))
    rho_h = 2700.0 * 0.002
    ref = sorted(
        math.sqrt(
            (
                d * ((m * math.pi / 0.6) ** 2 + (n * math.pi / 0.4) ** 2) ** 2
                + nx * (m * math.pi / 0.6) ** 2
                + ny * (n * math.pi / 0.4) ** 2
            )
            / rho_h
        )
        / (2.0 * math.pi)
        for m in range(1, 9)
        for n in range(1, 9)
    )[:8]
    f = _compile(Prestress(nx=nx, ny=ny))
    assert np.max(np.abs(f[:8] / np.array(ref) - 1.0)) < 0.005
    del plate


def test_b2_r4_buckled_compression_is_rejected() -> None:
    with pytest.raises(ValueError, match="negative eigenvalue"):
        _compile(Prestress(nx=-1.0e6, ny=-1.0e6))
