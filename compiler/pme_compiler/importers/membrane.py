# SPDX-License-Identifier: MIT
"""Analytic membrane modes for shamisen-type bodies (B4-R5). (SPEC-BODY, SPEC-INST table)

Fixed-edge membrane with tension per unit length ``T_m`` (N/m) and areal density ``rho_s``
(kg/m^2); wave speed c = sqrt(T_m / rho_s). Shapes are mass-normalised,
integral(rho_s Phi_k Phi_l dA) = delta_kl (unit kg^(-1/2)). Air loading is out of scope.

Frames: the circular membrane has its centre at the origin (polar angle measured from +x); the
rectangular membrane a x b has its corner at the origin, x in [0, a], y in [0, b]. Bridge points are
sampled by their (x, y); z is ignored.
"""

from __future__ import annotations

import math

import numpy as np
from numpy.typing import NDArray
from scipy.special import jn_zeros, jv

from pme_compiler.importers._common import (
    BodyModalTable,
    ImportDataError,
    sigma_from_loss,
    sigma_from_q,
    sorted_table,
)

F64 = NDArray[np.float64]


def _sigma(omega: F64, loss_factor: float | None, q_factor: float | None) -> F64:
    if (loss_factor is None) == (q_factor is None):
        raise ImportDataError("give exactly one of loss_factor and q_factor")
    if loss_factor is not None:
        if loss_factor < 0:
            raise ImportDataError("loss_factor must be >= 0")
        return sigma_from_loss(omega, loss_factor)
    assert q_factor is not None
    if q_factor <= 0:
        raise ImportDataError("q_factor must be > 0")
    return sigma_from_q(omega, q_factor)


def wave_speed(tension: float, areal_density: float) -> float:
    """c = sqrt(T_m / rho_s) (membrane wave equation)."""
    if tension <= 0 or areal_density <= 0:
        raise ImportDataError("tension and areal density must be > 0")
    return math.sqrt(tension / areal_density)


def circular_modes(
    radius: float, tension: float, areal_density: float, f_max: float, max_order: int = 12
) -> list[tuple[int, int, float, int]]:
    """Modes (m, n, f_mn in Hz, variant) of a clamped circular membrane up to ``f_max`` Hz.

    f_mn = j_{m,n} c / (2 pi a) with j_{m,n} the n-th positive zero of J_m (Bessel; Fletcher &
    Rossing, The Physics of Musical Instruments, membrane chapter). ``variant`` is 0 for
    cos(m theta)
    and 1 for sin(m theta); m = 0 has only the cos (axisymmetric) variant. Sorted by frequency.
    """
    c = wave_speed(tension, areal_density)
    out: list[tuple[int, int, float, int]] = []
    for m in range(max_order + 1):
        n_max = 1
        while True:
            # Over-allocate zeros; jn_zeros is exact to double precision.
            zeros = jn_zeros(m, n_max)
            if zeros[-1] * c / (2.0 * math.pi * radius) > f_max:
                break
            n_max *= 2
            if n_max > 4096:
                raise ImportDataError("f_max too large for the mode enumeration")
        for n, j in enumerate(zeros, start=1):
            f = float(j) * c / (2.0 * math.pi * radius)
            if f <= f_max:
                out.append((m, n, f, 0))
                if m > 0:
                    out.append((m, n, f, 1))
    out.sort(key=lambda t: (t[2], t[0], t[3]))
    return out


def circular_norm(radius: float, areal_density: float, m: int, j: float) -> float:
    """sqrt(rho_s N_mn) with N_0n = pi a^2 J_1(j)^2, N_mn = (pi a^2 / 2) J_{m+1}(j)^2 (m > 0).

    Derived from integral_0^a J_m(j r / a)^2 r dr = (a^2 / 2) J_{m+1}(j)^2 and the angular integral
    2 pi (m = 0) or pi (cos^2 or sin^2, m > 0).
    """
    n_mn = (
        math.pi * radius**2 * jv(1, j) ** 2
        if m == 0
        else 0.5 * math.pi * radius**2 * jv(m + 1, j) ** 2
    )
    return math.sqrt(areal_density * n_mn)


def circular_shape(
    radius: float, areal_density: float, m: int, n: int, variant: int, xy: F64
) -> F64:
    """Phi_mn = J_m(j r / a) {cos, sin}(m theta) / sqrt(rho_s N_mn) at points ``xy`` [N, 2+]."""
    j = float(jn_zeros(m, n)[-1])
    r = np.hypot(xy[:, 0], xy[:, 1])
    theta = np.arctan2(xy[:, 1], xy[:, 0])
    ang = np.cos(m * theta) if variant == 0 else np.sin(m * theta)
    return np.asarray(
        jv(m, j * r / radius) * ang / circular_norm(radius, areal_density, m, j), dtype=np.float64
    )


def circular_table(
    radius: float,
    tension: float,
    areal_density: float,
    points: F64,
    f_max: float,
    loss_factor: float | None = None,
    q_factor: float | None = None,
) -> BodyModalTable:
    """Circular-membrane modal table sampled at ``points`` [P, 3] (membrane frame)."""
    if radius <= 0:
        raise ImportDataError("radius must be > 0")
    pts = np.asarray(points, dtype=np.float64)
    if np.any(np.hypot(pts[:, 0], pts[:, 1]) > radius):
        raise ImportDataError("a bridge point lies outside the membrane")
    modes = circular_modes(radius, tension, areal_density, f_max)
    if not modes:
        raise ImportDataError("no mode below f_max")
    omega = np.array([2.0 * math.pi * md[2] for md in modes])
    phi = np.stack([circular_shape(radius, areal_density, m, n, v, pts) for m, n, _, v in modes])
    table = BodyModalTable(
        omega=omega, sigma=_sigma(omega, loss_factor, q_factor), phi=phi, points=pts.copy()
    )
    return sorted_table(table)


def rectangular_modes(
    a: float, b: float, tension: float, areal_density: float, f_max: float
) -> list[tuple[int, int, float]]:
    """Modes (m, n, f_mn) of a clamped a x b membrane: f_mn = (c/2) sqrt((m/a)^2 + (n/b)^2)."""
    c = wave_speed(tension, areal_density)
    m_max = int(2.0 * a * f_max / c) + 1
    n_max = int(2.0 * b * f_max / c) + 1
    out = []
    for m in range(1, m_max + 1):
        for n in range(1, n_max + 1):
            f = 0.5 * c * math.sqrt((m / a) ** 2 + (n / b) ** 2)
            if f <= f_max:
                out.append((m, n, f))
    out.sort(key=lambda t: (t[2], t[0], t[1]))
    return out


def rectangular_shape(a: float, b: float, areal_density: float, m: int, n: int, xy: F64) -> F64:
    """Phi_mn = 2 / sqrt(rho_s a b) sin(m pi x / a) sin(n pi y / b)."""
    return np.asarray(
        2.0
        / math.sqrt(areal_density * a * b)
        * np.sin(m * math.pi * xy[:, 0] / a)
        * np.sin(n * math.pi * xy[:, 1] / b),
        dtype=np.float64,
    )


def rectangular_table(
    a: float,
    b: float,
    tension: float,
    areal_density: float,
    points: F64,
    f_max: float,
    loss_factor: float | None = None,
    q_factor: float | None = None,
) -> BodyModalTable:
    """Rectangular-membrane modal table sampled at ``points`` [P, 3] (corner at the origin)."""
    if a <= 0 or b <= 0:
        raise ImportDataError("side lengths must be > 0")
    pts = np.asarray(points, dtype=np.float64)
    if (
        np.any(pts[:, 0] < 0)
        or np.any(pts[:, 0] > a)
        or np.any(pts[:, 1] < 0)
        or np.any(pts[:, 1] > b)
    ):
        raise ImportDataError("a bridge point lies outside the membrane")
    modes = rectangular_modes(a, b, tension, areal_density, f_max)
    if not modes:
        raise ImportDataError("no mode below f_max")
    omega = np.array([2.0 * math.pi * md[2] for md in modes])
    phi = np.stack([rectangular_shape(a, b, areal_density, m, n, pts) for m, n, _ in modes])
    table = BodyModalTable(
        omega=omega, sigma=_sigma(omega, loss_factor, q_factor), phi=phi, points=pts.copy()
    )
    return sorted_table(table)
