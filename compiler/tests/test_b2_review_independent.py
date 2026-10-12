# SPDX-License-Identifier: MIT
"""B2 code-review additions: independent checks of the rib terms and of the grain rotation."""

from __future__ import annotations

import dataclasses
import math

import numpy as np
import scipy.linalg as sla
from test_b2_common import d_coefficients, p05_plate, point

from pme_compiler.body import compile as plate_compile
from pme_compiler.schema import models


def test_b2_r3_review_rib_along_x_matches_a_galerkin_sine_basis_solution() -> None:
    """B2-R3: a simply supported plate with a rib at y = Ly/2 along x, solved in a sine basis.

    The rib is parallel to x, so for each m the plate modes (m, n) are coupled only through the rib
    terms E I k_m^4 (Lx/2) s_n s_n' and rho A (Lx/2) s_n s_n' with s_n = sin(n pi / 2). This is an
    independent reduction (no FEM) that is exact up to truncation in n.
    """
    f_max = 330.0
    width, height, e_rib, rho_rib = 0.008, 0.012, 10.0e9, 450.0
    rib = models.RibDesc(
        start=point(0.0, 0.2),
        end=point(0.6, 0.2),
        width=width,
        height=height,
        youngs=e_rib,
        density=rho_rib,
    )
    plate = p05_plate(fMax=f_max, ribs=[rib])
    c = plate_compile.compile_plate(
        plate, [point(0.3, 0.1)], boundary="simply_supported", mesh_size=0.04, grid_order=0
    )
    d11, d22, d12, d66, rho_h = d_coefficients(plate)
    lx, ly = plate.lengthX, plate.lengthY
    ei = e_rib * width * height**3 / 12.0
    rho_a = rho_rib * width * height
    m0 = rho_h * lx * ly / 4.0
    ns = np.arange(1, 60)
    s = np.sin(ns * math.pi / 2.0)
    roots: list[float] = []
    for m in range(1, 12):
        km = m * math.pi / lx
        kn = ns * math.pi / ly
        w2 = (d11 * km**4 + 2 * (d12 + 2 * d66) * km**2 * kn**2 + d22 * kn**4) / rho_h
        k_mat = np.diag(w2 * m0) + ei * km**4 * (lx / 2.0) * np.outer(s, s)
        m_mat = m0 * np.eye(len(ns)) + rho_a * (lx / 2.0) * np.outer(s, s)
        lam = sla.eigh(k_mat, m_mat, eigvals_only=True)
        roots += [math.sqrt(x) / (2 * math.pi) for x in lam[:8]]
    ref = np.sort(np.array(roots))
    f = c.table.omega / (2 * math.pi)
    n = int(np.sum(ref <= f_max))
    assert f.size >= 12
    assert f.size == n
    assert np.max(np.abs(f / ref[:n] - 1.0)) < 5.0e-3


def test_b2_r1_review_rotated_plate_and_grain_give_the_unrotated_frequencies() -> None:
    """B2-R1: rotating outline and grain together by 30 degrees leaves the frequencies unchanged."""
    th = math.radians(30.0)
    cs, sn = math.cos(th), math.sin(th)
    corners = [(0.0, 0.0), (0.6, 0.0), (0.6, 0.4), (0.0, 0.4)]
    outline = [point(cs * x - sn * y, sn * x + cs * y) for x, y in corners]
    base = p05_plate(fMax=250.0)
    rot = dataclasses.replace(base, outline=outline, grainAngle=th)
    a = plate_compile.compile_plate(
        base, [point(0.3, 0.2)], boundary="simply_supported", mesh_size=0.05, grid_order=0
    )
    b = plate_compile.compile_plate(
        rot,
        [point(cs * 0.3 - sn * 0.2, sn * 0.3 + cs * 0.2)],
        boundary="simply_supported",
        mesh_size=0.05,
        grid_order=0,
    )
    assert a.table.omega.size == b.table.omega.size
    assert np.max(np.abs(a.table.omega / b.table.omega - 1.0)) < 2.0e-3
