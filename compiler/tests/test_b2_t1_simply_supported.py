# SPDX-License-Identifier: MIT
"""B2-T1: simply supported orthotropic rectangle against the analytic frequencies (0.5 %)."""

from __future__ import annotations

import numpy as np
import pytest
from test_b2_common import p05_plate, point, ss_orthotropic_frequencies

from pme_compiler.body import compile as plate_compile

# The P0.5 plate (E_L = 10 GPa, E_R = 0.5 GPa, G_LR = 0.6 GPa, ...) with f_max chosen so that the
# 20 lowest modes (the 20th is near 335 Hz) are inside the band.
F_MAX = 420.0
MESHES = (0.06, 0.04)  # element size in m; Argyris (quintic) elements


@pytest.mark.parametrize("mesh_size", MESHES)
def test_b2_t1_simply_supported_first_20_frequencies_within_half_percent(
    mesh_size: float,
) -> None:
    """B2-T1: simply supported first 20 frequencies within half percent."""
    plate = p05_plate(fMax=F_MAX)
    c = plate_compile.compile_plate(
        plate, [point(0.3, 0.2)], boundary="simply_supported", mesh_size=mesh_size, grid_order=0
    )
    f = c.table.omega / (2.0 * np.pi)
    ref = ss_orthotropic_frequencies(plate, 24)
    assert f.size >= 20
    # no mode missed, none invented: the count below f_max agrees with the analytic count
    assert f.size == int(np.sum(ref <= F_MAX)) or f.size == int(np.sum(ref <= F_MAX + 1.0))
    rel = np.abs(f[:20] / ref[:20] - 1.0)
    print(f"B2-T1 mesh {mesh_size} m: {c.info.n_elements} elements, worst error {rel.max():.2e}")
    assert rel.max() < 0.005


def test_b2_t1_simply_supported_error_converges_with_the_mesh() -> None:
    """B2-T1: simply supported error converges with the mesh."""
    plate = p05_plate(fMax=F_MAX)
    ref = ss_orthotropic_frequencies(plate, 20)
    worst = []
    for h in MESHES:
        c = plate_compile.compile_plate(
            plate, [point(0.3, 0.2)], boundary="simply_supported", mesh_size=h, grid_order=0
        )
        worst.append(float(np.max(np.abs(c.table.omega[:20] / (2 * np.pi * ref) - 1.0))))
    assert worst[1] <= worst[0] + 1e-12
    assert worst[1] < 5.0e-4
