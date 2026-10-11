# SPDX-License-Identifier: MIT
"""Helmholtz air-cavity mode for guitar-type bodies as a fitted body mode (B4-R4). (SPEC-BODY)

f_H = (c0 / 2 pi) sqrt(S / (V L_eff)), cavity volume V, sound-hole area S, effective neck length
L_eff = t + 1.7 r for a hole of radius r = sqrt(S / pi) in a plate of thickness t. The 1.7 r is the
sum of two flanged-end corrections of 0.85 r each (Rayleigh, The Theory of Sound; Fletcher and
Rossing, The Physics of Musical Instruments, Helmholtz resonator and end correction).

Out of scope: the two-oscillator top-plate and cavity coupling. The Helmholtz frequency alone does
not fix the coupling, so the caller supplies Phi_H at the bridge points (from a mobility fit,
B4-R3, or a measurement).
"""

from __future__ import annotations

import dataclasses
import math

import numpy as np
from numpy.typing import NDArray

from pme_compiler.importers._common import BodyModalTable, ImportDataError, sorted_table

F64 = NDArray[np.float64]
C0_20C = 343.0  # m/s at 20 degC


def effective_neck_length(hole_area: float, plate_thickness: float) -> float:
    """L_eff = t + 1.7 r, r = sqrt(S / pi) (flanged-end correction)."""
    return plate_thickness + 1.7 * math.sqrt(hole_area / math.pi)


def helmholtz_frequency(
    volume: float,
    hole_area: float,
    plate_thickness: float = 0.0,
    neck_length: float | None = None,
    c0: float = C0_20C,
) -> float:
    """f_H = (c0 / 2 pi) sqrt(S / (V L_eff)) in Hz."""
    if volume <= 0 or hole_area <= 0 or plate_thickness < 0 or c0 <= 0:
        raise ImportDataError("volume, hole area and c0 must be > 0 and thickness >= 0")
    l_eff = effective_neck_length(hole_area, plate_thickness) if neck_length is None else neck_length
    if l_eff <= 0:
        raise ImportDataError("effective neck length must be > 0")
    return c0 / (2.0 * math.pi) * math.sqrt(hole_area / (volume * l_eff))


def merge_mode(
    table: BodyModalTable,
    frequency_hz: float,
    q_factor: float,
    phi: F64,
    alpha: F64 | None = None,
    beta: F64 | None = None,
) -> BodyModalTable:
    """Insert one mode (Omega = 2 pi f, sigma = Omega / (2 Q), Phi_H = ``phi`` [P]) keeping the sort.

    Tables with ``alpha``/``beta`` need the new mode's [C] gains; tables with a radiation grid
    (``grid_phi``) cannot be merged (the caller would have to supply Phi_H on the grid).
    """
    if frequency_hz <= 0 or q_factor <= 0:
        raise ImportDataError("frequency and Q must be > 0")
    p = table.phi.shape[1]
    phi = np.asarray(phi, dtype=np.float64)
    if phi.shape != (p,) or not np.all(np.isfinite(phi)):
        raise ImportDataError(f"phi must be {p} finite values (one per bridge point)")
    if table.grid_phi is not None:
        raise ImportDataError("cannot merge into a table with a radiation grid (gridPhi)")
    omega = 2.0 * math.pi * frequency_hz
    sigma = omega / (2.0 * q_factor)
    new_alpha = new_beta = None
    if table.alpha is not None:
        if alpha is None or beta is None:
            raise ImportDataError("table has alpha and beta: supply them for the new mode")
        c = table.alpha.shape[0]
        a_in, b_in = np.asarray(alpha, dtype=np.float64), np.asarray(beta, dtype=np.float64)
        if a_in.shape != (c,) or b_in.shape != (c,):
            raise ImportDataError(f"alpha and beta must have {c} values (one per channel)")
        assert table.beta is not None
        new_alpha = np.concatenate([table.alpha, a_in[:, None]], axis=1)
        new_beta = np.concatenate([table.beta, b_in[:, None]], axis=1)
    merged = dataclasses.replace(
        table,
        omega=np.append(table.omega, omega),
        sigma=np.append(table.sigma, sigma),
        phi=np.vstack([table.phi, phi[None, :]]),
        sigma_rad=None if table.sigma_rad is None else np.append(table.sigma_rad, 0.0),
        alpha=new_alpha,
        beta=new_beta,
    )
    return sorted_table(merged)
