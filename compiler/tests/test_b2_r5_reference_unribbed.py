# SPDX-License-Identifier: MIT
"""B2-R5 against the P0.5 reference plate (unribbed), and the shared comparison helpers of B2-T4.

Expected values come from the P0.5 reference solver (``pme_reference.plate_modes``), never from the
compiler: the discrete operator's frequencies on three grids with ratio 2 (24 x 16, 48 x 32,
96 x 64), Richardson-extrapolated to h -> 0 (second order, ``f* = f_fine + (f_fine - f_mid) / 3``).
Modes are paired across grids, and with the compiler's modes, by the modal assurance criterion
``MAC = (a.b)^2 / (a.a b.b)`` of the shapes sampled at a common lattice of 48 points off the
symmetry lines (docs/dev/reference-solver.md, "Using it from B2-T4"), not by sorted index: modes
such as 309.30 and 310.14 Hz swap places between grids.

Environment (as tools/tests/test_p0_5_r7_python.py): ``PME_REFERENCE_PYDIR`` names the directory
that holds the ``pme_reference`` module (build with ``-DPME_BUILD_PYTHON_BINDINGS=ON``; the
directory is ``build/<preset>/reference/python``). The tests skip when it is unset, unless
``PME_REFERENCE_REQUIRED=1``, where a missing module is a failure.

Runtime: the reference ladder takes about 8 s (96 x 64 dominates), each compiler run 1 to 2 s.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import pytest
from numpy.typing import NDArray
from scipy.optimize import linear_sum_assignment
from test_b2_common import p05_plate, point

from pme_compiler.body.compile import compile_plate
from pme_compiler.schema import models

Array = NDArray[np.float64]

TOLERANCE = 0.02  # B2-T4 (phase card): first 20 modes within 2 %
N_MODES = 20
# Finest first; the rib lines x = 0.15, 0.3, 0.45 are grid nodes on all three.
LADDER = ((96, 64), (48, 32), (24, 16))
N_FINE = 24  # modes requested on the finest grid (a margin above 20 for the order at the cut)
N_COARSE = 32  # modes requested on the coarser grids so that every partner is present
LX, LY, H = 0.6, 0.4, 0.003
# Lattice of 8 x 6 points off the symmetry lines x = 0.3 and y = 0.2 and off the ribs.
POINTS: list[tuple[float, float]] = [
    (LX * (i + 0.4) / 8.0, LY * (j + 0.45) / 6.0) for i in range(8) for j in range(6)
]


def reference_module() -> Any:
    """Load ``pme_reference`` as test_p0_5_r7_python.py does (skip, or fail when required)."""
    value = os.environ.get("PME_REFERENCE_PYDIR")
    if not value:
        if os.environ.get("PME_REFERENCE_REQUIRED") == "1":
            pytest.fail("PME_REFERENCE_PYDIR is not set but PME_REFERENCE_REQUIRED=1")
        pytest.skip("PME_REFERENCE_PYDIR is not set (build with -DPME_BUILD_PYTHON_BINDINGS=ON)")
    pydir = Path(value)
    sys.path.insert(0, str(pydir))
    try:
        import pme_reference
    except ImportError:
        if os.environ.get("PME_REFERENCE_REQUIRED") == "1":
            pytest.fail(f"pme_reference cannot be imported from {pydir}")
        pytest.skip(f"pme_reference cannot be imported from {pydir}")
    finally:
        sys.path.remove(str(pydir))
    return pme_reference


def reference_plate(rib_xs: tuple[float, ...]) -> dict[str, Any]:
    """The scenario ``plate`` object: the P0.5-T4 plate, spruce ribs 20 x 15 mm parallel to y."""
    return {
        "length_x": LX,
        "length_y": LY,
        "thickness": H,
        "youngs_l": 10.0e9,
        "youngs_r": 0.5e9,
        "shear_lr": 0.6e9,
        "poisson_lr": 0.37,
        "poisson_rl": 0.37 * 0.5e9 / 10.0e9,
        "density": 400.0,
        "grid": {"nx": 24, "ny": 16},
        "ribs": [
            {
                "start": {"x": x, "y": 0.0},
                "end": {"x": x, "y": LY},
                "width": 0.02,
                "height": 0.015,
                "youngs": 10.0e9,
                "density": 400.0,
            }
            for x in rib_xs
        ],
    }


def compiler_plate(rib_xs: tuple[float, ...], f_max: float) -> models.PlateParams:
    """The same plate for the compiler (B2's P0.5 plate builder; same E, section, density, x)."""
    ribs = [
        models.RibDesc(
            start=point(x, 0.0),
            end=point(x, LY),
            width=0.02,
            height=0.015,
            youngs=10.0e9,
            density=400.0,
        )
        for x in rib_xs
    ]
    return p05_plate(ribs=ribs, fMax=f_max)


def mac_matrix(a: Array, b: Array) -> Array:
    """MAC between the rows of ``a`` and of ``b``: (a_i . b_j)^2 / (a_i.a_i b_j.b_j)."""
    an = a / np.linalg.norm(a, axis=1)[:, None]
    bn = b / np.linalg.norm(b, axis=1)[:, None]
    out: Array = (an @ bn.T) ** 2
    return out


@dataclass(frozen=True)
class ReferenceModes:
    """Extrapolated reference modes, ascending in the extrapolated frequency."""

    freq_hz: Array  # (N_FINE,) Richardson-extrapolated, ascending
    shapes: Array  # (N_FINE, len(POINTS)) finest-grid shapes at POINTS, same order
    order: Array  # (N_FINE,) observed convergence order of each tracked mode
    fine_hz: Array  # (N_FINE,) finest-grid frequencies, same order
    seconds: float


def reference_ladder(ref: Any, rib_xs: tuple[float, ...]) -> ReferenceModes:
    """Run the reference on the three grids, track modes by MAC and extrapolate (see module doc)."""
    import time

    t0 = time.perf_counter()
    plate = reference_plate(rib_xs)
    results = []
    for (nx, ny), count in zip(LADDER, (N_FINE, N_COARSE, N_COARSE), strict=True):
        r = ref.plate_modes(plate, nx, ny, count, POINTS)
        results.append((np.asarray(r["frequencies_hz"], float), np.asarray(r["values"], float)))
    (f1, v1), (f2, v2), (f3, v3) = results
    # Partner of each finest-grid mode on the coarser grids: the largest MAC.
    p2 = mac_matrix(v1, v2).argmax(axis=1)
    p3 = mac_matrix(v1, v3).argmax(axis=1)
    fm, fc = f2[p2], f3[p3]
    star = f1 + (f1 - fm) / 3.0
    with np.errstate(divide="ignore", invalid="ignore"):
        order = np.log2((fc - fm) / (fm - f1))
    idx = np.argsort(star, kind="stable")
    return ReferenceModes(
        freq_hz=star[idx],
        shapes=v1[idx],
        order=order[idx],
        fine_hz=f1[idx],
        seconds=time.perf_counter() - t0,
    )


@dataclass(frozen=True)
class Comparison:
    freq_b2_hz: Array  # the first N_MODES compiler modes
    freq_ref_hz: Array  # matched reference frequency per compiler mode
    mac: Array  # MAC of each matched pair
    rel_err_mac: Array  # |f_b2 / f_ref - 1| with MAC pairing
    rel_err_sorted: Array  # |f_b2 / f_ref - 1| with the sorted pairing (mode k with mode k)
    n_modes: int  # modes the compiler found below f_max


def compare(
    ref: ReferenceModes, rib_xs: tuple[float, ...], mesh_size: float, f_max: float
) -> Comparison:
    """Compile with ``boundary="simply_supported"`` and compare the first 20 modes with ``ref``."""
    c = compile_plate(
        compiler_plate(rib_xs, f_max),
        [point(x, y) for x, y in POINTS],
        boundary="simply_supported",
        mesh_size=mesh_size,
        grid_order=0,
    )
    f = np.asarray(c.table.omega, float) / (2.0 * np.pi)
    assert f.size >= N_MODES, f"the compiler found only {f.size} modes below {f_max} Hz"
    phi = np.asarray(c.table.phi, float)  # (K, B): shapes at the bridge points = POINTS
    m = mac_matrix(phi[:N_MODES], ref.shapes)
    rows, cols = linear_sum_assignment(-m)  # the pairing with the largest total MAC
    f_ref = ref.freq_hz[cols]
    return Comparison(
        freq_b2_hz=f[:N_MODES],
        freq_ref_hz=f_ref,
        mac=m[rows, cols],
        rel_err_mac=np.abs(f[:N_MODES] / f_ref - 1.0),
        rel_err_sorted=np.abs(f[:N_MODES] / ref.freq_hz[:N_MODES] - 1.0),
        n_modes=int(f.size),
    )


def report(label: str, ref: ReferenceModes, cmp: Comparison) -> None:
    """Print the per-mode table (visible with ``pytest -s`` and in failure output)."""
    print(f"{label}: reference ladder {ref.seconds:.1f} s, compiler found {cmp.n_modes} modes")
    for k in range(N_MODES):
        print(
            f"  mode {k + 1:2d}: B2 {cmp.freq_b2_hz[k]:9.4f} Hz  ref {cmp.freq_ref_hz[k]:9.4f} Hz  "
            f"MAC {cmp.mac[k]:.4f}  err(MAC) {cmp.rel_err_mac[k]:.2e}  "
            f"err(sorted) {cmp.rel_err_sorted[k]:.2e}"
        )
    print(
        f"  worst error {cmp.rel_err_mac.max():.2e} (MAC pairing), "
        f"{cmp.rel_err_sorted.max():.2e} (sorted pairing)"
    )


@pytest.fixture(scope="module")
def unribbed_reference() -> ReferenceModes:
    return reference_ladder(reference_module(), ())


@pytest.mark.parametrize("mesh_size", (0.06, 0.04))
def test_b2_r5_reference_unribbed_first_20_modes_within_2_percent(
    unribbed_reference: ReferenceModes, mesh_size: float
) -> None:
    """B2-R5: unribbed simply supported plate, first 20 modes, compiler vs P0.5 reference (2 %).

    Reference: Richardson-extrapolated discrete-operator frequencies (24 x 16, 48 x 32, 96 x 64).
    The plate is the P0.5-T4 plate (spruce-like, 0.6 m x 0.4 m x 3 mm, grain along x).
    """
    ref = unribbed_reference
    # the reference itself must be in its asymptotic range for the extrapolation to mean anything
    assert np.all(np.abs(ref.order[:N_MODES] - 2.0) < 0.5), ref.order[:N_MODES]
    cmp = compare(ref, (), mesh_size, f_max=420.0)
    report(f"B2-R5 unribbed mesh {mesh_size} m", ref, cmp)
    assert cmp.mac.min() > 0.9, cmp.mac  # every compiler mode has a reference partner
    assert float(cmp.rel_err_mac.max()) <= TOLERANCE
    assert float(cmp.rel_err_sorted.max()) <= TOLERANCE
