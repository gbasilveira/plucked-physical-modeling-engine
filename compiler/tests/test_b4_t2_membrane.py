# SPDX-License-Identifier: MIT
"""B4-T2 and B4-R5: analytic membrane modes."""

from __future__ import annotations

import math

import numpy as np
import pytest
from scipy.integrate import dblquad
from scipy.optimize import brentq
from scipy.special import jn_zeros, jv

from pme_compiler.importers._common import ImportDataError
from pme_compiler.importers.membrane import (
    circular_modes,
    circular_shape,
    circular_table,
    rectangular_modes,
    rectangular_shape,
    rectangular_table,
    wave_speed,
)

A, T_M, RHO_S = 0.06, 1500.0, 0.25  # shamisen-like skin: radius 6 cm
C = math.sqrt(T_M / RHO_S)


def _all_modes() -> dict[tuple[int, int], float]:
    modes = circular_modes(A, T_M, RHO_S, f_max=6000.0, max_order=5)
    return {(m, n): f for m, n, f, v in modes if v == 0 and n <= 5}


def test_b4_t2_circular_frequencies_match_bessel_zeros() -> None:
    """B4-T2: f_mn = j_mn c / (2 pi a) to 1e-9, against jn_zeros and against brentq roots of J_m."""
    got = _all_modes()
    for m in range(6):
        for n in range(1, 6):
            f = got[(m, n)]
            f_zero = float(jn_zeros(m, n)[-1]) * C / (2 * math.pi * A)
            assert abs(f / f_zero - 1.0) <= 1e-9
            # Independent root finding: bracket the n-th root by scanning J_m for sign changes.
            xs = np.linspace(0.1, 40.0, 40000)
            vals = jv(m, xs)
            idx = np.where(np.sign(vals[:-1]) * np.sign(vals[1:]) < 0)[0]
            root = brentq(lambda x, mm=m: float(jv(mm, x)), xs[idx[n - 1]], xs[idx[n - 1] + 1], xtol=1e-14)
            assert abs(f / (root * C / (2 * math.pi * A)) - 1.0) <= 1e-9


def test_b4_t2_rectangular_frequencies_match_closed_form() -> None:
    """B4-T2: f_mn = (c/2) sqrt((m/a)^2 + (n/b)^2) to 1e-12."""
    a, b = 0.15, 0.1
    got = {(m, n): f for m, n, f in rectangular_modes(a, b, T_M, RHO_S, f_max=6000.0)}
    assert len(got) > 10
    for (m, n), f in got.items():
        want = 0.5 * C * math.sqrt((m / a) ** 2 + (n / b) ** 2)
        assert abs(f / want - 1.0) <= 1e-12
    assert (1, 1) in got


def _quad_circle(m1: int, n1: int, v1: int, m2: int, n2: int, v2: int) -> float:
    # integral rho_s Phi_1 Phi_2 dA in polar coordinates with Gauss-Legendre in r and a uniform
    # (spectrally accurate) rule in theta.
    xr, wr = np.polynomial.legendre.leggauss(80)
    r = 0.5 * A * (xr + 1.0)
    th = (np.arange(256) + 0.5) * 2 * math.pi / 256
    rr, tt = np.meshgrid(r, th, indexing="ij")
    xy = np.column_stack([(rr * np.cos(tt)).ravel(), (rr * np.sin(tt)).ravel()])
    p1 = circular_shape(A, RHO_S, m1, n1, v1, xy).reshape(rr.shape)
    p2 = circular_shape(A, RHO_S, m2, n2, v2, xy).reshape(rr.shape)
    w = (0.5 * A * wr)[:, None] * rr * (2 * math.pi / 256)
    return float(RHO_S * np.sum(p1 * p2 * w))


def test_b4_t2_circular_shapes_are_mass_normalised_and_orthogonal() -> None:
    """B4-T2: integral rho_s Phi^2 dA = 1 to 1e-8, cos/sin pair and neighbouring modes orthogonal."""
    for m in range(0, 4):
        for n in (1, 2, 3):
            for v in (0, 1) if m > 0 else (0,):
                assert abs(_quad_circle(m, n, v, m, n, v) - 1.0) <= 1e-8
            if m > 0:
                assert abs(_quad_circle(m, n, 0, m, n, 1)) <= 1e-8
            assert abs(_quad_circle(m, n, 0, m, n + 1, 0)) <= 1e-8
    assert abs(_quad_circle(0, 1, 0, 1, 1, 0)) <= 1e-8


def test_b4_t2_rectangular_shapes_are_mass_normalised() -> None:
    """B4-T2: integral rho_s Phi^2 dA = 1 to 1e-8 for the rectangle."""
    a, b = 0.15, 0.1
    for m, n in ((1, 1), (2, 3), (4, 1)):

        def integrand(y: float, x: float, mm: int = m, nn: int = n) -> float:
            xy = np.array([[x, y]])
            return float(RHO_S * rectangular_shape(a, b, RHO_S, mm, nn, xy)[0] ** 2)

        val, _ = dblquad(integrand, 0.0, a, 0.0, b, epsabs=1e-12, epsrel=1e-12)
        assert abs(val - 1.0) <= 1e-8


def test_b4_r5_tables_are_sorted_and_sampled() -> None:
    """B4-R5: tables are sorted, have both cos and sin modes for m > 0 and sample at the points."""
    pts = np.array([[0.02, 0.01, 0.0], [-0.03, 0.02, 0.0]])
    t = circular_table(A, T_M, RHO_S, pts, f_max=3000.0, q_factor=30.0)
    assert np.all(np.diff(t.omega) >= 0)
    assert t.phi.shape == (t.omega.size, 2)
    assert np.allclose(t.sigma, t.omega / 60.0)
    pair = np.isclose(t.omega[1:], t.omega[:-1])
    assert pair.any()  # degenerate cos/sin pairs of m > 0
    r = pts[0, :2]
    k0 = int(np.argmin(np.abs(t.omega - 2 * math.pi * float(jn_zeros(0, 1)[0]) * C / (2 * math.pi * A))))
    j = float(jn_zeros(0, 1)[0])
    want = jv(0, j * np.hypot(*r) / A) / math.sqrt(RHO_S * math.pi * A**2 * jv(1, j) ** 2)
    assert abs(t.phi[k0, 0] - want) < 1e-12
    rect = rectangular_table(0.15, 0.1, T_M, RHO_S, np.array([[0.05, 0.04, 0.0]]), 4000.0, loss_factor=0.02)
    assert np.all(np.diff(rect.omega) >= 0)
    assert np.allclose(rect.sigma, 0.01 * rect.omega)


def test_b4_r5_rejections() -> None:
    """B4-R5: outside points, missing damping and bad material data are rejected."""
    with pytest.raises(ImportDataError, match="outside"):
        circular_table(A, T_M, RHO_S, np.array([[0.1, 0.0, 0.0]]), 2000.0, q_factor=10.0)
    with pytest.raises(ImportDataError, match="exactly one"):
        circular_table(A, T_M, RHO_S, np.array([[0.0, 0.0, 0.0]]), 2000.0)
    with pytest.raises(ImportDataError):
        wave_speed(-1.0, 1.0)
    with pytest.raises(ImportDataError, match="outside"):
        rectangular_table(0.1, 0.1, T_M, RHO_S, np.array([[0.2, 0.0, 0.0]]), 2000.0, q_factor=5.0)
