# SPDX-License-Identifier: MIT
"""ID syntax shared by the extractors (P0.3-R1 naming rule, SPEC-ORC oracle IDs)."""

from __future__ import annotations

import re

# Requirement (-R), test (-T) and oracle (ORC-xx) IDs; the rule of docs/dev/testing.md, "Naming".
ID_PATTERN = r"(?:(?:P0\.[1-5]|[ABCPDEVH][0-9]+)-[RT][0-9]+|ORC-[0-9]{2})"
ID_RE = re.compile(ID_PATTERN)
# A C++/ctest test name: the ID, then a space (or an underscore, as in the CMake test
# P0.4-R4_params_json_matches_registry, or the end of the name).
NAME_ID_RE = re.compile(rf"^({ID_PATTERN})(?=[\s_]|$)")
# A Python docstring: the ID at the start.
DOC_ID_RE = re.compile(rf"^({ID_PATTERN})\b")
# A Python test function name: test_<id lower-cased, '.' and '-' replaced by '_'>_<what>.
PY_NAME_RE = re.compile(r"^test_((?:p0_[1-5]|[abcpdevh][0-9]+)_[rt][0-9]+|orc_[0-9]{2})(?:_|$)")
PHASE_ID_PATTERN = r"(?:P0\.[1-5]|[A-Z][0-9]+)"
PHASE_ID_RE = re.compile(PHASE_ID_PATTERN)

SYSTEMS = ("linux", "macos", "windows")

# Phases whose tests exist on fewer systems than SYSTEMS; every other phase needs all of them.
PHASE_SYSTEMS: dict[str, tuple[str, ...]] = {
    # D3 card (docs/plan/phasing-plan.md): "a native LV2 build for Linux hosts"; its tests run only
    # in the `lv2 (linux)` job.
    "D3": ("linux",),
}


def python_name_to_id(function_name: str) -> str | None:
    """Return the ID a Python test function name encodes, or None."""
    match = PY_NAME_RE.match(function_name)
    if match is None:
        return None
    parts = match.group(1).split("_")
    if parts[0] == "orc":
        return f"ORC-{parts[1]}"
    if parts[0] == "p0":
        return f"P0.{parts[1]}-{parts[2].upper()}"
    return f"{parts[0].upper()}-{parts[1].upper()}"


def name_id(test_name: str) -> str | None:
    """Return the ID that begins a C++/ctest test name, or None."""
    match = NAME_ID_RE.match(test_name)
    return match.group(1) if match else None


def phase_of(item_id: str) -> str | None:
    """Return the phase an R/T ID belongs to ('P0.3-R1' gives 'P0.3'); None for ORC IDs."""
    if item_id.startswith("ORC-"):
        return None
    return item_id.rsplit("-", 1)[0]


def detect_system(text: str, default: str | None) -> str | None:
    """Return the system named in an artifact or job name ('windows', 'macos', 'linux')."""
    lowered = text.lower()
    for system in SYSTEMS:
        if system in lowered:
            return system
    return default
