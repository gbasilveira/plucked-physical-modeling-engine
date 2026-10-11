# SPDX-License-Identifier: MIT
"""Validation errors of the instrument file loader (B1-R5)."""

from __future__ import annotations

from dataclasses import dataclass

from pme_compiler.schema.fields import MAX_ERRORS

# The closed list of error codes, shared with the C++ loader.
CODES = (
    "parse", "type", "missing", "unknown_key", "range", "enum", "version", "migration",
    "reference", "duplicate", "structure", "blob",
)  # fmt: skip


@dataclass(frozen=True, order=True)
class LoadError:
    """One error: a JSON Pointer (RFC 6901), a code and a message naming the expected value."""

    path: str
    code: str
    message: str = ""

    def __str__(self) -> str:
        return f"{self.path or '/'}: {self.code}: {self.message}"


class InstrumentError(Exception):
    """Raised by ``load``/``loads`` for an invalid document; ``errors`` lists every error found."""

    def __init__(self, errors: list[LoadError]) -> None:
        super().__init__("\n".join(str(e) for e in errors))
        self.errors = errors


def pointer(*parts: object) -> str:
    """Build a JSON Pointer from path parts (RFC 6901 escaping)."""
    return "".join("/" + str(p).replace("~", "~0").replace("/", "~1") for p in parts)


def finalize(errors: list[LoadError]) -> list[LoadError]:
    """Sort by (path, code), keep one error per pair, let a ``type`` error hide the others at its
    path, and cap the list (B1-R5). The C++ loader applies the same rule."""
    ordered = sorted(errors, key=lambda e: (e.path, e.code))
    out: list[LoadError] = []
    i = 0
    while i < len(ordered):
        j = i
        while j < len(ordered) and ordered[j].path == ordered[i].path:
            j += 1
        group = ordered[i:j]
        has_type = any(e.code == "type" for e in group)
        previous = ""
        for e in group:
            if e.code != previous and (not has_type or e.code == "type"):
                out.append(e)
            previous = e.code
        i = j
    return out[:MAX_ERRORS]
