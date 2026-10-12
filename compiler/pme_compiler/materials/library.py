# SPDX-License-Identifier: MIT
"""Tonewood material library (B2-R2).

Data in ``library.json``; every entry records its source (author, title, year, locator, licence and
how each value was derived). The loader checks that and fills ``PlateParams`` fields. The instrument
file remains the single source of truth for a compiled body. (SPEC-BODY "Plate model (offline)",
SI-0013 moisture law with MC_ref = 0.12)
"""

from __future__ import annotations

import dataclasses
import json
import math
from dataclasses import dataclass
from importlib import resources
from typing import Any

from pme_compiler.body.geometry import MC_REF
from pme_compiler.schema import models

CONSTANT_FIELDS = (
    "youngsL",
    "youngsR",
    "shearLR",
    "poissonLR",
    "poissonRL",
    "density",
    "lossFactor",
)
SLOPE_FIELDS = ("dEdMcL", "dEdMcR", "dGdMc", "dRhodMc")
SOURCE_FIELDS = ("author", "title", "year", "locator", "licence", "derivation")


@dataclass(frozen=True)
class Material:
    """One library entry."""

    name: str
    kind: str  # "tonewood" or "test-reference"
    description: str
    constants: dict[str, float]
    slopes: dict[str, float]  # per unit MC; zero when the entry gives none
    loss_table: tuple[tuple[float, float], ...]  # (frequency Hz, eta); empty = constant eta
    source: dict[str, Any]

    def loss_factor(self, frequency_hz: float) -> float:
        """eta(f): the constant, or the table interpolated linearly in log f (clamped)."""
        if not self.loss_table:
            return self.constants["lossFactor"]
        f0, e0 = self.loss_table[0]
        if frequency_hz <= f0:
            return e0
        for (fa, ea), (fb, eb) in zip(self.loss_table, self.loss_table[1:], strict=False):
            if frequency_hz <= fb:
                u = math.log(frequency_hz / fa) / math.log(fb / fa)
                return ea + u * (eb - ea)
        return self.loss_table[-1][1]


def validate_entry(entry: dict[str, Any]) -> list[str]:
    """Problems of a raw library entry (empty = valid): fields, positivity and a complete source."""
    problems: list[str] = []
    name = entry.get("name", "<unnamed>")
    if not entry.get("name"):
        problems.append("entry without a name")
    consts = entry.get("constants", {})
    for f in CONSTANT_FIELDS:
        v = consts.get(f)
        if not isinstance(v, (int, float)) or not math.isfinite(v):
            problems.append(f"{name}: constants.{f} missing or not finite")
        elif f != "lossFactor" and not f.startswith("poisson") and v <= 0:
            problems.append(f"{name}: constants.{f} must be > 0")
    slopes = entry.get("moistureSlopes")
    if slopes is not None:
        for f in SLOPE_FIELDS:
            if not isinstance(slopes.get(f), (int, float)):
                problems.append(f"{name}: moistureSlopes.{f} missing")
    src = entry.get("source")
    if not isinstance(src, dict):
        return [*problems, f"{name}: no source recorded"]
    for f in SOURCE_FIELDS:
        if not src.get(f):
            problems.append(f"{name}: source.{f} missing or empty")
    deriv = src.get("derivation", {})
    needed = [*CONSTANT_FIELDS, "moistureSlopes"]
    for f in needed:
        if not isinstance(deriv, dict) or not deriv.get(f):
            problems.append(f"{name}: source.derivation.{f} missing (how was the value obtained?)")
    return problems


def _parse(entry: dict[str, Any]) -> Material:
    slopes_raw = entry.get("moistureSlopes") or {}
    return Material(
        name=entry["name"],
        kind=entry["kind"],
        description=entry.get("description", ""),
        constants={f: float(entry["constants"][f]) for f in CONSTANT_FIELDS},
        slopes={f: float(slopes_raw.get(f, 0.0)) for f in SLOPE_FIELDS},
        loss_table=tuple((float(f), float(e)) for f, e in (entry.get("lossFactorTable") or [])),
        source=entry["source"],
    )


def load_raw() -> dict[str, Any]:
    """The library document as parsed JSON."""
    text = resources.files("pme_compiler.materials").joinpath("library.json").read_text("utf-8")
    doc = json.loads(text)
    assert isinstance(doc, dict)
    return doc


def load_library() -> dict[str, Material]:
    """All entries by name. Raises ``ValueError`` listing problems if any entry is invalid."""
    doc = load_raw()
    if doc.get("mcRef") != MC_REF:
        raise ValueError(f"library mcRef {doc.get('mcRef')} differs from the compiler's {MC_REF}")
    problems: list[str] = []
    for e in doc["materials"]:
        problems += validate_entry(e)
    if problems:
        raise ValueError("invalid material library: " + "; ".join(problems))
    return {e["name"]: _parse(e) for e in doc["materials"]}


def omissions() -> list[str]:
    """Species and fields the library lacks, with the reason."""
    return [str(s) for s in load_raw().get("omissions", [])]


def apply(material: Material, plate: models.PlateParams) -> models.PlateParams:
    """Copy of ``plate`` with the wood fields (constants, slopes) of ``material``.

    Geometry, moisture, rim support and fMax of ``plate`` are kept. ``lossFactor`` is the entry's
    constant value (for an eta(f) table pass ``material.loss_factor`` to ``compile_plate``).
    """
    return dataclasses.replace(
        plate,
        youngsL=material.constants["youngsL"],
        youngsR=material.constants["youngsR"],
        shearLR=material.constants["shearLR"],
        poissonLR=material.constants["poissonLR"],
        poissonRL=material.constants["poissonRL"],
        density=material.constants["density"],
        lossFactor=material.constants["lossFactor"],
        dEdMcL=material.slopes["dEdMcL"],
        dEdMcR=material.slopes["dEdMcR"],
        dGdMc=material.slopes["dGdMc"],
        dRhodMc=material.slopes["dRhodMc"],
    )
