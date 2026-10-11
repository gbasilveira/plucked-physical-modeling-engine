# SPDX-License-Identifier: MIT
"""B2-R1 geometry inputs: validation names the field, thickness grid, outline, grain angle."""

from __future__ import annotations

import dataclasses
import math

import numpy as np
import pytest
from test_b2_common import isotropic_plate, p05_plate, point

from pme_compiler.body import compile as plate_compile
from pme_compiler.body import geometry
from pme_compiler.schema import models


def _rib(**over: object) -> models.RibDesc:
    base = models.RibDesc(
        start=point(0.1, 0.1),
        end=point(0.5, 0.3),
        width=0.01,
        height=0.012,
        youngs=10.0e9,
        density=450.0,
    )
    return dataclasses.replace(base, **over)  # type: ignore[arg-type]


def _bridge(**over: object) -> models.BridgeBeamDesc:
    base = models.BridgeBeamDesc(
        path=[point(0.1, 0.2), point(0.5, 0.2)], width=0.01, height=0.01, youngs=9e9, density=700.0
    )
    return dataclasses.replace(base, **over)  # type: ignore[arg-type]


@pytest.mark.parametrize(
    ("over", "field"),
    [
        ({"outline": [point(0, 0), point(1, 0)]}, "plate.outline"),
        ({"outline": [point(0, 0), point(1, 0), point(2, 0)]}, "plate.outline"),
        (
            {"outline": [point(0, 0), point(1, 1), point(1, 0), point(0, 1)]},
            "plate.outline",
        ),
        ({"thickness": 0.0}, "plate.thickness"),
        ({"thickness": -1e-3}, "plate.thickness"),
        ({"lengthX": 0.0}, "plate.lengthX"),
        ({"lengthY": -0.4}, "plate.lengthY"),
        ({"fMax": 0.0}, "plate.fMax"),
        ({"rimStiffness": -1.0}, "plate.rimStiffness"),
        ({"rimRotationalStiffness": -1.0}, "plate.rimRotationalStiffness"),
        ({"youngsR": -1.0}, "plate.youngsR"),
        ({"density": 0.0}, "plate.density"),
        ({"lossFactor": -0.1}, "plate.lossFactor"),
        ({"poissonLR": 20.0, "poissonRL": 1.0}, "plate.poissonLR"),
        (
            {"thicknessNx": 1, "thicknessNy": 2, "thicknessGrid": [0.003, 0.003]},
            "plate.thicknessNx",
        ),
        ({"thicknessNx": 2, "thicknessNy": 2, "thicknessGrid": [0.003] * 3}, "plate.thicknessGrid"),
        (
            {"thicknessNx": 2, "thicknessNy": 2, "thicknessGrid": [0.003, 0.003, 0.0, 0.003]},
            "plate.thicknessGrid",
        ),
        ({"ribs": [_rib(width=0.0)]}, "plate.ribs[0].width"),
        ({"ribs": [_rib(youngs=-1.0)]}, "plate.ribs[0].youngs"),
        ({"ribs": [_rib(), _rib(end=point(0.9, 0.3))]}, "plate.ribs[1]"),
        ({"ribs": [_rib(start=point(0.2, 0.2), end=point(0.2, 0.2))]}, "plate.ribs[0]"),
        ({"bridges": [_bridge(path=[point(0.1, 0.1)])]}, "plate.bridges[0].path"),
        ({"bridges": [_bridge(height=0.0)]}, "plate.bridges[0].height"),
        ({"bridges": [_bridge(path=[point(0.1, 0.2), point(0.7, 0.2)])]}, "plate.bridges[0]"),
        # moisture that drives a modulus or the density to or below zero (SI-0013)
        ({"moisture": 0.5, "dEdMcL": -1.0e11}, "plate.youngsL"),
        ({"moisture": 0.5, "dRhodMc": -2000.0}, "plate.density"),
    ],
)
def test_b2_r1_invalid_geometry_is_rejected_naming_the_field(
    over: dict[str, object], field: str
) -> None:
    plate = p05_plate(**over)
    with pytest.raises(ValueError, match=r"^" + field.replace("[", r"\[").replace("]", r"\]")):
        geometry.from_plate(plate)


def test_b2_r1_bridge_point_outside_the_outline_is_rejected() -> None:
    with pytest.raises(ValueError, match=r"^bridgePoints\[1\]"):
        plate_compile.compile_plate(
            p05_plate(), [point(0.3, 0.2), point(0.7, 0.2)], boundary="simply_supported"
        )


def test_b2_r1_unknown_boundary_option_is_rejected() -> None:
    with pytest.raises(ValueError, match="boundary"):
        plate_compile.compile_plate(p05_plate(), [point(0.3, 0.2)], boundary="free")


def test_b2_r1_empty_outline_is_the_lx_by_ly_rectangle() -> None:
    g = geometry.from_plate(p05_plate())
    assert g.outline.tolist() == [[0.0, 0.0], [0.6, 0.0], [0.6, 0.4], [0.0, 0.4]]
    assert g.area == pytest.approx(0.24)


def test_b2_r1_thickness_grid_is_bilinear_and_row_major() -> None:
    nx, ny = 4, 3
    x0, y0, x1, y1 = 0.0, 0.0, 0.6, 0.4
    xs = np.linspace(x0, x1, nx)
    ys = np.linspace(y0, y1, ny)

    def hf(x: np.ndarray, y: np.ndarray) -> np.ndarray:
        # bilinear: reproduced exactly
        return np.asarray(0.002 + 0.001 * x + 0.0005 * y + 0.002 * x * y)

    grid = [float(hf(xs[i], ys[j])) for j in range(ny) for i in range(nx)]  # [j * Nx + i]
    g = geometry.from_plate(p05_plate(thicknessNx=nx, thicknessNy=ny, thicknessGrid=grid))
    rng = np.random.default_rng(3)
    x = rng.uniform(x0, x1, 200)
    y = rng.uniform(y0, y1, 200)
    assert np.max(np.abs(g.thickness.at(x, y) - hf(x, y))) < 1e-15
    # the corners and the clamping outside the bounding box
    assert g.thickness.at(np.array([x1]), np.array([y1]))[0] == pytest.approx(grid[-1])
    assert g.thickness.at(np.array([2.0]), np.array([-1.0]))[0] == pytest.approx(grid[nx - 1])


def test_b2_r1_uniform_thickness_grid_equals_a_uniform_plate() -> None:
    kw = dict(boundary="simply_supported", mesh_size=0.06, grid_order=0)
    a = plate_compile.compile_plate(p05_plate(fMax=300.0), [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    gridded = p05_plate(fMax=300.0, thicknessNx=3, thicknessNy=2, thicknessGrid=[0.003] * 6)
    b = plate_compile.compile_plate(gridded, [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    assert np.max(np.abs(a.table.omega / b.table.omega - 1.0)) < 1e-9


def test_b2_r1_thicker_plate_is_stiffer_by_the_expected_factor() -> None:
    # omega ~ h for a uniform plate: doubling h doubles every frequency
    kw = dict(boundary="simply_supported", mesh_size=0.06, grid_order=0)
    a = plate_compile.compile_plate(p05_plate(fMax=200.0), [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    b = plate_compile.compile_plate(
        p05_plate(fMax=400.0, thickness=0.006),
        [point(0.3, 0.2)],
        **kw,  # type: ignore[arg-type]
    )
    n = min(a.info.n_modes, 6)
    assert np.max(np.abs(b.table.omega[:n] / a.table.omega[:n] - 2.0)) < 1e-6


def test_b2_r1_grain_angle_90_degrees_swaps_the_roles_of_e_l_and_e_r() -> None:
    kw = dict(boundary="simply_supported", mesh_size=0.06, grid_order=0)
    rotated = p05_plate(fMax=400.0, grainAngle=math.pi / 2)
    swapped = p05_plate(
        fMax=400.0,
        youngsL=0.5e9,
        youngsR=10.0e9,
        poissonLR=rotated.poissonRL,
        poissonRL=rotated.poissonLR,
    )
    a = plate_compile.compile_plate(rotated, [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    b = plate_compile.compile_plate(swapped, [point(0.3, 0.2)], **kw)  # type: ignore[arg-type]
    assert a.info.n_modes == b.info.n_modes
    assert np.max(np.abs(a.table.omega / b.table.omega - 1.0)) < 1e-6


def test_b2_r1_isotropic_plate_is_independent_of_the_grain_angle() -> None:
    kw = dict(boundary="simply_supported", mesh_size=0.07, grid_order=0)
    a = plate_compile.compile_plate(isotropic_plate(), [point(0.25, 0.25)], **kw)  # type: ignore[arg-type]
    b = plate_compile.compile_plate(
        dataclasses.replace(isotropic_plate(), grainAngle=0.7),
        [point(0.25, 0.25)],
        **kw,  # type: ignore[arg-type]
    )
    assert a.info.n_modes == b.info.n_modes
    assert np.max(np.abs(a.table.omega / b.table.omega - 1.0)) < 1e-9


def test_b2_r1_polygon_outline_is_meshed_and_integrates_to_its_area() -> None:
    # an L-shaped soundboard outline (area 0.18 - 0.06 = 0.12 m^2 for the 0.6 x 0.3 minus 0.2 x 0.3)
    outline = [
        point(0.0, 0.0),
        point(0.6, 0.0),
        point(0.6, 0.15),
        point(0.3, 0.15),
        point(0.3, 0.3),
        point(0.0, 0.3),
    ]
    area = 0.6 * 0.15 + 0.3 * 0.15
    plate = p05_plate(outline=outline, fMax=300.0, rimStiffness=1e4)
    c = plate_compile.compile_plate(plate, [point(0.1, 0.1)], mesh_size=0.05, grid_order=4)
    assert c.geometry.area == pytest.approx(area)
    assert c.table.grid_weights is not None
    assert float(c.table.grid_weights.sum()) == pytest.approx(area, rel=1e-12)
    assert c.info.n_modes >= 3
    with pytest.raises(ValueError, match=r"^bridgePoints\[0\]"):
        plate_compile.compile_plate(plate, [point(0.5, 0.25)], mesh_size=0.05)  # in the notch
