# SPDX-License-Identifier: MIT
"""B2-T4: a ribbed rectangle against the P0.5 reference plate, first 20 modes within 2 %.

Plate: the P0.5-T4 plate (spruce-like, E_L = 10 GPa, E_R = 0.5 GPa, G_LR = 0.6 GPa, nu_LR = 0.37,
rho = 400 kg/m^3, 0.6 m x 0.4 m x 3 mm, grain along x), simply supported. Ribs: three spruce beams
(E = 10 GPa, rho = 400 kg/m^3, width 20 mm, height 15 mm) running the full width, parallel to y, at
x = 0.15, 0.30, 0.45 m. The ribs are expressed identically in both models (same E, section, density
and positions).

Modelling differences, stated: none in the physics. Both models treat a rib as an Euler-Bernoulli
beam acting at the plate's mid-surface (z = 0), with E I = E w h^3 / 12 and rho A = rho w h, and
neglect torsion, rotary inertia and the offset between the beam and the plate mid-surfaces
(docs/dev/body-compiler.md and reference/README.md "Limits"). The numerics differ: the compiler is
an Argyris finite-element model with the simply supported rim as a stiff penalty spring, the
reference a second-order finite-difference scheme (so its values are Richardson-extrapolated).

Reference: ``pme_reference.plate_modes`` on 24 x 16, 48 x 32 and 96 x 64 (ratio 2, rib lines on grid
nodes), modes tracked across grids and with the compiler by the modal assurance criterion of shapes
sampled at 48 common points (see test_b2_r5_reference_unribbed.py), extrapolated as
``f* = f_fine + (f_fine - f_mid) / 3``. Pairing of compiler and reference modes: the assignment
with the largest total MAC (Hungarian), not sorted index; the sorted pairing is also asserted. The
finest-grid 20th and 21st modes are close together (the 21st is at 610.88 Hz), so the order at the
cut can change on extrapolation; the "first 20" are therefore taken after sorting the 24 tracked
modes by their extrapolated frequency.

Tolerance (phase card, B2-T4): |f_B2 / f_ref - 1| <= 0.02 for each of the first 20 modes.
Environment variables and runtime: see test_b2_r5_reference_unribbed.py (about 8 s for the ladder,
1 to 2 s per compiler run).
"""

from __future__ import annotations

import numpy as np
import pytest
from test_b2_r5_reference_unribbed import (
    N_MODES,
    TOLERANCE,
    ReferenceModes,
    compare,
    reference_ladder,
    reference_module,
    report,
)

RIBS = (0.15, 0.30, 0.45)
F_MAX = 700.0  # above the 20th mode (about 614 Hz) and the next few, so none is missed


@pytest.fixture(scope="module")
def ribbed_reference() -> ReferenceModes:
    return reference_ladder(reference_module(), RIBS)


def test_b2_t4_reference_ladder_is_second_order(ribbed_reference: ReferenceModes) -> None:
    """B2-T4: the reference's Richardson ladder is in its asymptotic range (orders 1.8 to 2.1)."""
    order = ribbed_reference.order[:N_MODES]
    print("observed orders:", np.round(order, 2).tolist())
    assert np.all(order > 1.8) and np.all(order < 2.1), order
    # the first mode of the issued figures (reference-solver.md): 134.7326 Hz
    assert ribbed_reference.freq_hz[0] == pytest.approx(134.7326, abs=1e-3)


@pytest.mark.parametrize("mesh_size", (0.06, 0.04))
def test_b2_t4_ribbed_plate_first_20_modes_within_2_percent(
    ribbed_reference: ReferenceModes, mesh_size: float
) -> None:
    """B2-T4: ribbed rectangle against the P0.5 plate, first 20 modes within 2 %."""
    cmp = compare(ribbed_reference, RIBS, mesh_size, F_MAX)
    report(f"B2-T4 mesh {mesh_size} m", ribbed_reference, cmp)
    assert cmp.mac.min() > 0.9, cmp.mac  # every compiler mode has a reference partner
    assert float(cmp.rel_err_mac.max()) <= TOLERANCE
    assert float(cmp.rel_err_sorted.max()) <= TOLERANCE


def test_b2_t4_ribs_matter_a_plate_without_ribs_fails_the_comparison(
    ribbed_reference: ReferenceModes,
) -> None:
    """B2-T4: negative control. The compiler without ribs misses the ribbed reference by >> 2 %."""
    cmp = compare(ribbed_reference, (), 0.06, F_MAX)
    print(
        f"unribbed compiler vs ribbed reference: worst sorted error {cmp.rel_err_sorted.max():.2f}"
    )
    assert float(cmp.rel_err_sorted.max()) > 10 * TOLERANCE
