# SPDX-License-Identifier: MIT
"""Shared helpers of the example instrument generators (B1-R6).

Every parameter value in the examples is NOMINAL: chosen by the planner for testing, not measured
or fitted. Experiments E1 and P6 replace them. Formulas cite SPEC-STR and SPEC-HAM.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from itertools import pairwise
from pathlib import Path

from pme_compiler.schema import models
from pme_compiler.schema.loader import dump

PRODUCER = "nominal"


def sig(x: float, digits: int = 10) -> float:
    """Round to ``digits`` significant digits so that derived values are short decimals."""
    return float(f"{x:.{digits}g}")


def loglin(x: float, x0: float, y0: float, x1: float, y1: float) -> float:
    """Log-linear interpolation between (x0, y0) and (x1, y1)."""
    return float(y0 * (y1 / y0) ** ((x - x0) / (x1 - x0)))


def lin(x: float, x0: float, y0: float, x1: float, y1: float) -> float:
    """Linear interpolation between (x0, y0) and (x1, y1)."""
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def anchored(x: float, anchors: Sequence[tuple[float, float]]) -> float:
    """Log-linear interpolation through (x, y) anchors, in increasing x."""
    for (x0, y0), (x1, y1) in pairwise(anchors):
        if x0 <= x <= x1:
            return loglin(x, x0, y0, x1, y1)
    raise ValueError(f"{x} is outside the anchors")


def equal_tempered(key: int, reference: float = 440.0) -> float:
    """f_1 of a MIDI key (A4 = 69) in equal temperament (SPEC-STR Tension for pitch)."""
    return sig(reference * 2.0 ** ((key - 69) / 12.0))


def linear_mass_density(s: models.StringParams) -> float:
    """rho A of a wound string (SPEC-STR Wound strings), kg/m."""
    return (math.pi / 4.0) * (
        s.rhoCore * s.dCore**2 + s.packing * s.rhoWind * (s.dOuter**2 - s.dCore**2)
    )


def bending_stiffness(s: models.StringParams) -> float:
    """EI of a wound string (SPEC-STR Wound strings), N m^2."""
    return (1.0 + s.epsWind) * math.pi * s.ECore * s.dCore**4 / 64.0


def tension(s: models.StringParams) -> float:
    """Closed-form tension T = 4 L^2 rhoA f1^2 - pi^2 EI / L^2 (SPEC-STR Tension for pitch), N."""
    return (
        4.0 * s.L**2 * linear_mass_density(s) * s.f1Target**2
        - math.pi**2 * bending_stiffness(s) / s.L**2
    )


def inharmonicity(s: models.StringParams) -> float:
    """B = pi^2 EI / (T L^2) (SPEC-STR Modes)."""
    return math.pi**2 * bending_stiffness(s) / (tension(s) * s.L**2)


def plain_string(
    length: float,
    diameter: float,
    density: float,
    youngs: float,
    f1: float,
    losses: tuple[float, float, float],
) -> models.StringParams:
    """A plain (unwound) string: dOuter = dCore, the winding data repeat the core's."""
    return models.StringParams(
        L=length,
        dCore=diameter,
        rhoCore=density,
        ECore=youngs,
        dOuter=diameter,
        rhoWind=density,
        packing=1.0,
        epsWind=0.0,
        f1Target=f1,
        b1=losses[0],
        b2=losses[1],
        b3=losses[2],
        sigmaOverride=[],
        detuneH=0.0,
    )


def wound_outer_diameter(
    length: float,
    f1: float,
    target_tension: float,
    d_core: float,
    rho_core: float,
    youngs: float,
    rho_wind: float,
    packing: float,
    eps_wind: float = 0.0,
) -> float:
    """Outer diameter d_o for which the string at f1 has the target tension.

    rho A = (T + pi^2 EI / L^2) / (4 L^2 f1^2) (SPEC-STR Tension for pitch) and
    rho A = pi/4 [rho_c d_c^2 + eta_w rho_w (d_o^2 - d_c^2)] (SPEC-STR Wound strings).
    """
    ei = (1.0 + eps_wind) * math.pi * youngs * d_core**4 / 64.0
    rho_a = (target_tension + math.pi**2 * ei / length**2) / (4.0 * length**2 * f1**2)
    do_sq = d_core**2 + (4.0 * rho_a / math.pi - rho_core * d_core**2) / (packing * rho_wind)
    if do_sq < d_core**2:
        raise ValueError("the target tension needs a lighter string than the bare core")
    return math.sqrt(do_sq)


def plain_diameter_for_tension(
    length: float, f1: float, target_tension: float, density: float, youngs: float
) -> float:
    """Diameter of a plain string with the target tension at f1 (smaller root of the quadratic).

    T = a d^2 - b d^4 with a = pi L^2 f1^2 rho and b = pi^3 E / (64 L^2) (SPEC-STR Tension for
    pitch with rho A = pi rho d^2 / 4 and EI = pi E d^4 / 64).
    """
    a = math.pi * length**2 * f1**2 * density
    b = math.pi**3 * youngs / (64.0 * length**2)
    disc = a * a - 4.0 * b * target_tension
    if disc < 0.0:
        raise ValueError("no plain string reaches the target tension at this pitch")
    return math.sqrt((a - math.sqrt(disc)) / (2.0 * b))


def hammer_contact_time(mass: float, k: float, p: float, v: float) -> float:
    """Contact time t_c of a rigid felt contact K [delta]_+^p (SPEC-HAM Oracle), s."""
    delta_m = float(((p + 1.0) * mass * v * v / (2.0 * k)) ** (1.0 / (p + 1.0)))
    ratio = (
        math.sqrt(math.pi) * math.gamma(1.0 + 1.0 / (p + 1.0)) / math.gamma(0.5 + 1.0 / (p + 1.0))
    )
    return 2.0 * delta_m / v * ratio


def hammer_stiffness_for_contact_time(mass: float, p: float, v: float, t_c: float) -> float:
    """Felt stiffness K for which the SPEC-HAM oracle contact time at speed v equals t_c, N/m^p."""
    ratio = (
        math.sqrt(math.pi) * math.gamma(1.0 + 1.0 / (p + 1.0)) / math.gamma(0.5 + 1.0 / (p + 1.0))
    )
    delta_m = t_c * v / (2.0 * ratio)
    return float((p + 1.0) * mass * v * v / (2.0 * delta_m ** (p + 1.0)))


def spruce_plate(
    length_x: float,
    length_y: float,
    thickness: float,
    grain_angle: float = 0.0,
    rim_stiffness: float = 0.0,
    outline: list[models.Point] | None = None,
    ribs: list[models.RibDesc] | None = None,
    bridges: list[models.BridgeBeamDesc] | None = None,
) -> models.PlateParams:
    """Nominal spruce soundboard (E_L 11 GPa, E_R 0.9 GPa, G_LR 0.7 GPa, rho 420 kg/m^3)."""
    return models.PlateParams(
        lengthX=length_x,
        lengthY=length_y,
        thickness=thickness,
        grainAngle=grain_angle,
        youngsL=11.0e9,
        youngsR=0.9e9,
        shearLR=0.7e9,
        poissonLR=0.37,
        poissonRL=0.03,
        density=420.0,
        moisture=0.12,
        dEdMcL=0.0,
        dEdMcR=0.0,
        dGdMc=0.0,
        dRhodMc=0.0,
        lossFactor=0.01,
        rimStiffness=rim_stiffness,
        fMax=3000.0,
        ribs=ribs or [],
        outline=outline or [],
        thicknessNx=0,
        thicknessNy=0,
        thicknessGrid=[],
        bridges=bridges or [],
        rimRotationalStiffness=0.0,
    )


def write_example(file: models.InstrumentFile, out_dir: Path) -> Path:
    """Write ``instrument.json`` (canonical form) into ``out_dir``."""
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / "instrument.json"
    dump(file, target)
    return target
