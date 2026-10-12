# SPDX-License-Identifier: MIT
"""Shared builders and analytic references for the B2 tests (contains no tests)."""

from __future__ import annotations

import dataclasses
import math

import numpy as np
from numpy.typing import NDArray

from pme_compiler.schema import models

Pt = models.Point


def point(x: float, y: float, z: float = 0.0) -> models.Point:
    return Pt(x=x, y=y, z=z)


def p05_plate(**over: object) -> models.PlateParams:
    """The P0.5 reference plate (docs/dev/reference-solver.md): spruce-like, 0.6 x 0.4 x 3 mm.

    E_L = 10 GPa, E_R = 0.5 GPa, G_LR = 0.6 GPa, nu_LR = 0.37, nu_RL = nu_LR E_R / E_L,
    rho = 400 kg/m^3, grain along x. Free rim (rimStiffness = 0); tests select a boundary option.
    """
    base = models.PlateParams(
        lengthX=0.6,
        lengthY=0.4,
        thickness=0.003,
        grainAngle=0.0,
        youngsL=10.0e9,
        youngsR=0.5e9,
        shearLR=0.6e9,
        poissonLR=0.37,
        poissonRL=0.37 * 0.5e9 / 10.0e9,
        density=400.0,
        moisture=0.12,
        dEdMcL=0.0,
        dEdMcR=0.0,
        dGdMc=0.0,
        dRhodMc=0.0,
        lossFactor=0.01,
        rimStiffness=0.0,
        fMax=400.0,
        ribs=[],
        outline=[],
        thicknessNx=0,
        thicknessNy=0,
        thicknessGrid=[],
        bridges=[],
        rimRotationalStiffness=0.0,
    )
    return dataclasses.replace(base, **over)  # type: ignore[arg-type]


def isotropic_plate(
    e: float = 70.0e9,
    nu: float = 0.3,
    rho: float = 2700.0,
    h: float = 0.002,
    lx: float = 0.5,
    ly: float = 0.5,
    f_max: float = 1000.0,
) -> models.PlateParams:
    """An isotropic plate written in the orthotropic form (G = E / (2 (1 + nu)))."""
    return p05_plate(
        lengthX=lx,
        lengthY=ly,
        thickness=h,
        youngsL=e,
        youngsR=e,
        shearLR=e / (2.0 * (1.0 + nu)),
        poissonLR=nu,
        poissonRL=nu,
        density=rho,
        fMax=f_max,
    )


def d_coefficients(p: models.PlateParams, moisture_ref: float = 0.12) -> tuple[float, ...]:
    """D11, D22, D12, D66 and rho h at the plate's moisture (SPEC-BODY, SI-0013), uniform h."""
    dmc = p.moisture - moisture_ref
    e_l = p.youngsL + p.dEdMcL * dmc
    e_r = p.youngsR + p.dEdMcR * dmc
    g = p.shearLR + p.dGdMc * dmc
    rho = p.density + p.dRhodMc * dmc
    den = 12.0 * (1.0 - p.poissonLR * p.poissonRL)
    d11 = e_l * p.thickness**3 / den
    d22 = e_r * p.thickness**3 / den
    return d11, d22, p.poissonRL * d11, g * p.thickness**3 / 12.0, rho * p.thickness


def ss_orthotropic_frequencies(
    p: models.PlateParams, count: int, nmax: int = 14, prestress: float = 0.0
) -> NDArray[np.float64]:
    """The lowest ``count`` simply supported frequencies (Hz) of a uniform orthotropic rectangle.

    omega_mn^2 = [D11 k_m^4 + 2 (D12 + 2 D66) k_m^2 k_n^2 + D22 k_n^4
                  + N (k_m^2 + k_n^2)] / (rho h),
    k_m = m pi / Lx, k_n = n pi / Ly, grain along x (B2-T1, B2-T5, B2-R4).
    """
    d11, d22, d12, d66, rho_h = d_coefficients(p)
    out = []
    for m in range(1, nmax + 1):
        for n in range(1, nmax + 1):
            km = m * math.pi / p.lengthX
            kn = n * math.pi / p.lengthY
            w2 = (
                d11 * km**4
                + 2.0 * (d12 + 2.0 * d66) * km**2 * kn**2
                + d22 * kn**4
                + prestress * (km**2 + kn**2)
            ) / rho_h
            out.append(math.sqrt(w2) / (2.0 * math.pi))
    return np.sort(np.array(out))[:count]
