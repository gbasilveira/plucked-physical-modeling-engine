# SPDX-License-Identifier: MIT
"""Body modal-table blob, layout 1 (B2-R7; shared by B2, B3, B4).

One blob file per body: raw little-endian float64 arrays, row-major, each at a 64-byte-aligned
offset, listed in the instrument file's ``blobs`` manifest (docs/dev/instrument-format.md,
"Blob manifest"). Modes are sorted by ascending Omega. K modes, P bridge points, C channels and
G radiation-grid points. The array names, shapes and units are the layout table in
docs/dev/body-compiler.md. (SPEC-BODY "Modal data (offline output)", SPEC-INST "Instrument file")
"""

from __future__ import annotations

import dataclasses
import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from numpy.typing import NDArray

import pme_compiler
from pme_compiler.schema import models

Array = NDArray[np.float64]

ALIGNMENT = 64
DEFAULT_RELPATH = "blobs/body-modal-table.bin"

# (manifest name, attribute, unit) in file order.
_LAYOUT: tuple[tuple[str, str, str], ...] = (
    ("omega", "omega", "rad/s"),
    ("sigma", "sigma", "1/s"),
    ("phi", "phi", "kg^(-1/2)"),
    ("points", "points", "m"),
    ("gridPoints", "grid_points", "m"),
    ("gridWeights", "grid_weights", "m^2"),
    ("gridPhi", "grid_phi", "kg^(-1/2)"),
    ("sigmaRad", "sigma_rad", "1/s"),
    ("alpha", "alpha", "kg^(1/2)/m^2"),
    ("beta", "beta", "kg^(1/2)/m^2"),
    ("channelDelay", "channel_delay", "s"),
    ("residualMobility", "residual_mobility", "s/kg"),
)

_GROUPS: tuple[tuple[str, ...], ...] = (
    ("grid_points", "grid_weights", "grid_phi"),
    ("alpha", "beta", "channel_delay"),
)


@dataclass(frozen=True)
class BodyModalTable:
    """Mass-normalised body modes (float64 arrays; optional ones may be ``None``)."""

    omega: Array  # [K] rad/s
    sigma: Array  # [K] 1/s
    phi: Array  # [K, P] kg^(-1/2); phi[k, b] = BodyModes::phi[k * numPoints + b]
    points: Array  # [P, 3] m
    grid_points: Array | None = None  # [G, 3] m
    grid_weights: Array | None = None  # [G] m^2
    grid_phi: Array | None = None  # [K, G] kg^(-1/2)
    sigma_rad: Array | None = None  # [K] 1/s
    alpha: Array | None = None  # [C, K] kg^(1/2)/m^2
    beta: Array | None = None  # [C, K]
    channel_delay: Array | None = None  # [C] s
    residual_mobility: Array | None = None  # [P] s/kg


def _expected_shapes(t: BodyModalTable) -> dict[str, tuple[int, ...]]:
    k = int(t.omega.shape[0]) if t.omega.ndim == 1 else -1
    p = int(t.points.shape[0]) if t.points.ndim == 2 else -1
    g = int(t.grid_points.shape[0]) if t.grid_points is not None and t.grid_points.ndim == 2 else -1
    c = int(t.alpha.shape[0]) if t.alpha is not None and t.alpha.ndim == 2 else -1
    return {
        "omega": (k,),
        "sigma": (k,),
        "phi": (k, p),
        "points": (p, 3),
        "grid_points": (g, 3),
        "grid_weights": (g,),
        "grid_phi": (k, g),
        "sigma_rad": (k,),
        "alpha": (c, k),
        "beta": (c, k),
        "channel_delay": (c,),
        "residual_mobility": (p,),
    }


def validate(table: BodyModalTable) -> list[str]:
    """Problems of ``table``; the empty list means valid.

    Checks shapes, dtype float64, finiteness, ascending omega >= 0, sigma >= 0 and that the
    optional groups are complete. (SPEC-BODY "Modal data (offline output)")
    """
    problems: list[str] = []
    shapes = _expected_shapes(table)
    for _name, attr, _unit in _LAYOUT:
        arr = getattr(table, attr)
        if arr is None:
            continue
        if not isinstance(arr, np.ndarray):
            problems.append(f"{attr}: expected a numpy array")
            continue
        if arr.dtype != np.float64:
            problems.append(f"{attr}: expected dtype float64, got {arr.dtype}")
        if arr.shape != shapes[attr]:
            problems.append(f"{attr}: expected shape {shapes[attr]}, got {arr.shape}")
        if not np.all(np.isfinite(arr)):
            problems.append(f"{attr}: contains NaN or infinity")
    for group in _GROUPS:
        present = [getattr(table, a) is not None for a in group]
        if any(present) and not all(present):
            problems.append(f"{', '.join(group)}: must appear together or not at all")
    if problems:
        return problems
    if table.omega.size and (np.any(table.omega < 0.0) or np.any(np.diff(table.omega) < 0.0)):
        problems.append("omega: must be >= 0 and sorted ascending")
    if np.any(table.sigma < 0.0):
        problems.append("sigma: must be >= 0")
    if table.sigma_rad is not None and np.any(table.sigma_rad < 0.0):
        problems.append("sigma_rad: must be >= 0")
    if table.grid_weights is not None and np.any(table.grid_weights < 0.0):
        problems.append("grid_weights: must be >= 0")
    return problems


def _align(n: int) -> int:
    return (n + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT


def write(table: BodyModalTable, directory: Path, relpath: str, tool: str) -> models.BlobEntry:
    """Write the blob to ``directory / relpath`` and return its manifest entry.

    ``tool`` is ``"pme_compiler.body"`` or ``"pme_compiler.importers"``. Raises ``ValueError``
    listing the problems if the table is invalid.
    """
    problems = validate(table)
    if problems:
        raise ValueError("invalid body modal table: " + "; ".join(problems))
    buf = bytearray()
    arrays: list[models.BlobArray] = []
    for name, attr, unit in _LAYOUT:
        arr = getattr(table, attr)
        if arr is None:
            continue
        buf.extend(b"\x00" * (_align(len(buf)) - len(buf)))
        arrays.append(
            models.BlobArray(
                name=name,
                dtype="float64",
                shape=[int(s) for s in arr.shape],
                unit=unit,
                offset=len(buf),
            )
        )
        buf.extend(np.ascontiguousarray(arr, dtype="<f8").tobytes())
    data = bytes(buf)
    path = Path(directory) / relpath
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return models.BlobEntry(
        hash="sha256:" + hashlib.sha256(data).hexdigest(),
        path=relpath,
        producer=models.BlobProducer(tool=tool, version=pme_compiler.__version__),
        arrays=arrays,
    )


def read(entry: models.BlobEntry, directory: Path) -> BodyModalTable:
    """Read the blob named by ``entry`` (relative to ``directory``); bit-identical to the write."""
    data = (Path(directory) / entry.path).read_bytes()
    by_name = {a.name: a for a in entry.arrays}
    values: dict[str, Any] = {}
    for name, attr, _unit in _LAYOUT:
        a = by_name.get(name)
        if a is None:
            continue
        if a.dtype != "float64":
            raise ValueError(f"array {name}: expected float64, got {a.dtype}")
        count = int(np.prod(a.shape))
        values[attr] = (
            np.frombuffer(data, dtype="<f8", count=count, offset=a.offset)
            .astype(np.float64)
            .reshape(a.shape)
        )
    for required in ("omega", "sigma", "phi", "points"):
        if required not in values:
            raise ValueError(f"blob {entry.path} lacks the required array {required}")
    return BodyModalTable(**values)


def attach(
    file: models.InstrumentFile, entry: models.BlobEntry, source: str
) -> models.InstrumentFile:
    """Copy of ``file`` with ``entry`` in ``blobs`` and the body pointing at it.

    ``source`` is ``"ModalTable"`` or ``"MeasuredMobilityFit"``; the plate block is kept as
    provenance. An existing entry with the same path is replaced.
    """
    if source not in ("ModalTable", "MeasuredMobilityFit"):
        raise ValueError(f"source: expected ModalTable or MeasuredMobilityFit, got {source!r}")
    body = dataclasses.replace(
        file.desc.body,
        source=source,  # type: ignore[arg-type]
        modalTableHash=entry.hash,
    )
    desc = dataclasses.replace(file.desc, body=body)
    blobs = [b for b in file.blobs if b.path != entry.path] + [entry]
    return dataclasses.replace(file, desc=desc, blobs=blobs)
