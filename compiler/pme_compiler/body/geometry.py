# SPDX-License-Identifier: MIT
"""Plate geometry and wood data, validated (B2-R1, B2-R2 moisture law).

Reads ``models.PlateParams`` (SI-0003) and returns normalised, checked inputs for the mesher and the
assembler. Invalid input raises ``ValueError`` naming the field (``plate.<member>``).
(SPEC-BODY "Geometry inputs", "Plate model (offline)")
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
from numpy.typing import NDArray

from pme_compiler.schema import models

Array = NDArray[np.float64]

# SPEC-BODY SI-0013: the listed constants are the values at MC_ref. A module constant until the next
# ICR adds PlateParams::moistureRef.
MC_REF = 0.12

BOUNDARY_CONDITIONS = ("elastic", "simply_supported", "clamped")


@dataclass(frozen=True)
class Beam:
    """A straight beam segment of a rib or bridge, rectangular section (SPEC-BODY ribs, bridges)."""

    p0: tuple[float, float]
    p1: tuple[float, float]
    width: float
    height: float
    youngs: float
    density: float
    label: str

    @property
    def length(self) -> float:
        return math.hypot(self.p1[0] - self.p0[0], self.p1[1] - self.p0[1])

    @property
    def bending_stiffness(self) -> float:
        """E I of the rectangular section for out-of-plate bending, I = width * height^3 / 12."""
        return self.youngs * self.width * self.height**3 / 12.0

    @property
    def mass_per_length(self) -> float:
        """rho A of the section."""
        return self.density * self.width * self.height


@dataclass(frozen=True)
class ThicknessMap:
    """h(x, y): uniform, or bilinear on a regular Nx x Ny grid over the outline's bounding box.

    The grid is row-major [j * Nx + i], i along x and j along y (body.hpp). (SPEC-BODY SI-0003)
    """

    uniform: float
    nx: int = 0
    ny: int = 0
    grid: Array | None = None
    x0: float = 0.0
    y0: float = 0.0
    x1: float = 1.0
    y1: float = 1.0

    def at(self, x: Array, y: Array) -> Array:
        """h at the points (x, y) (arrays of equal shape); clamped to the bounding box."""
        if self.grid is None:
            return np.full(np.shape(x), self.uniform, dtype=np.float64)
        g = self.grid.reshape(self.ny, self.nx)
        u = np.clip((np.asarray(x) - self.x0) / (self.x1 - self.x0), 0.0, 1.0) * (self.nx - 1)
        v = np.clip((np.asarray(y) - self.y0) / (self.y1 - self.y0), 0.0, 1.0) * (self.ny - 1)
        i = np.minimum(u.astype(np.int64), self.nx - 2)
        j = np.minimum(v.astype(np.int64), self.ny - 2)
        fu = u - i
        fv = v - j
        return np.asarray(
            (1 - fu) * (1 - fv) * g[j, i]
            + fu * (1 - fv) * g[j, i + 1]
            + (1 - fu) * fv * g[j + 1, i]
            + fu * fv * g[j + 1, i + 1],
            dtype=np.float64,
        )

    @property
    def max(self) -> float:
        return self.uniform if self.grid is None else float(self.grid.max())

    @property
    def min(self) -> float:
        return self.uniform if self.grid is None else float(self.grid.min())


@dataclass(frozen=True)
class Material:
    """Constants at the plate's moisture content (SPEC-BODY, SI-0013)."""

    E_L: float
    E_R: float
    G_LR: float
    nu_lr: float
    nu_rl: float
    rho: float
    eta: float

    def d_coefficients(self, h: Array) -> tuple[Array, Array, Array, Array]:
        """D11, D22, D12, D66 for thickness ``h``.

        # SPEC-BODY Kirchhoff form: D11 = E_L h^3 / (12 (1 - nu_lr nu_rl)), D22 likewise with E_R,
        # D12 = nu_rl D11, D66 = G_LR h^3 / 12.
        """
        den = 12.0 * (1.0 - self.nu_lr * self.nu_rl)
        h3 = h**3
        d11 = self.E_L * h3 / den
        d22 = self.E_R * h3 / den
        return d11, d22, self.nu_rl * d11, self.G_LR * h3 / 12.0


@dataclass(frozen=True)
class Geometry:
    outline: Array  # [n, 2] body-frame vertices
    thickness: ThicknessMap
    grain_angle: float
    material: Material
    ribs: tuple[Beam, ...]
    bridges: tuple[Beam, ...]
    rim_stiffness: float
    rim_rotational_stiffness: float
    f_max: float

    @property
    def beams(self) -> tuple[Beam, ...]:
        return self.ribs + self.bridges

    @property
    def bbox(self) -> tuple[float, float, float, float]:
        return (
            float(self.outline[:, 0].min()),
            float(self.outline[:, 1].min()),
            float(self.outline[:, 0].max()),
            float(self.outline[:, 1].max()),
        )

    @property
    def area(self) -> float:
        return polygon_area(self.outline)


def polygon_area(poly: Array) -> float:
    """Signed-free area of a simple polygon (shoelace)."""
    x, y = poly[:, 0], poly[:, 1]
    return float(0.5 * abs(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1))))


def _cross(ax: float, ay: float, bx: float, by: float) -> float:
    return ax * by - ay * bx


def _segments_properly_cross(
    a: Array | tuple[float, float],
    b: Array | tuple[float, float],
    c: Array | tuple[float, float],
    d: Array | tuple[float, float],
    tol: float,
) -> bool:
    """True if open segments ab and cd cross at a point interior to both (not merely touching)."""
    d1 = _cross(b[0] - a[0], b[1] - a[1], c[0] - a[0], c[1] - a[1])
    d2 = _cross(b[0] - a[0], b[1] - a[1], d[0] - a[0], d[1] - a[1])
    d3 = _cross(d[0] - c[0], d[1] - c[1], a[0] - c[0], a[1] - c[1])
    d4 = _cross(d[0] - c[0], d[1] - c[1], b[0] - c[0], b[1] - c[1])
    return ((d1 > tol and d2 < -tol) or (d1 < -tol and d2 > tol)) and (
        (d3 > tol and d4 < -tol) or (d3 < -tol and d4 > tol)
    )


def _point_segment_distance(p: tuple[float, float], a: Array, b: Array) -> float:
    ab = b - a
    t = float(np.dot(np.asarray(p) - a, ab) / np.dot(ab, ab))
    t = min(1.0, max(0.0, t))
    return float(np.linalg.norm(np.asarray(p) - (a + t * ab)))


def point_in_polygon(p: tuple[float, float], poly: Array, tol: float) -> bool:
    """True if ``p`` lies inside the polygon or within ``tol`` of its boundary."""
    n = len(poly)
    for i in range(n):
        if _point_segment_distance(p, poly[i], poly[(i + 1) % n]) <= tol:
            return True
    inside = False
    x, y = p
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            inside = not inside
    return inside


def _check_simple_polygon(poly: Array, scale: float) -> None:
    n = len(poly)
    tol = 1e-12 * scale * scale
    if polygon_area(poly) <= 1e-12 * scale * scale:
        raise ValueError("plate.outline: the polygon has zero area")
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        if np.linalg.norm(b - a) <= 1e-12 * scale:
            raise ValueError(f"plate.outline: vertices {i} and {(i + 1) % n} coincide")
        for j in range(i + 1, n):
            if j == i + 1 or (i == 0 and j == n - 1):
                continue
            c, d = poly[j], poly[(j + 1) % n]
            if _segments_properly_cross(a, b, c, d, tol):
                raise ValueError(f"plate.outline: edges {i} and {j} intersect (self-intersection)")


def _positive(value: float, name: str) -> float:
    if not math.isfinite(value) or value <= 0.0:
        raise ValueError(f"{name}: expected a finite value > 0, got {value}")
    return float(value)


def _nonneg(value: float, name: str) -> float:
    if not math.isfinite(value) or value < 0.0:
        raise ValueError(f"{name}: expected a finite value >= 0, got {value}")
    return float(value)


def effective_material(plate: models.PlateParams) -> Material:
    """Constants at the plate's moisture content.

    # SPEC-BODY / SI-0013: E_L(MC) = E_L + dE_L/dMC (MC - MC_ref), likewise E_R, G_LR and rho,
    # MC_ref = 0.12. A combination that makes a modulus or the density non-positive is rejected.
    """
    dmc = float(plate.moisture) - MC_REF
    if not math.isfinite(dmc):
        raise ValueError(f"plate.moisture: expected a finite value, got {plate.moisture}")
    e_l = plate.youngsL + plate.dEdMcL * dmc
    e_r = plate.youngsR + plate.dEdMcR * dmc
    g_lr = plate.shearLR + plate.dGdMc * dmc
    rho = plate.density + plate.dRhodMc * dmc
    for value, name in ((e_l, "youngsL"), (e_r, "youngsR"), (g_lr, "shearLR"), (rho, "density")):
        if not math.isfinite(value) or value <= 0.0:
            raise ValueError(
                f"plate.{name}: moisture {plate.moisture} gives a non-positive value ({value})"
            )
    for value, name in (
        (plate.poissonLR, "poissonLR"),
        (plate.poissonRL, "poissonRL"),
        (plate.lossFactor, "lossFactor"),
    ):
        if not math.isfinite(value):
            raise ValueError(f"plate.{name}: expected a finite value, got {value}")
    if plate.lossFactor < 0.0:
        raise ValueError(f"plate.lossFactor: expected >= 0, got {plate.lossFactor}")
    if 1.0 - plate.poissonLR * plate.poissonRL <= 0.0:
        raise ValueError("plate.poissonLR: 1 - poissonLR * poissonRL must be > 0")
    return Material(
        E_L=e_l,
        E_R=e_r,
        G_LR=g_lr,
        nu_lr=float(plate.poissonLR),
        nu_rl=float(plate.poissonRL),
        rho=rho,
        eta=float(plate.lossFactor),
    )


def _beam_segments(
    pts: list[tuple[float, float]],
    width: float,
    height: float,
    youngs: float,
    density: float,
    label: str,
) -> list[Beam]:
    return [
        Beam(pts[i], pts[i + 1], width, height, youngs, density, f"{label}.path[{i}..{i + 1}]")
        for i in range(len(pts) - 1)
    ]


def from_plate(plate: models.PlateParams, f_max: float | None = None) -> Geometry:
    """Validate ``plate`` and return the normalised geometry. Raises ``ValueError`` naming a field.

    # SPEC-BODY Geometry inputs (SI-0003): outline, thickness grid, ribs, bridges, rim stiffness.
    """
    material = effective_material(plate)
    if plate.outline:
        if len(plate.outline) < 3:
            raise ValueError("plate.outline: expected 0 or at least 3 vertices")
        outline = np.array([[p.x, p.y] for p in plate.outline], dtype=np.float64)
        if not np.all(np.isfinite(outline)):
            raise ValueError("plate.outline: vertices must be finite")
    else:
        lx = _positive(plate.lengthX, "plate.lengthX")
        ly = _positive(plate.lengthY, "plate.lengthY")
        outline = np.array([[0.0, 0.0], [lx, 0.0], [lx, ly], [0.0, ly]], dtype=np.float64)
    scale = float(max(np.ptp(outline[:, 0]), np.ptp(outline[:, 1])))
    if scale <= 0.0:
        raise ValueError("plate.outline: the polygon has zero extent")
    _check_simple_polygon(outline, scale)
    x0, y0 = float(outline[:, 0].min()), float(outline[:, 1].min())
    x1, y1 = float(outline[:, 0].max()), float(outline[:, 1].max())

    if plate.thicknessGrid:
        if plate.thicknessNx < 2 or plate.thicknessNy < 2:
            raise ValueError("plate.thicknessNx: the thickness grid needs Nx >= 2 and Ny >= 2")
        if len(plate.thicknessGrid) != plate.thicknessNx * plate.thicknessNy:
            raise ValueError("plate.thicknessGrid: size must equal thicknessNx * thicknessNy")
        grid = np.array(plate.thicknessGrid, dtype=np.float64)
        if not np.all(np.isfinite(grid)) or np.any(grid <= 0.0):
            raise ValueError("plate.thicknessGrid: every thickness must be finite and > 0")
        thickness = ThicknessMap(
            uniform=float(grid.mean()),
            nx=int(plate.thicknessNx),
            ny=int(plate.thicknessNy),
            grid=grid,
            x0=x0,
            y0=y0,
            x1=x1,
            y1=y1,
        )
    else:
        thickness = ThicknessMap(uniform=_positive(plate.thickness, "plate.thickness"))

    tol = 1e-9 * scale
    ribs: list[Beam] = []
    for i, r in enumerate(plate.ribs):
        name = f"plate.ribs[{i}]"
        w = _positive(r.width, f"{name}.width")
        hgt = _positive(r.height, f"{name}.height")
        e = _positive(r.youngs, f"{name}.youngs")
        rho = _positive(r.density, f"{name}.density")
        pts = [(float(r.start.x), float(r.start.y)), (float(r.end.x), float(r.end.y))]
        if not all(math.isfinite(c) for p in pts for c in p):
            raise ValueError(f"{name}: coordinates must be finite")
        ribs.append(Beam(pts[0], pts[1], w, hgt, e, rho, name))
    bridges: list[Beam] = []
    for i, b in enumerate(plate.bridges):
        name = f"plate.bridges[{i}]"
        w = _positive(b.width, f"{name}.width")
        hgt = _positive(b.height, f"{name}.height")
        e = _positive(b.youngs, f"{name}.youngs")
        rho = _positive(b.density, f"{name}.density")
        if len(b.path) < 2:
            raise ValueError(f"{name}.path: expected at least 2 points")
        pts = [(float(p.x), float(p.y)) for p in b.path]
        if not all(math.isfinite(c) for p in pts for c in p):
            raise ValueError(f"{name}.path: coordinates must be finite")
        bridges += _beam_segments(pts, w, hgt, e, rho, name)
    n = len(outline)
    for beam in ribs + bridges:
        if beam.length <= tol:
            raise ValueError(f"{beam.label}: zero-length segment")
        for p in (beam.p0, beam.p1):
            if not point_in_polygon(p, outline, tol):
                raise ValueError(f"{beam.label}: end point {p} lies outside plate.outline")
        for k in range(n):
            if _segments_properly_cross(beam.p0, beam.p1, outline[k], outline[(k + 1) % n], 0.0):
                raise ValueError(f"{beam.label}: leaves plate.outline")

    rim = _nonneg(plate.rimStiffness, "plate.rimStiffness")
    rim_rot = _nonneg(plate.rimRotationalStiffness, "plate.rimRotationalStiffness")
    fmax = _positive(plate.fMax if f_max is None else f_max, "plate.fMax")
    if not math.isfinite(plate.grainAngle):
        raise ValueError("plate.grainAngle: expected a finite value")
    return Geometry(
        outline=outline,
        thickness=thickness,
        grain_angle=float(plate.grainAngle),
        material=material,
        ribs=tuple(ribs),
        bridges=tuple(bridges),
        rim_stiffness=rim,
        rim_rotational_stiffness=rim_rot,
        f_max=fmax,
    )
