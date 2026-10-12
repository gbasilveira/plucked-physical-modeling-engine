# SPDX-License-Identifier: MIT
"""Shared helpers of the body-data import routes (B4).

Every route returns a ``BodyModalTable`` (layout 1, ``pme_compiler.body.modal_blob``). The helpers
here read the instrument-side geometry the table must match and write instrument file and blob.
(SPEC-BODY "Modal data (offline output)"; SPEC-INST "Instrument file")
"""

from __future__ import annotations

import dataclasses
from pathlib import Path

import numpy as np
from numpy.typing import NDArray

from pme_compiler.body import modal_blob
from pme_compiler.body.modal_blob import BodyModalTable
from pme_compiler.schema import dump, load, models

DEFAULT_BLOB_PATH = "blobs/body-modal-table.bin"
TOOL = "pme_compiler.importers"

__all__ = [
    "DEFAULT_BLOB_PATH",
    "TOOL",
    "BodyModalTable",
    "ImportDataError",
    "bridge_positions",
    "finish",
    "load_base",
    "sigma_from_loss",
    "sigma_from_q",
    "sorted_table",
]


class ImportDataError(ValueError):
    """Input data rejected by an import route. The message names the offending row and column."""


def bridge_positions(desc: models.InstrumentDesc) -> NDArray[np.float64]:
    """Body-frame bridge-point positions, shape [P, 3], in the instrument's order."""
    return np.array(
        [[b.position.x, b.position.y, b.position.z] for b in desc.bridgePoints], dtype=np.float64
    )


def sigma_from_loss(
    omega: NDArray[np.float64], eta: float | NDArray[np.float64]
) -> NDArray[np.float64]:
    """sigma_k = eta Omega_k / 2 (SPEC-BODY, loss factor form)."""
    return np.asarray(0.5 * np.asarray(eta) * omega, dtype=np.float64)


def sigma_from_q(omega: NDArray[np.float64], q: float | NDArray[np.float64]) -> NDArray[np.float64]:
    """sigma_k = Omega_k / (2 Q_k) (quality factor Q = Omega / (2 sigma))."""
    return np.asarray(omega / (2.0 * np.asarray(q)), dtype=np.float64)


def sorted_table(table: BodyModalTable) -> BodyModalTable:
    """The table with modes sorted by ascending Omega (stable), per-mode arrays permuted."""
    order = np.argsort(table.omega, kind="stable")
    if np.array_equal(order, np.arange(order.size)):
        return table

    def pick(a: NDArray[np.float64] | None, axis: int) -> NDArray[np.float64] | None:
        return None if a is None else np.take(a, order, axis=axis)

    return dataclasses.replace(
        table,
        omega=table.omega[order],
        sigma=table.sigma[order],
        phi=table.phi[order],
        grid_phi=pick(table.grid_phi, 0),
        sigma_rad=pick(table.sigma_rad, 0),
        alpha=pick(table.alpha, 1),
        beta=pick(table.beta, 1),
    )


def finish(
    base: models.InstrumentFile,
    table: BodyModalTable,
    source: str,
    out_path: Path,
    blob_path: str = DEFAULT_BLOB_PATH,
) -> models.InstrumentFile:
    """Validate ``table``, write blob and instrument file next to ``out_path`` and return the file.

    A modal table already attached to ``base`` (same ``modalTableHash``) is replaced.
    """
    errors = modal_blob.validate(table)
    if errors:
        raise ImportDataError("invalid modal table: " + "; ".join(errors))
    entry = modal_blob.write(table, out_path.parent, blob_path, TOOL)
    old = base.desc.body.modalTableHash
    kept = [b for b in base.blobs if b.hash != old and b.path != blob_path]
    stripped = dataclasses.replace(base, blobs=kept)
    result = modal_blob.attach(stripped, entry, source)
    dump(result, out_path)
    return result


def load_base(path: Path) -> models.InstrumentFile:
    """Load the instrument file the table is attached to (blob files are not required)."""
    return load(path, verify_blobs=False)
