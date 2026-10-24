# SPDX-License-Identifier: MIT
"""Migration of older instrument file versions to the current one (B1-R2).

Version 0 mirrors the interfaces-v1 structs (2daee85); it never shipped and exists so that the
migration path is exercised from the first release. The steps are listed in
docs/dev/instrument-format.md ("Versions and migrations"); the C++ loader
(``engine/src/runtime/loader/migrate.cpp``) implements the same steps.
"""

from __future__ import annotations

from typing import Any

from pme_compiler.schema.errors import LoadError, pointer
from pme_compiler.schema.fields import CURRENT_VERSION


def _rename(obj: dict[str, Any], old: str, new: str) -> None:
    """Rename ``old`` to ``new`` unless ``new`` is already present."""
    if old in obj and new not in obj:
        obj[new] = obj.pop(old)


def _require_sources(
    obj: dict[str, Any], path: str, what: str, keys: tuple[str, ...], errors: list[LoadError]
) -> bool:
    """Report ``migration`` at ``path`` when a member that has no version 0 source is missing."""
    missing = [k for k in keys if k not in obj]
    if not missing:
        return True
    errors.append(
        LoadError(
            path,
            "migration",
            f"no version 0 source for {', '.join(missing)} of a {what}; supply them",
        )
    )
    return False


def _migrate_exciter(exciter: dict[str, Any], path: str, errors: list[LoadError]) -> None:
    for key, alt in exciter.items():
        if not isinstance(alt, dict):
            continue
        at = f"{path}{pointer(key)}"
        if key == "pluckPulse":
            _rename(alt, "peakForce", "peakForceMax")
            alt.setdefault("strengthGamma", 1)
        elif key == "plectrum":
            if _require_sources(alt, at, "plectrum", ("position", "angle"), errors):
                _rename(alt, "driverSpeed", "driverSpeedMax")
                alt.setdefault("strengthGamma", 1)
        elif key == "finger" and _require_sources(
            alt, at, "finger", ("driverStiffness", "position", "angle"), errors
        ):
            _rename(alt, "mass", "tipMass")
            _rename(alt, "stiffness", "contactK")
            _rename(alt, "alpha", "contactAlpha")
            _rename(alt, "driverSpeed", "driverSpeedMax")
            alt.setdefault("strengthGamma", 1)


def _migrate_0_to_1(doc: dict[str, Any], errors: list[LoadError]) -> None:
    inst = doc.get("instrument")
    if not isinstance(inst, dict):
        return
    groups = inst.get("groups")
    if isinstance(groups, list):
        for gi, group in enumerate(groups):
            if not isinstance(group, dict):
                continue
            exciter = group.get("exciter")
            if isinstance(exciter, dict):
                _migrate_exciter(exciter, f"/instrument/groups/{gi}/exciter", errors)
            segments = group.get("segments")
            if isinstance(segments, list):
                for segment in segments:
                    if isinstance(segment, dict):
                        segment.setdefault("unisonDetuneCents", 0)
    body = inst.get("body")
    plate = body.get("plate") if isinstance(body, dict) else None
    if isinstance(plate, dict):
        plate.setdefault("outline", [])
        plate.setdefault("thicknessNx", 0)
        plate.setdefault("thicknessNy", 0)
        plate.setdefault("thicknessGrid", [])
        plate.setdefault("bridges", [])
        plate.setdefault("rimRotationalStiffness", 0)


def migrate(doc: dict[str, Any], from_version: int) -> list[LoadError]:
    """Migrate ``doc`` in place from ``from_version`` to the current version.

    Returns the ``migration`` errors (empty on success). A document that is already current is
    left unchanged.
    """
    errors: list[LoadError] = []
    if from_version == 0:
        _migrate_0_to_1(doc, errors)
    if from_version < CURRENT_VERSION:
        doc["schemaVersion"] = CURRENT_VERSION
    return errors
