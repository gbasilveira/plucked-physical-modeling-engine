# SPDX-License-Identifier: MIT
"""Triangle meshing of the plate with Gmsh (B2-R3).

Gmsh is called as a separately installed library (ADR-0002: never vendored). The rib and bridge axes
are fragmented into the plate surface so that the triangle mesh has element edges along every beam
axis; the beam terms are then integrated on exactly those mesh edges. (SPEC-BODY "Plate model")
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
from numpy.typing import NDArray

from pme_compiler.body.geometry import Geometry

IntArray = NDArray[np.int64]
Array = NDArray[np.float64]


@dataclass(frozen=True)
class PlateMesh:
    """Triangles (3, M) over nodes (2, N); ``beam_edges[i]``: node pairs (2, E_i) of beam i."""

    p: Array
    t: IntArray
    beam_edges: tuple[IntArray, ...]
    mesh_size: float


def default_mesh_size(geom: Geometry) -> float:
    """A mesh size resolving the bending wavelength at f_max with ~3.5 elements (Argyris, quintic).

    The bending wavenumber is k = (rho h omega^2 / D)^(1/4) with the smaller of D11, D22; it is
    also capped at a sixth of the shorter bounding-box side so low-frequency shapes are resolved.
    """
    m = geom.material
    h = geom.thickness.min
    d_min = min(m.E_L, m.E_R) * h**3 / (12.0 * (1.0 - m.nu_lr * m.nu_rl))
    omega = 2.0 * math.pi * geom.f_max
    k = (m.rho * geom.thickness.max * omega**2 / d_min) ** 0.25
    wavelength = 2.0 * math.pi / k
    x0, y0, x1, y1 = geom.bbox
    return float(min(wavelength / 3.5, min(x1 - x0, y1 - y0) / 6.0))


def mesh_plate(geom: Geometry, mesh_size: float | None = None) -> PlateMesh:
    """Mesh the outline with beam axes embedded.

    Raises ``RuntimeError`` if Gmsh is not importable (``pip install gmsh``, extra ``fem``).
    """
    try:
        import gmsh
    except (ImportError, OSError) as exc:  # pragma: no cover - environment
        raise RuntimeError(
            "gmsh is not available: install the 'fem' extra (and, on Linux, libGLU)"
        ) from exc

    h = float(mesh_size) if mesh_size is not None else default_mesh_size(geom)
    if not math.isfinite(h) or h <= 0.0:
        raise ValueError(f"mesh_size: expected a finite value > 0, got {h}")

    gmsh.initialize(readConfigFiles=False)
    try:
        gmsh.option.setNumber("General.Terminal", 0)
        gmsh.option.setNumber("General.NumThreads", 1)
        gmsh.option.setNumber("Mesh.MeshSizeMax", h)
        gmsh.option.setNumber("Mesh.MeshSizeMin", 0.4 * h)
        gmsh.option.setNumber("Mesh.MeshSizeFromPoints", 0)
        gmsh.option.setNumber("Mesh.MeshSizeFromCurvature", 0)
        gmsh.option.setNumber("Mesh.MeshSizeExtendFromBoundary", 1)
        gmsh.option.setNumber("Mesh.Algorithm", 6)  # Frontal-Delaunay
        gmsh.option.setNumber("Mesh.RecombineAll", 0)
        gmsh.option.setNumber("Mesh.ElementOrder", 1)
        gmsh.model.add("plate")
        occ = gmsh.model.occ
        pts = [occ.addPoint(float(x), float(y), 0.0) for x, y in geom.outline]
        lines = [occ.addLine(pts[i], pts[(i + 1) % len(pts)]) for i in range(len(pts))]
        loop = occ.addCurveLoop(lines)
        surf = occ.addPlaneSurface([loop])
        beam_lines = []
        for b in geom.beams:
            a = occ.addPoint(b.p0[0], b.p0[1], 0.0)
            c = occ.addPoint(b.p1[0], b.p1[1], 0.0)
            beam_lines.append(occ.addLine(a, c))
        if beam_lines:
            _, outmap = occ.fragment([(2, surf)], [(1, tag) for tag in beam_lines])
            pieces = [[tag for dim, tag in entry if dim == 1] for entry in outmap[1:]]
        else:
            pieces = []
        occ.synchronize()
        gmsh.model.mesh.generate(2)

        tags, coords, _ = gmsh.model.mesh.getNodes()
        node_xy = np.asarray(coords, dtype=np.float64).reshape(-1, 3)[:, :2]
        tag_to_row = {int(t): i for i, t in enumerate(tags)}

        tri_nodes: list[int] = []
        for dim, tag in gmsh.model.getEntities(2):
            types, _, enodes = gmsh.model.mesh.getElements(dim, tag)
            for et, nodes in zip(types, enodes, strict=True):
                if et != 2:
                    raise RuntimeError(f"unexpected Gmsh element type {et} in the plate surface")
                tri_nodes.extend(int(n) for n in nodes)
        tri_rows = np.array([tag_to_row[n] for n in tri_nodes], dtype=np.int64).reshape(-1, 3)
        used = np.unique(tri_rows)
        remap = -np.ones(len(node_xy), dtype=np.int64)
        remap[used] = np.arange(len(used))
        p = np.ascontiguousarray(node_xy[used].T)
        t = np.ascontiguousarray(remap[tri_rows].T)

        edges: list[IntArray] = []
        for piece_tags in pieces:
            pairs: list[tuple[int, int]] = []
            for tag in piece_tags:
                types, _, enodes = gmsh.model.mesh.getElements(1, tag)
                for et, nodes in zip(types, enodes, strict=True):
                    if et != 1:
                        continue
                    arr = np.array([tag_to_row[int(n)] for n in nodes], dtype=np.int64)
                    pairs.extend((int(a), int(b)) for a, b in arr.reshape(-1, 2))
            if not pairs:
                raise RuntimeError("a rib or bridge segment produced no mesh edges")
            e = np.array(pairs, dtype=np.int64).T
            mapped = remap[e]
            if np.any(mapped < 0):
                raise RuntimeError("a beam edge lies outside the triangle mesh")
            edges.append(np.ascontiguousarray(mapped))
        return PlateMesh(p=p, t=t, beam_edges=tuple(edges), mesh_size=h)
    finally:
        gmsh.finalize()
