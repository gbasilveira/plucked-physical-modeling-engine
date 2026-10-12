# SPDX-License-Identifier: MIT
"""Orthotropic Kirchhoff plate in scikit-fem: stiffness, mass, beams, rim, prestress (B2-R3, B2-R4).

Element: ``ElementTriArgyris`` (C1, quintic, 21 degrees of freedom; ADR-0002). Ribs and bridges are
Euler-Bernoulli beams integrated along mesh edges that follow their axes ("beam elements attached
along their axes"); the beam adds E I (w_ss)^2 / 2 and rho A w^2 / 2 per unit axis length, where s
is the axis coordinate. The plate and beam share the Argyris degrees of freedom, so the coupling is
exact for out-of-plate bending.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np
import scipy.sparse as sp
from numpy.typing import NDArray
from skfem import Basis, BilinearForm, ElementTriArgyris, FacetBasis, MeshTri

from pme_compiler.body.geometry import BOUNDARY_CONDITIONS, Geometry
from pme_compiler.body.mesh import PlateMesh

Array = NDArray[np.float64]

# Dimensionless penalty factors for "simply_supported" and "clamped" (tests and comparisons only).
# The penalty stiffness is PENALTY * D_max / Lc^3 (translational) and PENALTY * D_max / Lc
# (rotational), Lc = sqrt(area); the relative frequency error of the constraint is ~ 1 / PENALTY.
PENALTY = 1.0e8


@dataclass(frozen=True)
class Prestress:
    """Uniform in-plane resultants N_x, N_y, N_xy (N/m) in the body frame (B2-R4). Tension > 0."""

    nx: float = 0.0
    ny: float = 0.0
    nxy: float = 0.0


@dataclass(frozen=True)
class System:
    """Assembled matrices and the discretisation they live on."""

    stiffness: sp.csc_matrix
    mass: sp.csc_matrix
    basis: Basis
    mesh: MeshTri
    plate_mass: sp.csc_matrix
    beam_stiffness: sp.csc_matrix
    beam_mass: sp.csc_matrix


def _facets_of_edges(mesh: MeshTri, edges: NDArray[np.int64]) -> NDArray[np.int64]:
    """Indices of the mesh facets whose node pairs are the columns of ``edges``."""
    n = np.int64(mesh.p.shape[1])
    f = np.sort(mesh.facets, axis=0)
    keys = f[0] * n + f[1]
    order = np.argsort(keys)
    e = np.sort(edges, axis=0)
    query = e[0] * n + e[1]
    pos = np.searchsorted(keys[order], query)
    pos = np.minimum(pos, len(keys) - 1)
    found = keys[order][pos] == query
    if not np.all(found):
        raise RuntimeError("a beam edge is not an edge of the triangle mesh")
    return np.asarray(order[pos], dtype=np.int64)


def assemble(
    geom: Geometry,
    pm: PlateMesh,
    boundary: str = "elastic",
    prestress: Prestress | None = None,
) -> System:
    """Assemble K and M for the plate with beams, rim support and optional prestress.

    # SPEC-BODY Kirchhoff form in the grain frame: energy density
    #   1/2 [D11 w_LL^2 + 2 D12 w_LL w_RR + D22 w_RR^2 + 4 D66 w_LR^2],
    # kinetic density 1/2 rho h w_t^2; L = (cos t, sin t), R = (-sin t, cos t), t = grain angle.
    """
    if boundary not in BOUNDARY_CONDITIONS:
        raise ValueError(f"boundary: expected one of {BOUNDARY_CONDITIONS}, got {boundary!r}")
    mesh = MeshTri(pm.p, pm.t)
    elem = ElementTriArgyris()
    basis = Basis(mesh, elem)
    mat = geom.material
    c, s = math.cos(geom.grain_angle), math.sin(geom.grain_angle)
    hmap = geom.thickness

    def _grain(hess: Array) -> tuple[Array, Array, Array]:
        # second derivatives along L and R (rotation of the Hessian into the grain frame)
        w_ll = c * c * hess[0][0] + 2.0 * c * s * hess[0][1] + s * s * hess[1][1]
        w_rr = s * s * hess[0][0] - 2.0 * c * s * hess[0][1] + c * c * hess[1][1]
        w_lr = c * s * (hess[1][1] - hess[0][0]) + (c * c - s * s) * hess[0][1]
        return w_ll, w_rr, w_lr

    @BilinearForm
    def plate_k(u, v, w):  # type: ignore[no-untyped-def]
        h = hmap.at(w.x[0], w.x[1])
        d11, d22, d12, d66 = mat.d_coefficients(h)
        ull, urr, ulr = _grain(u.hess)
        vll, vrr, vlr = _grain(v.hess)
        return (
            d11 * ull * vll
            + d12 * (ull * vrr + urr * vll)
            + d22 * urr * vrr
            + 4.0 * d66 * ulr * vlr
        )

    @BilinearForm
    def plate_m(u, v, w):  # type: ignore[no-untyped-def]
        return mat.rho * hmap.at(w.x[0], w.x[1]) * u * v

    k = plate_k.assemble(basis).tocsc()
    m_plate = plate_m.assemble(basis).tocsc()
    n = k.shape[0]
    k_beam = sp.csc_matrix((n, n))
    m_beam = sp.csc_matrix((n, n))

    # Ribs and bridges: Euler-Bernoulli beams on the mesh edges along their axes.
    # SPEC-BODY "ribs and bridges as attached beams": E I (w_ss)^2 and rho A w^2 per unit length.
    for beam, edges in zip(geom.beams, pm.beam_edges, strict=True):
        fidx = _facets_of_edges(mesh, edges)
        fb = FacetBasis(mesh, elem, facets=fidx)
        tx = (beam.p1[0] - beam.p0[0]) / beam.length
        ty = (beam.p1[1] - beam.p0[1]) / beam.length
        ei = beam.bending_stiffness
        rho_a = beam.mass_per_length

        def _wss(hess: Array, tx: float = tx, ty: float = ty) -> Array:  # SPEC-BODY beam w_ss
            return np.asarray(
                tx * tx * hess[0][0] + 2.0 * tx * ty * hess[0][1] + ty * ty * hess[1][1]
            )

        @BilinearForm
        def beam_k(u, v, w, ei=ei, _wss=_wss):  # type: ignore[no-untyped-def]
            return ei * _wss(u.hess) * _wss(v.hess)

        @BilinearForm
        def beam_m(u, v, w, rho_a=rho_a):  # type: ignore[no-untyped-def]
            return rho_a * u * v

        k_beam = k_beam + beam_k.assemble(fb).tocsc()
        m_beam = m_beam + beam_m.assemble(fb).tocsc()
    k = k + k_beam

    # Rim support, SPEC-BODY "elastically supported rim": translational k_rim (N/m^2) and
    # rotational k_rim,theta (N m/rad per m) springs per unit rim length.
    bfacets = mesh.boundary_facets()
    if boundary == "elastic":
        kt, kr = geom.rim_stiffness, geom.rim_rotational_stiffness
    else:
        d_max = max(mat.E_L, mat.E_R) * hmap.max**3 / (12.0 * (1.0 - mat.nu_lr * mat.nu_rl))
        lc = math.sqrt(geom.area)
        kt = PENALTY * d_max / lc**3
        kr = PENALTY * d_max / lc if boundary == "clamped" else 0.0
    if bfacets.size and (kt > 0.0 or kr > 0.0):
        bb = FacetBasis(mesh, elem, facets=bfacets)

        @BilinearForm
        def rim(u, v, w, kt=kt, kr=kr):  # type: ignore[no-untyped-def]
            un = u.grad[0] * w.n[0] + u.grad[1] * w.n[1]
            vn = v.grad[0] * w.n[0] + v.grad[1] * w.n[1]
            return kt * u * v + kr * un * vn

        k = k + rim.assemble(bb).tocsc()

    # B2-R4: geometric stiffness from uniform in-plane resultants.
    if prestress is not None and (prestress.nx or prestress.ny or prestress.nxy):
        nx, ny, nxy = prestress.nx, prestress.ny, prestress.nxy

        @BilinearForm
        def geo(u, v, w):  # type: ignore[no-untyped-def]
            return (
                nx * u.grad[0] * v.grad[0]
                + nxy * (u.grad[0] * v.grad[1] + u.grad[1] * v.grad[0])
                + ny * u.grad[1] * v.grad[1]
            )

        k = k + geo.assemble(basis).tocsc()

    mass = (m_plate + m_beam).tocsc()
    return System(
        stiffness=((k + k.T) * 0.5).tocsc(),
        mass=mass,
        basis=basis,
        mesh=mesh,
        plate_mass=m_plate,
        beam_stiffness=k_beam,
        beam_mass=m_beam,
    )
