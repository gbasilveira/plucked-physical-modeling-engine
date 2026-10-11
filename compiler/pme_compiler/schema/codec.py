# SPDX-License-Identifier: MIT
"""Conversion between JSON trees and the models, driven by the field table (B1-R4)."""

from __future__ import annotations

from typing import Any

from pme_compiler.schema import models
from pme_compiler.schema.fields import (
    BY_NAME,
    CURRENT_VERSION,
    EXCITER_ALTERNATIVES,
    FORMAT_NAME,
    Field,
    Struct,
)

_KEY_OF = {ref: key for key, ref in EXCITER_ALTERNATIVES}
_REF_OF = dict(EXCITER_ALTERNATIVES)


def _value_to_json(f: Field, value: Any) -> Any:
    kind = f.kind
    if kind == "num":
        return float(value)
    if kind == "int":
        return int(value)
    if kind == "numarray":
        return [float(v) for v in value]
    if kind == "intarray":
        return [int(v) for v in value]
    if kind in ("obj", "optobj"):
        return struct_to_json(f.ref, value)
    if kind == "objarray":
        return [struct_to_json(f.ref, v) for v in value]
    return value


def struct_to_json(name: str, obj: Any) -> dict[str, Any]:
    """The ordered JSON tree of a model (keys in schema ``properties`` order)."""
    struct: Struct = BY_NAME[name]
    if struct.extra.get("variant"):
        alt = obj.e
        cls = type(alt).__name__
        return {_KEY_OF[cls]: struct_to_json(cls, alt)}
    out: dict[str, Any] = {}
    for f in struct.fields:
        value = getattr(obj, f.name)
        if f.kind == "optobj" and value is None:
            continue
        out[f.name] = _value_to_json(f, value)
    return out


def _value_from_json(f: Field, value: Any) -> Any:
    kind = f.kind
    if kind == "num":
        return float(value)
    if kind == "int":
        return int(value)
    if kind == "numarray":
        return [float(v) for v in value]
    if kind == "intarray":
        return [int(v) for v in value]
    if kind in ("obj", "optobj"):
        return struct_from_json(f.ref, value)
    if kind == "objarray":
        return [struct_from_json(f.ref, v) for v in value]
    return value


def struct_from_json(name: str, data: dict[str, Any]) -> Any:
    """Build a model from a JSON tree that has passed schema validation."""
    struct = BY_NAME[name]
    cls = getattr(models, name)
    if struct.extra.get("variant"):
        ((key, alt),) = data.items()
        return cls(e=struct_from_json(_REF_OF[key], alt))
    kwargs = {f.name: _value_from_json(f, data[f.name]) for f in struct.fields if f.name in data}
    return cls(**kwargs)


def file_to_json(file: models.InstrumentFile) -> dict[str, Any]:
    """The ordered JSON tree of a whole instrument file at the current version."""
    return {
        "format": FORMAT_NAME,
        "schemaVersion": CURRENT_VERSION,
        "instrument": struct_to_json("InstrumentDesc", file.desc),
        "blobs": [struct_to_json("BlobEntry", b) for b in file.blobs],
    }


def file_from_json(doc: dict[str, Any], source_version: int) -> models.InstrumentFile:
    """Build the file model from a validated current-version tree."""
    return models.InstrumentFile(
        desc=struct_from_json("InstrumentDesc", doc["instrument"]),
        blobs=[struct_from_json("BlobEntry", b) for b in doc.get("blobs", [])],
        source_version=source_version,
    )
