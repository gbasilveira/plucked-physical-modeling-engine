# SPDX-License-Identifier: MIT
"""Generator of the SYNTHETIC MAESSTRO-layout fixture used by B4-T3 (ours, MIT; not MAESSTRO data).

A simply supported rectangular plate (0.30 m x 0.40 m, rho h = 3.2 kg/m^2, mesh pitch 0.01 m) with
the analytic modes (m, n) in 1..3 x 1..3, shapes scaled to unit peak (so the adapter must mass-
normalise), loss factor 0.01 + 0.002 m n. Layout: see pme_compiler/importers/maesstro.py.

    python compiler/tests/data/b4/make_maesstro_fixture.py [output.mat]
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np
from scipy.io import savemat

A, B, RHO_H, PITCH = 0.30, 0.40, 3.2, 0.01
D_FLEX = 8.0  # N m, flexural rigidity of the synthetic plate


def build() -> dict[str, np.ndarray]:
    nx, ny = round(A / PITCH) + 1, round(B / PITCH) + 1
    xs, ys = np.arange(nx) * PITCH, np.arange(ny) * PITCH
    gx, gy = np.meshgrid(xs, ys, indexing="ij")
    nodes = np.column_stack([gx.ravel(), gy.ravel()])
    wx = np.full(nx, PITCH)
    wx[[0, -1]] = PITCH / 2
    wy = np.full(ny, PITCH)
    wy[[0, -1]] = PITCH / 2
    mass = (RHO_H * np.outer(wx, wy)).ravel()
    modes, freq, eta = [], [], []
    for m in range(1, 4):
        for n in range(1, 4):
            # Simply supported plate: f = (pi/2) sqrt(D / (rho h)) ((m/a)^2 + (n/b)^2)
            f = (math.pi / 2) * math.sqrt(D_FLEX / RHO_H) * ((m / A) ** 2 + (n / B) ** 2)
            modes.append(np.sin(m * math.pi * nodes[:, 0] / A) * np.sin(n * math.pi * nodes[:, 1] / B))
            freq.append(f)
            eta.append(0.01 + 0.002 * m * n)
    order = np.argsort(freq)[::-1]  # deliberately descending: the adapter must sort
    return {
        "frequencies": np.array(freq)[order],
        "loss_factor": np.array(eta)[order],
        "nodes": nodes,
        "modes": np.column_stack(modes)[:, order],
        "nodal_mass": mass,
        "normalisation": np.array("unit"),
    }


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("synthetic_maesstro.mat")
    savemat(out, build())
    print(f"wrote {out}")
