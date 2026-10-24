# SPDX-License-Identifier: MIT
"""Validation of instrument documents (B1-R1, B1-R3, B1-R5).

``schema_errors`` runs the JSON Schema (draft 2020-12) and maps its failures to the closed error
codes; ``cross_field_errors`` and the manifest checks implement the rules JSON Schema cannot
express. The C++ loader implements the same rules with the same paths and codes
(``engine/src/runtime/loader``).
"""

from __future__ import annotations

import copy
import hashlib
import json
from collections.abc import Iterator
from functools import lru_cache
from itertools import pairwise
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError

from pme_compiler.schema import models
from pme_compiler.schema.codec import file_from_json
from pme_compiler.schema.errors import LoadError, finalize, pointer
from pme_compiler.schema.fields import CURRENT_VERSION, DTYPE_SIZE
from pme_compiler.schema.migrate import migrate

SCHEMA_PATH = Path(__file__).with_name("instrument.schema.json")
SAFE_MAX = 2**53 - 1


@lru_cache(maxsize=1)
def schema() -> dict[str, Any]:
    """The instrument JSON Schema."""
    loaded: dict[str, Any] = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    return loaded


@lru_cache(maxsize=1)
def _validator() -> Draft202012Validator:
    return Draft202012Validator(schema())


def _map_error(err: ValidationError) -> Iterator[LoadError]:
    path = list(err.absolute_path)
    where = pointer(*path)
    kw = err.validator
    if kw == "type":
        expected = err.validator_value
        yield LoadError(where, "type", f"expected {expected}")
    elif kw == "required":
        instance = err.instance
        for name in err.validator_value:  # type: ignore[union-attr]
            if isinstance(instance, dict) and name not in instance:
                yield LoadError(pointer(*path, name), "missing", "required member is missing")
    elif kw == "additionalProperties":
        known = err.schema.get("properties", {}) if isinstance(err.schema, dict) else {}
        if isinstance(err.instance, dict):
            for key in err.instance:
                if key not in known:
                    yield LoadError(pointer(*path, key), "unknown_key", "unknown member")
    elif kw in ("minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum"):
        yield LoadError(where, "range", err.message)
    elif kw in ("minItems", "maxItems", "minProperties", "maxProperties"):
        yield LoadError(where, "structure", err.message)
    elif kw == "enum":
        yield LoadError(where, "enum", f"expected one of {err.validator_value}")
    elif kw == "pattern":
        yield LoadError(where, "type", f"expected a string matching {err.validator_value}")
    else:  # pragma: no cover - the schema uses no other keyword
        raise RuntimeError(f"unmapped JSON Schema keyword {kw!r}")


def schema_errors(document: object) -> list[LoadError]:
    """JSON Schema failures of a current-version document, as load errors (unsorted)."""
    out: list[LoadError] = []
    for err in _validator().iter_errors(document):
        out.extend(_map_error(err))
    return out


# --- cross-field rules ---------------------------------------------------------------------------


def _check_segment_order(group: models.NoteGroupDesc, group_path: str) -> list[LoadError]:
    """SI-0010 (orchestrator default): the tuning rule applies to speaking (AtL) segments only and
    a rear or duplex (At0) segment takes the tension of the speaking segment just before it, so a
    group's first segment must be a speaking segment. This is the only place the rule is checked."""
    if group.segments and group.segments[0].string.bridgeEnd == "At0":
        return [
            LoadError(
                f"{group_path}/segments/0/string/bridgeEnd",
                "structure",
                "the first segment of a group must be a speaking segment (AtL); a rear segment "
                "continues the speaking segment before it (SI-0010)",
            )
        ]
    return []


def _check_segment(seg: models.SegmentDesc, path: str, bridge_points: int) -> list[LoadError]:
    out: list[LoadError] = []
    if seg.bridgePoint >= bridge_points:
        out.append(
            LoadError(
                f"{path}/bridgePoint",
                "reference",
                f"expected an index below {bridge_points} (bridgePoints size)",
            )
        )
    if seg.string.dOuter < seg.string.dCore:
        out.append(
            LoadError(
                f"{path}/string/dOuter", "range", "expected an outer diameter of at least dCore"
            )
        )
    for k, b in enumerate(seg.barriers):
        if not len(b.x) == len(b.b) == len(b.w):
            out.append(
                LoadError(
                    f"{path}/barriers/{k}",
                    "structure",
                    "expected x, b and w to have the same length",
                )
            )
    return out


def _check_plate(body: models.BodyParams) -> list[LoadError]:
    p = body.plate
    path = "/instrument/body/plate"
    out: list[LoadError] = []
    if p.thicknessGrid:
        if p.thicknessNx < 2:
            out.append(
                LoadError(
                    f"{path}/thicknessNx",
                    "range",
                    "expected at least 2 grid points when thicknessGrid is given",
                )
            )
        if p.thicknessNy < 2:
            out.append(
                LoadError(
                    f"{path}/thicknessNy",
                    "range",
                    "expected at least 2 grid points when thicknessGrid is given",
                )
            )
        if (
            p.thicknessNx >= 2
            and p.thicknessNy >= 2
            and len(p.thicknessGrid) != p.thicknessNx * p.thicknessNy
        ):
            out.append(
                LoadError(
                    f"{path}/thicknessGrid",
                    "structure",
                    f"expected thicknessNx * thicknessNy = {p.thicknessNx * p.thicknessNy} values",
                )
            )
    if len(p.outline) in (1, 2):
        out.append(
            LoadError(
                f"{path}/outline", "structure", "expected no vertices (rectangle) or at least 3"
            )
        )
    if body.source != "PlateModel":
        return out

    def positive(value: float, key: str) -> None:
        if not value > 0.0:
            out.append(
                LoadError(
                    f"{path}/{key}",
                    "range",
                    "expected a value greater than 0 when source is PlateModel",
                )
            )

    positive(p.youngsL, "youngsL")
    positive(p.youngsR, "youngsR")
    positive(p.shearLR, "shearLR")
    positive(p.density, "density")
    positive(p.fMax, "fMax")
    if not p.thicknessGrid:
        positive(p.thickness, "thickness")
    if not p.outline:
        positive(p.lengthX, "lengthX")
        positive(p.lengthY, "lengthY")
    return out


def _check_modal_hash(file: models.InstrumentFile) -> list[LoadError]:
    body = file.desc.body
    path = "/instrument/body/modalTableHash"
    if body.source == "PlateModel":
        if body.modalTableHash:
            return [
                LoadError(path, "structure", "expected an empty hash when source is PlateModel")
            ]
        return []
    if not any(b.hash == body.modalTableHash for b in file.blobs):
        return [LoadError(path, "reference", "expected the hash of an entry of blobs")]
    return []


def cross_field_errors(file: models.InstrumentFile) -> list[LoadError]:
    """Rules JSON Schema cannot express (docs/dev/instrument-format.md, "Cross-field rules")."""
    d = file.desc
    out: list[LoadError] = []
    seen: set[int] = set()
    for g, group in enumerate(d.groups):
        if group.key >= 0:
            if group.key in seen:
                out.append(
                    LoadError(
                        f"/instrument/groups/{g}/key",
                        "duplicate",
                        f"key {group.key} is used by an earlier group",
                    )
                )
            seen.add(group.key)
    for g, group in enumerate(d.groups):
        group_path = f"/instrument/groups/{g}"
        out.extend(_check_segment_order(group, group_path))
        for s, seg in enumerate(group.segments):
            out.extend(_check_segment(seg, f"{group_path}/segments/{s}", len(d.bridgePoints)))
    out.extend(_check_plate(d.body))
    out.extend(_check_modal_hash(file))
    return out


# --- blob manifest -------------------------------------------------------------------------------


def valid_blob_path(path: str) -> bool:
    """Relative, forward slashes, no empty, '.' or '..' part and no ':'."""
    if not path or path.startswith("/") or "\\" in path or ":" in path:
        return False
    return all(part not in ("", ".", "..") for part in path.split("/"))


def _array_bytes(a: models.BlobArray) -> int | None:
    size = DTYPE_SIZE[a.dtype]
    for dim in a.shape:
        if dim == 0 or size > SAFE_MAX // dim:
            return None
        size *= dim
    return size


def manifest_errors(blobs: list[models.BlobEntry]) -> list[LoadError]:
    """Manifest checks that need no file: paths, alignment, overlap, duplicates."""
    out: list[LoadError] = []
    paths: set[str] = set()
    hashes: set[str] = set()
    for i, b in enumerate(blobs):
        bp = f"/blobs/{i}"
        if not valid_blob_path(b.path):
            out.append(
                LoadError(
                    f"{bp}/path",
                    "blob",
                    "expected a relative path with forward slashes and no empty, '.' or '..' part",
                )
            )
        elif b.path in paths:
            out.append(LoadError(f"{bp}/path", "duplicate", "a blob path appears twice"))
        else:
            paths.add(b.path)
        if b.hash in hashes:
            out.append(LoadError(f"{bp}/hash", "duplicate", "a blob hash appears twice"))
        hashes.add(b.hash)
        names: set[str] = set()
        spans: list[tuple[int, int, int]] = []
        for k, a in enumerate(b.arrays):
            ap = f"{bp}/arrays/{k}"
            if a.name in names:
                out.append(
                    LoadError(f"{ap}/name", "duplicate", "an array name appears twice in one blob")
                )
            names.add(a.name)
            size = DTYPE_SIZE[a.dtype]
            if a.offset % size != 0:
                out.append(
                    LoadError(f"{ap}/offset", "blob", f"expected an offset aligned to {size} bytes")
                )
            nbytes = _array_bytes(a)
            if nbytes is None:
                out.append(LoadError(f"{ap}/shape", "blob", "array is larger than 2^53 bytes"))
                continue
            spans.append((a.offset, a.offset + nbytes, k))
        spans.sort(key=lambda s: (s[0], s[2]))
        for prev, cur in pairwise(spans):
            if cur[0] < prev[1]:
                out.append(
                    LoadError(
                        f"{bp}/arrays/{cur[2]}/offset",
                        "blob",
                        "array overlaps the array before it in the file",
                    )
                )
    return out


def blob_file_errors(
    blobs: list[models.BlobEntry], directory: Path, verify: bool
) -> list[LoadError]:
    """Checks against the blob files: lengths when the file exists; existence and SHA-256 when
    ``verify``."""
    out: list[LoadError] = []
    for i, b in enumerate(blobs):
        if not valid_blob_path(b.path):
            continue
        bp = f"/blobs/{i}"
        file = directory.joinpath(*b.path.split("/"))
        if not file.is_file():
            if verify:
                out.append(LoadError(f"{bp}/path", "blob", f"blob file not found: {b.path}"))
            continue
        try:
            length = file.stat().st_size
        except OSError:
            out.append(LoadError(f"{bp}/path", "blob", f"cannot read blob file: {b.path}"))
            continue
        for k, a in enumerate(b.arrays):
            nbytes = _array_bytes(a)
            if nbytes is not None and a.offset + nbytes > length:
                out.append(
                    LoadError(
                        f"{bp}/arrays/{k}/offset",
                        "blob",
                        f"array extends past the end of the file ({length} bytes)",
                    )
                )
        if verify:
            digest = hashlib.sha256()
            try:
                with file.open("rb") as handle:
                    for chunk in iter(lambda: handle.read(65536), b""):
                        digest.update(chunk)
            except OSError:
                out.append(LoadError(f"{bp}/path", "blob", f"cannot read blob file: {b.path}"))
                continue
            if f"sha256:{digest.hexdigest()}" != b.hash:
                out.append(LoadError(f"{bp}/hash", "blob", f"expected sha256:{digest.hexdigest()}"))
    return out


# --- whole documents -----------------------------------------------------------------------------


def _read_version(doc: dict[str, Any]) -> tuple[int, LoadError | None]:
    """The schema version of a root object; an error if present but unsupported."""
    if "schemaVersion" not in doc:
        return CURRENT_VERSION, None  # reported as `missing` by the schema pass
    v = doc["schemaVersion"]
    integral = (isinstance(v, int) and not isinstance(v, bool)) or (
        isinstance(v, float) and v.is_integer()
    )
    if not integral:
        return 0, LoadError("/schemaVersion", "version", "expected an integer schema version")
    if v < 0 or v > CURRENT_VERSION:
        return 0, LoadError(
            "/schemaVersion", "version", f"expected a schema version from 0 to {CURRENT_VERSION}"
        )
    return int(v), None


def process_document(
    document: object, directory: Path | None = None, verify_blobs: bool = False
) -> tuple[models.InstrumentFile | None, list[LoadError]]:
    """Version check, migration, schema, cross-field and blob checks of a parsed document.

    Returns the loaded file, or None with every error found (sorted, at most 100). Blob files are
    checked only when ``directory`` is given.
    """
    if not isinstance(document, dict):
        return None, [LoadError("", "type", "expected an object")]
    version, error = _read_version(document)
    if error is not None:
        return None, [error]
    doc = document
    if version < CURRENT_VERSION:
        doc = copy.deepcopy(document)
        migration_errors = migrate(doc, version)
        if migration_errors:
            return None, finalize(migration_errors)
    errors = schema_errors(doc)
    if errors:
        return None, finalize(errors)
    file = file_from_json(doc, version)
    errors = cross_field_errors(file) + manifest_errors(file.blobs)
    if directory is not None:
        errors += blob_file_errors(file.blobs, directory, verify_blobs)
    if errors:
        return None, finalize(errors)
    return file, []


def validate(document: object) -> list[LoadError]:
    """Every error of a parsed document (version, migration, schema, cross-field rules and
    manifest consistency); empty if the document is valid. No file is read."""
    return process_document(document)[1]
