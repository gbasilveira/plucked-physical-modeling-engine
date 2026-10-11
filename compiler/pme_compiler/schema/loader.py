# SPDX-License-Identifier: MIT
"""Loading and writing instrument files (B1-R2 to B1-R5).

``load`` reads a path or a text, migrates older versions, validates and returns the models;
``dumps`` writes the canonical text. Errors are reported as ``LoadError`` lists with JSON Pointer
paths. The C++ loader (``engine/src/runtime/loader``) behaves the same way.
"""

from __future__ import annotations

import json
import math
import os
from pathlib import Path
from typing import Any

from pme_compiler.schema import models
from pme_compiler.schema.canonical import dumps_document
from pme_compiler.schema.codec import file_to_json
from pme_compiler.schema.errors import InstrumentError, LoadError
from pme_compiler.schema.fields import MAX_DEPTH
from pme_compiler.schema.validate import process_document


def _too_deep(text: str) -> bool:
    """True if arrays and objects nest deeper than MAX_DEPTH (strings and escapes are skipped)."""
    depth = 0
    in_string = False
    escaped = False
    for c in text:
        if in_string:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c in "[{":
            depth += 1
            if depth > MAX_DEPTH:
                return True
        elif c in "]}" and depth > 0:
            depth -= 1
    return False


def _parse_float(text: str) -> float:
    value = float(text)
    if not math.isfinite(value):
        raise ValueError("number out of range")
    return value


def _reject_constant(name: str) -> Any:
    raise ValueError(f"{name} is not valid JSON")


def parse_text(text: str) -> tuple[Any, list[LoadError]]:
    """Parse JSON text; errors are ``parse`` or ``structure`` at the document root."""
    if text.startswith("﻿"):
        return None, [LoadError("", "parse", "a byte order mark is not allowed")]
    if _too_deep(text):
        return None, [LoadError("", "structure", f"nesting is deeper than {MAX_DEPTH} levels")]
    try:
        return (
            json.loads(text, parse_float=_parse_float, parse_constant=_reject_constant),
            [],
        )
    except (ValueError, RecursionError):
        return None, [LoadError("", "parse", "the text is not valid JSON")]


def try_loads(
    text: str, directory: Path | None = None, verify_blobs: bool = False
) -> tuple[models.InstrumentFile | None, list[LoadError]]:
    """Load from text, returning the file or the errors (never raising for a bad document)."""
    document, errors = parse_text(text)
    if errors:
        return None, errors
    return process_document(document, directory, verify_blobs)


def try_load(
    path: str | os.PathLike[str], verify_blobs: bool = True
) -> tuple[models.InstrumentFile | None, list[LoadError]]:
    """Load from a file, also checking its blob files next to it (``verify_blobs``: recompute SHA-256)."""
    file = Path(path)
    try:
        raw = file.read_bytes()
    except OSError:
        return None, [LoadError("", "parse", f"cannot read {file}")]
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        return None, [LoadError("", "parse", "the file is not valid UTF-8")]
    return try_loads(text, file.parent, verify_blobs)


def loads(text: str) -> models.InstrumentFile:
    """Load from text. Raises ``InstrumentError`` listing every error found."""
    result, errors = try_loads(text)
    if result is None:
        raise InstrumentError(errors)
    return result


def load(source: str | os.PathLike[str], verify_blobs: bool = True) -> models.InstrumentFile:
    """Load a file path, or document text if ``source`` is a string that looks like JSON.

    Raises ``InstrumentError`` listing every error found.
    """
    if isinstance(source, str) and source.lstrip().startswith(("{", "[")):
        return loads(source)
    result, errors = try_load(source, verify_blobs)
    if result is None:
        raise InstrumentError(errors)
    return result


def dumps(file: models.InstrumentFile) -> str:
    """The canonical text of an instrument file (docs/dev/instrument-format.md, "Canonical form")."""
    return dumps_document(file_to_json(file))


def dump(file: models.InstrumentFile, path: str | os.PathLike[str]) -> None:
    """Write the canonical text to ``path`` (UTF-8, LF line ends)."""
    Path(path).write_text(dumps(file), encoding="utf-8", newline="\n")
