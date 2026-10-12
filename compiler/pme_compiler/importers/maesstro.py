# SPDX-License-Identifier: MIT
"""Adapter for soundboard modal bases in the layout used for MAESSTRO output (B4-R1). (SPEC-BODY)

STATUS: PARTIAL. MAESSTRO 1.0.0 (Boutillon et al., HAL hal-03409789; user guides at
maesstro.cnrs.fr, section "Structure du fichier de sortie et donnees normalisees") is Matlab-based
(maesstro-matlab computes the soundboard modal basis on a Gmsh mesh). Its output-file specification
could not be retrieved in the implementation environment (the host was unreachable), so the exact
variable names are NOT verified. This adapter therefore reads a documented, closest-description
layout (below) from a MATLAB v5 ``.mat`` or NumPy ``.npz`` file, with a ``names`` mapping so the
real names can be substituted without code changes once a genuine sample is available. The
MAESSTRO licence is also unverified (to be recorded for H1).

Layout (one file; names are the defaults of ``DEFAULT_NAMES``):

- ``frequencies`` [K]  modal frequencies, Hz
- ``loss_factor`` [K]  modal loss factors eta (sigma_k = eta Omega_k / 2), or ``sigma`` [K] in 1/s
- ``nodes`` [N, 2|3]   mesh node positions, m (soundboard frame; only x and y are used)
- ``modes`` [N, K]     nodal values of the modal displacement shapes (column k = mode k)
- ``nodal_mass`` [N]   lumped mass of each node, kg (optional if the shapes are mass-normalised)
- ``normalisation``    text ``mass`` when sum_i m_i Phi_k(i)^2 = 1 already holds, anything else
                       (e.g. ``unit``) when the shapes carry an arbitrary scale

Shapes are linearly interpolated (Delaunay triangulation of the nodes in x, y) at the instrument's
bridge points; a point outside the mesh is an error. Mass normalisation (SPEC-BODY: integral rho h
Phi_k Phi_l dA = delta_kl, discretised with the lumped masses) is checked when declared and
performed otherwise.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any

import numpy as np
from numpy.typing import NDArray
from scipy.interpolate import LinearNDInterpolator
from scipy.io import loadmat

from pme_compiler.importers._common import (
    BodyModalTable,
    ImportDataError,
    sigma_from_loss,
    sorted_table,
)

F64 = NDArray[np.float64]

DEFAULT_NAMES: dict[str, str] = {
    "frequencies": "frequencies",
    "loss_factor": "loss_factor",
    "sigma": "sigma",
    "nodes": "nodes",
    "modes": "modes",
    "nodal_mass": "nodal_mass",
    "normalisation": "normalisation",
}

MASS_TOLERANCE = 1e-6


def _load(path: Path) -> dict[str, Any]:
    if path.suffix == ".npz":
        with np.load(path, allow_pickle=False) as z:
            return {k: z[k] for k in z.files}
    if path.suffix == ".mat":
        return {k: v for k, v in loadmat(path).items() if not k.startswith("__")}
    raise ImportDataError(f"{path.name}: unsupported extension (use .mat or .npz)")


def _text(v: Any) -> str:
    return str(np.asarray(v).reshape(-1)[0]).strip()


def read_maesstro(path: Path, points: F64, names: dict[str, str] | None = None) -> BodyModalTable:
    """Read the modal basis in ``path`` and return the table at ``points`` [P, 3]."""
    nm = {**DEFAULT_NAMES, **(names or {})}
    data = _load(path)

    def need(key: str) -> Any:
        if nm[key] not in data:
            raise ImportDataError(f"{path.name}: variable {nm[key]!r} ({key}) is missing")
        return data[nm[key]]

    freq = np.asarray(need("frequencies"), dtype=np.float64).reshape(-1)
    nodes = np.atleast_2d(np.asarray(need("nodes"), dtype=np.float64))
    modes = np.asarray(need("modes"), dtype=np.float64)
    k = freq.size
    if nodes.shape[1] not in (2, 3):
        raise ImportDataError(f"{path.name}: nodes must be [N, 2] or [N, 3]")
    if modes.shape != (nodes.shape[0], k):
        raise ImportDataError(
            f"{path.name}: modes must be [N={nodes.shape[0]}, K={k}], got {list(modes.shape)}"
        )
    if not (np.all(np.isfinite(freq)) and np.all(np.isfinite(modes)) and np.all(freq > 0)):
        raise ImportDataError(f"{path.name}: non-finite or non-positive values")
    omega = 2.0 * math.pi * freq

    if nm["loss_factor"] in data:
        eta = np.asarray(data[nm["loss_factor"]], dtype=np.float64).reshape(-1)
        if eta.size != k or np.any(eta < 0) or not np.all(np.isfinite(eta)):
            raise ImportDataError(f"{path.name}: loss_factor must be {k} finite values >= 0")
        sigma = sigma_from_loss(omega, eta)
    elif nm["sigma"] in data:
        sigma = np.asarray(data[nm["sigma"]], dtype=np.float64).reshape(-1)
        if sigma.size != k or np.any(sigma < 0) or not np.all(np.isfinite(sigma)):
            raise ImportDataError(f"{path.name}: sigma must be {k} finite values >= 0")
    else:
        raise ImportDataError(
            f"{path.name}: neither {nm['loss_factor']!r} nor {nm['sigma']!r} found"
        )

    declared_mass = nm["normalisation"] in data and _text(data[nm["normalisation"]]) == "mass"
    scale = np.ones(k)
    if nm["nodal_mass"] in data:
        mass = np.asarray(data[nm["nodal_mass"]], dtype=np.float64).reshape(-1)
        if mass.size != nodes.shape[0] or np.any(mass <= 0):
            raise ImportDataError(f"{path.name}: nodal_mass must be N positive values")
        norm2 = mass @ (modes**2)  # sum_i m_i Phi_k(i)^2, discretised mass normalisation
        if declared_mass:
            worst = float(np.max(np.abs(norm2 - 1.0)))
            if worst > MASS_TOLERANCE:
                raise ImportDataError(
                    f"{path.name}: shapes declared mass-normalised "
                    f"but |sum m Phi^2 - 1| = {worst:.3g}"
                )
        else:
            if np.any(norm2 <= 0):
                raise ImportDataError(f"{path.name}: a mode has zero mass norm")
            scale = 1.0 / np.sqrt(norm2)
    elif not declared_mass:
        raise ImportDataError(
            f"{path.name}: shapes are not declared mass-normalised and no nodal_mass is given"
        )

    pts = np.asarray(points, dtype=np.float64)
    interp = LinearNDInterpolator(nodes[:, :2], modes * scale)
    phi_kp = np.asarray(interp(pts[:, :2]), dtype=np.float64)  # [P, K]
    if phi_kp.ndim == 1:
        phi_kp = phi_kp[:, None]
    bad = np.where(~np.all(np.isfinite(phi_kp), axis=1))[0]
    if bad.size:
        b = int(bad[0])
        raise ImportDataError(
            f"bridge point {b} at ({pts[b, 0]:g}, {pts[b, 1]:g}) "
            f"lies outside the mesh of {path.name}"
        )
    return sorted_table(
        BodyModalTable(omega=omega, sigma=sigma, phi=phi_kp.T.copy(), points=pts.copy())
    )
