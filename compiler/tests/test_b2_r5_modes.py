# SPDX-License-Identifier: MIT
"""B2-R5 eigen-solve (windows, rigid-body modes), B2-R6 decay, radiation grid."""

from __future__ import annotations

import math

import numpy as np
import pytest
import scipy.sparse as sp
from test_b2_common import isotropic_plate, p05_plate, point

from pme_compiler.body import compile as plate_compile
from pme_compiler.body import eigen


def test_b2_r5_free_plate_drops_exactly_three_rigid_body_modes() -> None:
    # rimStiffness = 0 and rimRotationalStiffness = 0: a free plate (heave and two tilts)
    c = plate_compile.compile_plate(
        p05_plate(fMax=300.0), [point(0.3, 0.2)], mesh_size=0.05, grid_order=0
    )
    assert c.info.n_rigid_dropped == 3
    assert c.table.omega[0] / (2.0 * math.pi) > 1.0
    gram = c.modes.vectors.T @ (c.system.mass @ c.modes.vectors)
    assert np.max(np.abs(gram - np.eye(gram.shape[0]))) < 1e-9


def test_b2_r5_supported_plate_has_no_rigid_body_modes() -> None:
    c = plate_compile.compile_plate(
        p05_plate(fMax=300.0),
        [point(0.3, 0.2)],
        boundary="simply_supported",
        mesh_size=0.05,
        grid_order=0,
    )
    assert c.info.n_rigid_dropped == 0


@pytest.mark.parametrize("nev", [6, 12, 40])
def test_b2_r5_windowed_solve_equals_the_dense_solve_including_repeated_modes(nev: int) -> None:
    # a square isotropic plate has repeated eigenvalues (m, n)/(n, m); small windows cut them
    plate = isotropic_plate(lx=0.5, ly=0.5, f_max=900.0)
    common = dict(boundary="simply_supported", mesh_size=0.06, grid_order=0)
    dense = plate_compile.compile_plate(
        plate,
        [point(0.2, 0.3)],
        dense_threshold=10**6,
        **common,  # type: ignore[arg-type]
    )
    windowed = plate_compile.compile_plate(
        plate,
        [point(0.2, 0.3)],
        dense_threshold=0,
        nev=nev,
        **common,  # type: ignore[arg-type]
    )
    assert windowed.info.windows >= 1
    if nev < 40:
        assert windowed.info.windows > 1
    assert windowed.info.n_modes == dense.info.n_modes
    assert np.max(np.abs(windowed.table.omega / dense.table.omega - 1.0)) < 1e-6
    gram = windowed.modes.vectors.T @ (windowed.system.mass @ windowed.modes.vectors)
    assert np.max(np.abs(gram - np.eye(gram.shape[0]))) < 1e-9


def test_b2_r5_solve_is_deterministic() -> None:
    kw = dict(mesh_size=0.05, grid_order=2, dense_threshold=0)
    a = plate_compile.compile_plate(p05_plate(fMax=300.0), [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    b = plate_compile.compile_plate(p05_plate(fMax=300.0), [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    assert np.array_equal(a.table.omega, b.table.omega)
    assert np.array_equal(a.table.phi, b.table.phi)
    assert a.table.grid_phi is not None and b.table.grid_phi is not None
    assert np.array_equal(a.table.grid_phi, b.table.grid_phi)


def test_b2_r5_mass_normalise_repairs_a_non_orthogonal_pair() -> None:
    m = sp.identity(3, format="csc") * 2.0
    v = np.array([[1.0, 1.0], [0.0, 0.1], [0.0, 0.0]])
    out = eigen.mass_normalise(v, m)
    assert np.max(np.abs(out.T @ (m @ out) - np.eye(2))) < 1e-12


def test_b2_r5_radiation_grid_integrates_the_modes_and_the_area() -> None:
    # grid_order = 10 integrates the product of two quintic mode shapes exactly on each triangle
    plate = p05_plate(fMax=250.0)
    c = plate_compile.compile_plate(
        plate,
        [point(0.3, 0.2)],
        boundary="simply_supported",
        mesh_size=0.06,
        grid_order=10,
    )
    t = c.table
    assert t.grid_points is not None and t.grid_weights is not None and t.grid_phi is not None
    assert t.grid_phi.shape == (c.info.n_modes, t.grid_points.shape[0])
    assert float(t.grid_weights.sum()) == pytest.approx(0.6 * 0.4, rel=1e-12)
    assert np.all(t.grid_points[:, 2] == 0.0)
    assert np.all(t.grid_weights > 0.0)
    rho_h = 400.0 * 0.003
    gram = rho_h * (t.grid_phi * t.grid_weights) @ t.grid_phi.T
    assert np.max(np.abs(gram - np.eye(gram.shape[0]))) < 1e-6


def test_b2_r5_default_radiation_grid_covers_the_plate_area() -> None:
    c = plate_compile.compile_plate(p05_plate(fMax=250.0), [point(0.3, 0.2)], mesh_size=0.06)
    t = c.table
    assert t.grid_weights is not None and t.grid_points is not None
    assert t.grid_points.shape[0] == 3 * c.info.n_elements
    assert float(t.grid_weights.sum()) == pytest.approx(0.24, rel=1e-12)
    none = plate_compile.compile_plate(
        p05_plate(fMax=250.0), [point(0.3, 0.2)], mesh_size=0.06, grid_order=0
    )
    assert none.table.grid_points is None and none.table.grid_phi is None


def test_b2_r6_decay_rate_is_half_eta_omega() -> None:
    c = plate_compile.compile_plate(
        p05_plate(fMax=250.0, lossFactor=0.02),
        [point(0.3, 0.2)],
        boundary="simply_supported",
        mesh_size=0.06,
        grid_order=0,
    )
    assert np.array_equal(c.table.sigma, 0.5 * 0.02 * c.table.omega)
    assert c.table.sigma_rad is None  # radiation damping is B3's


def test_b2_r6_loss_factor_function_of_frequency_is_used() -> None:
    def eta(f_hz: float) -> float:
        return 0.01 + 1e-5 * f_hz

    c = plate_compile.compile_plate(
        p05_plate(fMax=250.0),
        [point(0.3, 0.2)],
        boundary="simply_supported",
        mesh_size=0.06,
        grid_order=0,
        loss_factor=eta,
    )
    f = c.table.omega / (2.0 * math.pi)
    assert np.allclose(c.table.sigma, 0.5 * (0.01 + 1e-5 * f) * c.table.omega, rtol=1e-14)
    with pytest.raises(ValueError, match="loss_factor"):
        plate_compile.compile_plate(
            p05_plate(fMax=250.0),
            [point(0.3, 0.2)],
            boundary="simply_supported",
            mesh_size=0.06,
            loss_factor=lambda f: -1.0,
        )
