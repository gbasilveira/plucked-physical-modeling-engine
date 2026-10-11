# SPDX-License-Identifier: MIT
"""B1-T1: Python half of the Python -> JSON -> C++ -> JSON round trip.

The C++ half (engine/tests/loader) loads the same committed files, writes them back and compares
byte for byte. Both halves must reproduce every committed file exactly.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from pme_compiler.schema import dumps, load
from pme_compiler.schema.canonical import format_double

REPO = Path(__file__).resolve().parents[2]
DATA = REPO / "compiler" / "tests" / "data" / "b1"
EXAMPLES = REPO / "instruments" / "examples"

ROUND_TRIP_FILES = [
    *sorted(EXAMPLES.glob("*/instrument.json")),
    DATA / "all-fields.json",
    DATA / "body-measured.json",
    DATA / "numbers.json",
]


def test_b1_t1_files_exist() -> None:
    """B1-T1: the four examples and the three fixtures are committed."""
    assert [p.parent.name for p in EXAMPLES.glob("*/instrument.json")] == sorted(
        ["generic-grand-88", "test-guitar-6", "test-monochord", "test-unison-pair"]
    )
    assert all(p.exists() for p in ROUND_TRIP_FILES)


@pytest.mark.parametrize("path", ROUND_TRIP_FILES, ids=lambda p: p.parent.name + "/" + p.name)
def test_b1_t1_python_dumps_is_byte_identical(path: Path) -> None:
    """B1-T1: load then dumps reproduces the committed canonical file; load(dumps(x)) == x."""
    first = load(path)
    text = dumps(first)
    assert text == path.read_text(encoding="utf-8")
    assert "\r" not in text
    assert text.endswith("}\n")
    second = load(text)
    assert second == first


def test_b1_t1_all_fields_has_every_variant() -> None:
    """B1-T1: all-fields.json holds every exciter alternative and every group/segment enum value."""
    f = load(DATA / "all-fields.json")
    kinds = {type(g.exciter.e).__name__ for g in f.desc.groups if g.exciter is not None}
    assert kinds == {"HammerParams", "PluckPulse", "Plectrum", "Finger", "InitialDisplacement"}
    assert any(g.exciter is None for g in f.desc.groups)
    segments = [s for g in f.desc.groups for s in g.segments]
    assert {s.string.boundary for s in segments} == {"Pinned", "Clamped"}
    assert {s.string.bridgeEnd for s in segments} == {"AtL", "At0"}
    assert {s.damper.mode for s in segments if s.damper} == {"Diagonal", "RankOne"}
    assert any(s.damper is None for s in segments)
    assert {b.kind for s in segments for b in s.barriers} == {
        "Custom", "Fret", "Fretboard", "Jawari", "Thread", "Sawari"
    }  # fmt: skip
    assert {b.side for s in segments for b in s.barriers} == {-1, 1}
    assert {
        e.law for g in f.desc.groups if g.exciter for e in [g.exciter.e] if hasattr(e, "law")
    } == {"Stulov"}
    assert f.desc.body.source == "ModalTable"
    assert len(f.blobs) == 2
    assert load(DATA / "body-measured.json").desc.body.source == "MeasuredMobilityFit"


@pytest.mark.parametrize(
    ("value", "text"),
    [
        (0.0, "0.0"),
        (-0.0, "-0.0"),
        (1.0, "1.0"),
        (100.0, "100.0"),
        (0.1, "0.1"),
        (0.0001, "0.0001"),
        (0.00001, "1e-05"),
        (1.5e-9, "1.5e-09"),
        (2e-9, "2e-09"),
        (123456.789, "123456.789"),
        (1e14, "100000000000000.0"),
        (1e15, "1e+15"),
        (1.5e300, "1.5e+300"),
        (5e-324, "5e-324"),
        (200e9, "200000000000.0"),
        (-2.5, "-2.5"),
        (1.7976931348623157e308, "1.7976931348623157e+308"),
    ],
)
def test_b1_t1_number_format_follows_nlohmann(value: float, text: str) -> None:
    """B1-T1: the canonical number layout (shortest round trip, nlohmann/json style)."""
    assert format_double(value) == text
    assert float(text) == value
