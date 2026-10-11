# SPDX-License-Identifier: MIT
"""B2-R3 FEM: ribs and bridges add E I and rho A of their section; rim support options."""

from __future__ import annotations

import numpy as np
import pytest
from test_b2_common import d_coefficients, p05_plate, point

from pme_compiler.body import compile as plate_compile
from pme_compiler.body import fem, geometry, mesh
from pme_compiler.schema import models


def _rib(p0: tuple[float, float], p1: tuple[float, float], **over: float) -> models.RibDesc:
    kw = {"width": 0.01, "height": 0.015, "youngs": 10.0e9, "density": 450.0, **over}
    return models.RibDesc(
        start=point(*p0),
        end=point(*p1),
        width=kw["width"],
        height=kw["height"],
        youngs=kw["youngs"],
        density=kw["density"],
    )


def _field(x: np.ndarray, y: np.ndarray) -> np.ndarray:
    return np.asarray(np.sin(3.0 * x) * np.cos(2.0 * y) + 0.3 * x * y)


def _hessian(x: float, y: float) -> np.ndarray:
    sx, cx = np.sin(3.0 * x), np.cos(3.0 * x)
    sy, cy = np.sin(2.0 * y), np.cos(2.0 * y)
    wxx = -9.0 * sx * cy
    wyy = -4.0 * sx * cy
    wxy = -6.0 * cx * sy + 0.3
    return np.array([[wxx, wxy], [wxy, wyy]])


def test_b2_r3_a_rib_adds_its_bending_stiffness_and_mass() -> None:
    """B2-R3: a rib adds its bending stiffness and mass."""
    p0, p1 = (0.1, 0.05), (0.5, 0.35)
    rib = _rib(p0, p1)
    plate = p05_plate(ribs=[rib], rimStiffness=1.0e3)
    geom = geometry.from_plate(plate)
    pm = mesh.mesh_plate(geom, 0.04)
    system = fem.assemble(geom, pm)
    # a smooth test field in the Argyris space (L2 projection; error far below the 1e-3 checked)
    w = system.basis.project(lambda x: _field(x[0], x[1]))
    length = float(np.hypot(p1[0] - p0[0], p1[1] - p0[1]))
    t = np.array([p1[0] - p0[0], p1[1] - p0[1]]) / length
    gx, gw = np.polynomial.legendre.leggauss(40)
    s = 0.5 * length * (gx + 1.0)
    weights = 0.5 * length * gw
    px = p0[0] + t[0] * s
    py = p0[1] + t[1] * s
    ei = rib.youngs * rib.width * rib.height**3 / 12.0
    rho_a = rib.density * rib.width * rib.height
    w_ss = np.array([t @ _hessian(x, y) @ t for x, y in zip(px, py, strict=True)])
    energy_ref = ei * float(np.sum(weights * w_ss**2))
    mass_ref = rho_a * float(np.sum(weights * _field(px, py) ** 2))
    energy = float(w @ (system.beam_stiffness @ w))
    mass = float(w @ (system.beam_mass @ w))
    assert energy == pytest.approx(energy_ref, rel=2e-3)
    assert mass == pytest.approx(mass_ref, rel=2e-3)


def test_b2_r3_total_mass_is_plate_plus_beams() -> None:
    """B2-R3: total mass is plate plus beams."""
    rib = _rib((0.1, 0.1), (0.5, 0.1))
    bridge = models.BridgeBeamDesc(
        path=[point(0.05, 0.3), point(0.3, 0.3), point(0.55, 0.35)],
        width=0.012,
        height=0.02,
        youngs=9.0e9,
        density=700.0,
    )
    plate = p05_plate(ribs=[rib], bridges=[bridge])
    geom = geometry.from_plate(plate)
    system = fem.assemble(geom, mesh.mesh_plate(geom, 0.04))
    one = system.basis.project(lambda x: np.ones_like(x[0]))
    total = float(one @ (system.mass @ one))
    expected = 400.0 * 0.003 * 0.24
    expected += 450.0 * 0.01 * 0.015 * 0.4
    expected += 700.0 * 0.012 * 0.02 * (0.25 + float(np.hypot(0.25, 0.05)))
    assert total == pytest.approx(expected, rel=1e-9)


def _ss_mn(p: models.PlateParams, m: int, n: int) -> float:
    """Closed-form simply supported frequency (Hz) of mode (m, n), grain along x."""
    d11, d22, d12, d66, rho_h = d_coefficients(p)
    km, kn = m * np.pi / p.lengthX, n * np.pi / p.lengthY
    w2 = (d11 * km**4 + 2.0 * (d12 + 2.0 * d66) * km**2 * kn**2 + d22 * kn**4) / rho_h
    return float(np.sqrt(w2) / (2.0 * np.pi))


def test_b2_r3_rib_on_the_centre_line_leaves_the_antisymmetric_modes_unchanged() -> None:
    # A stiff, heavy rib along y = Ly / 2 does not move modes (m, 2n), whose shape is zero there,
    # but raises the symmetric modes such as (1, 1).
    """B2-R3: rib on the centre line leaves the antisymmetric modes unchanged."""
    rib = _rib((0.0, 0.2), (0.6, 0.2), width=0.02, height=0.03, youngs=20.0e9, density=900.0)
    bare = p05_plate(fMax=380.0)
    c = plate_compile.compile_plate(
        p05_plate(fMax=380.0, ribs=[rib]),
        [point(0.3, 0.1)],
        boundary="simply_supported",
        mesh_size=0.05,
        grid_order=0,
    )
    f = c.table.omega / (2.0 * np.pi)
    for m, n in ((1, 2), (2, 2), (3, 2)):
        ref = _ss_mn(bare, m, n)
        assert np.min(np.abs(f - ref)) / ref < 2.0e-3, (m, n)
    f11 = _ss_mn(bare, 1, 1)
    assert f[0] > 1.05 * f11  # the symmetric fundamental is shifted up by the rib


def test_b2_r3_refined_mesh_changes_a_ribbed_plate_by_less_than_one_percent() -> None:
    """B2-R3: refined mesh changes a ribbed plate by less than one percent."""
    rib = _rib((0.0, 0.2), (0.6, 0.2))
    plate = p05_plate(fMax=300.0, ribs=[rib])
    freqs = []
    for h in (0.05, 0.03):
        c = plate_compile.compile_plate(
            plate, [point(0.3, 0.1)], boundary="simply_supported", mesh_size=h, grid_order=0
        )
        freqs.append(c.table.omega[:6])
    assert np.max(np.abs(freqs[0] / freqs[1] - 1.0)) < 0.01


def test_b2_r3_elastic_rim_tends_to_simply_supported_and_clamped() -> None:
    """B2-R3: elastic rim tends to simply supported and clamped."""
    kw = dict(mesh_size=0.05, grid_order=0)
    base = p05_plate(fMax=250.0)
    ss = plate_compile.compile_plate(
        base,
        [point(0.3, 0.2)],
        boundary="simply_supported",
        **kw,  # type: ignore[arg-type]
    )
    cl = plate_compile.compile_plate(
        base,
        [point(0.3, 0.2)],
        boundary="clamped",
        **kw,  # type: ignore[arg-type]
    )
    stiff = p05_plate(fMax=250.0, rimStiffness=1.0e9)
    e1 = plate_compile.compile_plate(stiff, [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    n = 5
    assert np.max(np.abs(e1.table.omega[:n] / ss.table.omega[:n] - 1.0)) < 1e-3
    both = p05_plate(fMax=250.0, rimStiffness=1.0e9, rimRotationalStiffness=1.0e7)
    e2 = plate_compile.compile_plate(both, [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    assert np.max(np.abs(e2.table.omega[:n] / cl.table.omega[:n] - 1.0)) < 1e-3
    # a clamped plate is stiffer than a simply supported one, and a soft rim is softer than both
    assert np.all(cl.table.omega[:n] > ss.table.omega[:n])
    soft = p05_plate(fMax=250.0, rimStiffness=2.0e2)
    e0 = plate_compile.compile_plate(soft, [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    assert e0.table.omega[0] < ss.table.omega[0]
    assert e0.info.n_rigid_dropped in (0, 3)
