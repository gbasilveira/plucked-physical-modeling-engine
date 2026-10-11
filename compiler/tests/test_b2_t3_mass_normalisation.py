# SPDX-License-Identifier: MIT
"""B2-T3: mass normalisation (Phi^T M Phi = I to 1e-6) and bridge-point interpolation."""

from __future__ import annotations

import numpy as np
from test_b2_common import p05_plate, point

from pme_compiler.body import compile as plate_compile
from pme_compiler.schema import models


def _ribbed_plate_with_thickness_grid() -> models.PlateParams:
    # 3 x 3 thickness grid (row-major j * Nx + i): thicker towards the +x side.
    grid = [0.0025, 0.0030, 0.0035] * 3
    rib = models.RibDesc(
        start=point(0.1, 0.1),
        end=point(0.5, 0.3),
        width=0.01,
        height=0.012,
        youngs=10.0e9,
        density=450.0,
    )
    bridge = models.BridgeBeamDesc(
        path=[point(0.15, 0.2), point(0.3, 0.25), point(0.45, 0.2)],
        width=0.012,
        height=0.012,
        youngs=9.0e9,
        density=700.0,
    )
    return p05_plate(
        fMax=300.0,
        thicknessNx=3,
        thicknessNy=3,
        thicknessGrid=grid,
        ribs=[rib],
        bridges=[bridge],
        rimStiffness=5.0e3,
        rimRotationalStiffness=20.0,
    )


def _check(c: plate_compile.Compiled) -> None:
    phi = c.modes.vectors
    gram = phi.T @ (c.system.mass @ phi)
    err = float(np.max(np.abs(gram - np.eye(gram.shape[0]))))
    print(f"B2-T3: {phi.shape[1]} modes, max |Phi^T M Phi - I| = {err:.2e}")
    assert err < 1.0e-6


def test_b2_t3_mass_normalisation_of_the_uniform_plate() -> None:
    c = plate_compile.compile_plate(
        p05_plate(fMax=420.0),
        [point(0.3, 0.2)],
        boundary="simply_supported",
        mesh_size=0.05,
        grid_order=0,
    )
    assert c.info.n_modes >= 20
    _check(c)


def test_b2_t3_mass_normalisation_of_a_ribbed_plate_with_a_thickness_grid() -> None:
    c = plate_compile.compile_plate(
        _ribbed_plate_with_thickness_grid(), [point(0.3, 0.2)], mesh_size=0.04, grid_order=0
    )
    assert c.info.n_modes >= 8
    _check(c)


def test_b2_t3_bridge_point_values_equal_the_assembled_field() -> None:
    plate = _ribbed_plate_with_thickness_grid()
    pts = [point(0.3, 0.2), point(0.21, 0.13), point(0.45, 0.33), point(0.05, 0.05)]
    c = plate_compile.compile_plate(plate, pts, mesh_size=0.04, grid_order=0)
    basis, mesh = c.system.basis, c.system.mesh
    vec = c.modes.vectors
    # (1) at mesh nodes the interpolated value is the nodal degree of freedom of the field
    nodes = np.arange(0, mesh.p.shape[1], max(1, mesh.p.shape[1] // 40))
    xy = mesh.p[:, nodes].T
    sampled = plate_compile.sample_matrix(basis, xy) @ vec
    nodal = vec[basis.nodal_dofs[0, nodes], :]
    assert float(np.max(np.abs(sampled - nodal))) < 1.0e-6 * float(np.max(np.abs(nodal)))
    # (2) the table's phi equals the interpolation of the same field at the bridge points
    xy_b = np.array([[p.x, p.y] for p in pts])
    direct = (plate_compile.sample_matrix(basis, xy_b) @ vec).T
    assert c.table.phi.shape == (c.info.n_modes, len(pts))
    assert np.array_equal(c.table.phi, direct)
    # (3) the interpolant of a mode is continuous across an element edge: sample just either side
    # of the first bridge point and compare with the value at the point
    eps = 1.0e-7
    near = np.array([[0.3 + eps, 0.2], [0.3 - eps, 0.2], [0.3, 0.2 + eps]])
    vals = (plate_compile.sample_matrix(basis, near) @ vec).T
    scale = float(np.max(np.abs(c.table.phi[:, 0])))
    assert float(np.max(np.abs(vals - c.table.phi[:, [0]]))) < 1.0e-4 * scale
