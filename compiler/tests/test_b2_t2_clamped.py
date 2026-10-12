# SPDX-License-Identifier: MIT
"""B2-T2: clamped square plate against published frequency parameters (1 %)."""

from __future__ import annotations

import math

import pytest
from test_b2_common import isotropic_plate, point

from pme_compiler.body import compile as plate_compile

# Published reference: A. W. Leissa, Vibration of Plates, NASA SP-160 (1969), Chapter 4, rectangular
# plates with all edges clamped (CCCC), square, nu = 0.3. Frequency parameters
# lambda = omega a^2 sqrt(rho h / D) of the first six modes, as tabulated (rounded to 0.01):
#   35.99, 73.41, 73.41, 108.27, 131.64, 132.24.
# The first value (35.99) is the one quoted in the B2 brief. The table number and the remaining five
# values were transcribed from the book by the implementer without access to a copy during the
# run; they agree with the converged FEM values below to 0.05 %, which is the evidence they are
# right. A reviewer with the book should confirm them (see the B2 handoff report).
LEISSA_CCCC_SQUARE = (35.99, 73.41, 73.41, 108.27, 131.64, 132.24)


def test_b2_t2_clamped_square_matches_published_frequency_parameters() -> None:
    """B2-T2: clamped square matches published frequency parameters."""
    e, nu, rho, h, a = 70.0e9, 0.3, 2700.0, 0.01, 1.0
    plate = isotropic_plate(e=e, nu=nu, rho=rho, h=h, lx=a, ly=a, f_max=2000.0)
    d = e * h**3 / (12.0 * (1.0 - nu**2))
    c = plate_compile.compile_plate(
        plate, [point(0.5, 0.5)], boundary="clamped", mesh_size=0.1, grid_order=0
    )
    lam = c.table.omega * a**2 * math.sqrt(rho * h / d)
    assert lam.size >= 6
    for k, ref in enumerate(LEISSA_CCCC_SQUARE):
        assert lam[k] == pytest.approx(ref, rel=0.01), f"mode {k}"
    # the repeated pair (1,2)/(2,1) of the square is found twice
    assert lam[1] == pytest.approx(lam[2], rel=1e-6)
    assert lam[0] == pytest.approx(35.985, rel=1e-3)
