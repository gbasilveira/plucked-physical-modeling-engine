# SPDX-License-Identifier: MIT
"""B2-T5: a moisture change shifts the frequencies as the SI-0013 coefficients predict (1 %)."""

from __future__ import annotations

import dataclasses

import numpy as np
import pytest
from test_b2_common import p05_plate, point, ss_orthotropic_frequencies

from pme_compiler.body import compile as plate_compile
from pme_compiler.body.geometry import MC_REF
from pme_compiler.materials import library
from pme_compiler.schema import models

# Slopes per unit moisture content (MC is a fraction, 0.12 = 12 %). The P0.5 reference material
# carries no moisture data, so these are illustrative test values chosen to move each constant by
# a visible amount over MC 0.12 -> 0.16 (E_L -10 %, E_R -10 %, G_LR -10 %, rho +3 %). They are not
# measurements of any wood; the test checks the law, not the numbers (docs/dev/body-compiler.md).
SLOPES = {"dEdMcL": -2.5e10, "dEdMcR": -1.25e9, "dGdMc": -1.5e9, "dRhodMc": 300.0}


def _plate(mc: float) -> models.PlateParams:
    ref = library.load_library()["p0_5_reference_plate"]
    base = library.apply(ref, p05_plate(fMax=300.0))

    return dataclasses.replace(
        base,
        moisture=mc,
        dEdMcL=SLOPES["dEdMcL"],
        dEdMcR=SLOPES["dEdMcR"],
        dGdMc=SLOPES["dGdMc"],
        dRhodMc=SLOPES["dRhodMc"],
    )


def _freqs(mc: float, count: int) -> np.ndarray:
    plate = _plate(mc)
    c = plate_compile.compile_plate(
        plate,
        [point(0.3, 0.2)],
        boundary="simply_supported",
        mesh_size=0.05,
        grid_order=0,
    )
    return np.asarray(c.table.omega[:count] / (2.0 * np.pi))


def test_b2_t5_reference_moisture_is_0_12() -> None:
    assert MC_REF == 0.12


@pytest.mark.parametrize("mc_to", [0.16, 0.08])
def test_b2_t5_moisture_shift_matches_the_si_0013_prediction(mc_to: float) -> None:
    f_ref = _freqs(MC_REF, 10)
    f_new = _freqs(mc_to, 10)
    # prediction from the SI-0013 law applied to the closed-form simply supported frequencies
    pred_ref = ss_orthotropic_frequencies(_plate(MC_REF), 10)
    pred_new = ss_orthotropic_frequencies(_plate(mc_to), 10)
    d_pred = pred_new - pred_ref
    d_fem = f_new - f_ref
    # sign: stiffness falls faster than mass rises for MC above the reference (and the reverse
    # below), so the frequencies move in the direction of -(MC - MC_ref)
    assert np.all(np.sign(d_fem) == np.sign(d_pred))
    assert np.all(np.sign(d_fem) == -np.sign(mc_to - MC_REF))
    ratio = d_fem / d_pred
    print(f"B2-T5 MC {mc_to}: worst |dF_FEM/dF_pred - 1| = {np.max(np.abs(ratio - 1)):.2e}")
    assert np.max(np.abs(ratio - 1.0)) <= 0.01


def test_b2_t5_zero_slopes_leave_the_frequencies_unchanged() -> None:

    base = p05_plate(fMax=300.0)
    a = plate_compile.compile_plate(
        base, [point(0.3, 0.2)], boundary="simply_supported", mesh_size=0.06, grid_order=0
    )
    b = plate_compile.compile_plate(
        dataclasses.replace(base, moisture=0.2),
        [point(0.3, 0.2)],
        boundary="simply_supported",
        mesh_size=0.06,
        grid_order=0,
    )
    assert np.array_equal(a.table.omega, b.table.omega)
