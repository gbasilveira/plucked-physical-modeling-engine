# SPDX-License-Identifier: MIT
"""Plate to modal table: mesh, assemble, solve, sample (B2-R1 to B2-R7).

``compile_plate`` is the Python counterpart of ``compileBody`` for a ``PlateModel`` body
(SPEC-BODY "Contracts"): mass-normalised modes Omega_k, sigma_k, Phi_k at the bridge points and on a
radiation grid. Radiation damping (sigma_k,rad) and the radiation weights are B3's.
"""

from __future__ import annotations

import math
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass

import numpy as np
import scipy.sparse as sp
from numpy.typing import NDArray
from skfem import Basis

from pme_compiler.body import eigen, fem, geometry, mesh
from pme_compiler.body.modal_blob import BodyModalTable
from pme_compiler.schema import models

Array = NDArray[np.float64]

# B2-R5 default radiation grid: element quadrature points of this order (3 points per triangle).
DEFAULT_GRID_ORDER = 2


@dataclass(frozen=True)
class CompileInfo:
    """Facts about one compilation, for reports and the CLI."""

    n_modes: int
    n_rigid_dropped: int
    n_dofs: int
    n_elements: int
    mesh_size: float
    windows: int
    boundary: str
    seconds: float


@dataclass(frozen=True)
class Compiled:
    """The modal table plus the discretisation it came from (kept for tests and B3)."""

    table: BodyModalTable
    info: CompileInfo
    modes: eigen.Modes
    system: fem.System
    geometry: geometry.Geometry


def sample_matrix(basis: Basis, points: Array) -> sp.csc_matrix:
    """Sparse matrix (P x ndofs) interpolating a field of ``basis`` at ``points`` (P, 2).

    # SPEC-BODY: Phi_k(x_b) is interpolated with the element's own basis (Argyris, C1).
    """
    pts = np.ascontiguousarray(np.asarray(points, dtype=np.float64).T)
    return sp.csc_matrix(basis.probes(pts))


def quadrature_matrix(basis: Basis) -> tuple[sp.csc_matrix, Array, Array]:
    """Evaluation matrix (G x ndofs), points (G, 2) and weights (G,) of the quadrature rule."""
    nel = basis.nelems
    nq = basis.dx.shape[1]
    rows: list[Array] = []
    cols: list[Array] = []
    vals: list[Array] = []
    local = np.arange(nel * nq).reshape(nel, nq)
    for i in range(basis.Nbfun):
        rows.append(local.ravel())
        cols.append(np.repeat(basis.element_dofs[i], nq))
        vals.append(np.asarray(basis.basis[i][0]).ravel())
    ev = sp.csc_matrix(
        (np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))),
        shape=(nel * nq, basis.N),
    )
    xy = np.asarray(basis.global_coordinates())  # (2, nel, nq)
    pts = np.stack([xy[0].ravel(), xy[1].ravel()], axis=1)
    return ev, pts, np.asarray(basis.dx, dtype=np.float64).ravel()


def compile_plate(
    plate: models.PlateParams,
    bridge_points: Sequence[models.Point],
    *,
    boundary: str = "elastic",
    prestress: fem.Prestress | None = None,
    mesh_size: float | None = None,
    f_max: float | None = None,
    loss_factor: Callable[[float], float] | None = None,
    grid_order: int = DEFAULT_GRID_ORDER,
    nev: int = 40,
    dense_threshold: int = 400,
) -> Compiled:
    """Compile a plate into a body modal table.

    ``boundary``: ``"elastic"`` (rim springs from the plate, the instrument-file meaning),
    ``"simply_supported"`` or ``"clamped"`` (tests and comparisons only). ``prestress`` is B2-R4.
    ``loss_factor`` is eta as a function of frequency in Hz (default the constant
    ``plate.lossFactor``). ``grid_order`` 0 omits the radiation grid. Raises ``ValueError`` naming
    the field for invalid input.
    """
    t0 = time.perf_counter()
    geom = geometry.from_plate(plate, f_max)
    outline = geom.outline
    tol = 1.0e-9 * float(max(np.ptp(outline[:, 0]), np.ptp(outline[:, 1])))
    xy = np.empty((len(bridge_points), 2))
    pts3 = np.empty((len(bridge_points), 3))
    for i, bp in enumerate(bridge_points):
        if not all(math.isfinite(c) for c in (bp.x, bp.y, bp.z)):
            raise ValueError(f"bridgePoints[{i}]: coordinates must be finite")
        if not geometry.point_in_polygon((bp.x, bp.y), outline, tol):
            raise ValueError(f"bridgePoints[{i}]: position lies outside plate.outline")
        xy[i] = (bp.x, bp.y)
        pts3[i] = (bp.x, bp.y, bp.z)

    pm = mesh.mesh_plate(geom, mesh_size)
    system = fem.assemble(geom, pm, boundary, prestress)
    modes = eigen.solve_modes(system.stiffness, system.mass, geom.f_max, nev, dense_threshold)

    # B2-R6, SPEC-BODY: sigma_k = 1/2 eta(Omega_k) Omega_k (radiation damping is added by B3).
    freq = modes.omega / (2.0 * math.pi)
    if loss_factor is None:
        eta = np.full_like(modes.omega, geom.material.eta)
    else:
        eta = np.array([loss_factor(float(f)) for f in freq], dtype=np.float64)
        if not np.all(np.isfinite(eta)) or np.any(eta < 0.0):
            raise ValueError("loss_factor: eta(f) must be finite and >= 0")
    sigma = 0.5 * eta * modes.omega

    # B2-R5: Phi_k at the bridge points. phi[k, b] = BodyModes::phi[k * numPoints + b].
    if len(bridge_points):
        phi = np.ascontiguousarray((sample_matrix(system.basis, xy) @ modes.vectors).T)
    else:
        phi = np.zeros((modes.omega.size, 0))
    table = BodyModalTable(omega=modes.omega, sigma=sigma, phi=phi, points=pts3)

    if grid_order > 0:
        gbasis = Basis(system.mesh, system.basis.elem, intorder=grid_order)
        ev, gxy, gw = quadrature_matrix(gbasis)
        gpts = np.column_stack([gxy, np.zeros(len(gxy))])
        table = BodyModalTable(
            omega=table.omega,
            sigma=table.sigma,
            phi=table.phi,
            points=table.points,
            grid_points=gpts,
            grid_weights=gw,
            grid_phi=np.ascontiguousarray((ev @ modes.vectors).T),
        )
    info = CompileInfo(
        n_modes=int(modes.omega.size),
        n_rigid_dropped=modes.n_rigid,
        n_dofs=int(system.stiffness.shape[0]),
        n_elements=int(pm.t.shape[1]),
        mesh_size=pm.mesh_size,
        windows=modes.windows,
        boundary=boundary,
        seconds=time.perf_counter() - t0,
    )
    return Compiled(table=table, info=info, modes=modes, system=system, geometry=geom)
