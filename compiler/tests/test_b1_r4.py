# SPDX-License-Identifier: MIT
"""B1-R4: Python loader behaviour on hostile input, and the canonical form."""

from __future__ import annotations

import json
from pathlib import Path

from pme_compiler.schema import dumps, load, loads, try_load, try_loads
from pme_compiler.schema.models import InstrumentFile

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = REPO / "instruments" / "examples"


def _codes(text: str) -> list[tuple[str, str]]:
    _, errors = try_loads(text)
    return [(e.path, e.code) for e in errors]


def test_b1_r4_garbage_input_reports_parse() -> None:
    """B1-R4: ten megabytes of garbage return a parse error and do not crash."""
    garbage = ("\x00\x7f}{][" * 1_000_000)[:10_000_000]
    assert _codes(garbage) == [("", "parse")]


def test_b1_r4_deep_nesting_reports_structure() -> None:
    """B1-R4: a 10000-deep nested array returns a structure error and does not crash."""
    assert _codes("[" * 10_000 + "]" * 10_000) == [("", "structure")]


def test_b1_r4_non_json_numbers_and_bom_are_parse_errors() -> None:
    """B1-R4: NaN, Infinity, out-of-range numbers and a byte order mark are parse errors."""
    for text in ("NaN", "[Infinity]", '{"a": 1e999}', "\ufeff{}"):
        assert _codes(text) == [("", "parse")]


def test_b1_r4_canonical_form_is_stable_and_lf_only() -> None:
    """B1-R4: 2-space indentation, final newline, keys in schema order, idempotent."""
    text = (EXAMPLES / "test-monochord" / "instrument.json").read_text(encoding="utf-8")
    assert text.startswith(
        '{\n  "format": "pme-instrument",\n  "schemaVersion": 1,\n  "instrument": {'
    )
    assert "\r" not in text and text.endswith("\n") and not text.endswith("\n\n")
    assert dumps(loads(text)) == text
    keys = list(json.loads(text)["instrument"]["groups"][0]["segments"][0]["string"])
    assert keys[:3] == ["L", "dCore", "rhoCore"]


def test_b1_r4_reordered_and_reformatted_input_canonicalises() -> None:
    """B1-R4: a compact, differently ordered document loads and writes the canonical text."""
    path = EXAMPLES / "test-monochord" / "instrument.json"
    doc = json.loads(path.read_text(encoding="utf-8"))
    reordered = {k: doc[k] for k in reversed(list(doc))}
    loaded = loads(json.dumps(reordered, separators=(",", ":")))
    assert dumps(loaded) == path.read_text(encoding="utf-8")


def test_b1_r4_optional_members_take_header_defaults() -> None:
    """B1-R4: members with a default initialiser may be omitted; unaCorda, defaults too."""
    doc = json.loads((EXAMPLES / "test-monochord" / "instrument.json").read_text(encoding="utf-8"))
    segment = doc["instrument"]["groups"][0]["segments"][0]
    del segment["longitudinalModes"], segment["kappaL"]
    del doc["instrument"]["groups"][0]["unaCorda"], doc["instrument"]["defaults"]
    del doc["blobs"]
    loaded = loads(json.dumps(doc))
    assert loaded.desc.groups[0].segments[0].longitudinalModes == 8
    assert loaded.desc.groups[0].segments[0].kappaL == 0.026
    assert loaded.desc.defaults.localBridgeFSplit == 2000.0
    assert loaded.desc.groups[0].unaCorda.s1 == 0.9


def test_b1_r4_load_accepts_path_and_text() -> None:
    """B1-R4: load takes a path or document text; both give equal results."""
    path = EXAMPLES / "test-unison-pair" / "instrument.json"
    from_path = load(path)
    from_text = load(path.read_text(encoding="utf-8"))
    assert from_path == from_text
    assert isinstance(from_path, InstrumentFile)
    assert try_load(path)[1] == []
