# SPDX-License-Identifier: MIT
"""B1-R6: the example instruments are reproducible and meet their stated bounds."""

from __future__ import annotations

import importlib.util
import math
from pathlib import Path
from types import ModuleType

from pme_compiler.examples import common, generic_grand, small, test_guitar
from pme_compiler.schema import dumps, load

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = REPO / "instruments" / "examples"
DATA = REPO / "compiler" / "tests" / "data" / "b1"


def _text(name: str) -> str:
    return (EXAMPLES / name / "instrument.json").read_text(encoding="utf-8")


def test_b1_r6_generic_grand_regenerates_byte_identical() -> None:
    """B1-R6: regenerating generic-grand-88 reproduces the committed file."""
    assert dumps(generic_grand.build()) == _text("generic-grand-88")


def test_b1_r6_test_guitar_regenerates_byte_identical() -> None:
    """B1-R6: regenerating test-guitar-6 reproduces the committed file."""
    assert dumps(test_guitar.build()) == _text("test-guitar-6")


def test_b1_r6_small_examples_regenerate_byte_identical() -> None:
    """B1-R6: regenerating test-monochord and test-unison-pair reproduces the committed files."""
    assert dumps(small.monochord()) == _text("test-monochord")
    assert dumps(small.unison_pair()) == _text("test-unison-pair")


def test_b1_r6_generic_grand_tension_bounds() -> None:
    """B1-R6: every speaking string's closed-form tension lies in 400-2000 N."""
    report = generic_grand.tension_report(load(EXAMPLES / "generic-grand-88" / "instrument.json"))
    for low, high, mean_b in report.values():
        assert 400.0 <= low <= high <= 2000.0
        assert 0.0 < mean_b < 0.1


def test_b1_r6_generic_grand_structure() -> None:
    """B1-R6: 88 groups, keys 21 to 108, strings per key, dampers, tuning defaults."""
    desc = load(EXAMPLES / "generic-grand-88" / "instrument.json").desc
    assert [g.key for g in desc.groups] == list(range(21, 109))
    counts = [len(g.segments) for g in desc.groups]
    assert counts[:8] == [1] * 8 and counts[8:20] == [2] * 12 and counts[20:] == [3] * 68
    assert len(desc.bridgePoints) == 88
    assert desc.tuning.referencePitch == 440.0 and desc.tuning.stretchCents == []
    for g in desc.groups:
        assert all(s.unisonDetuneCents == 0.0 for s in g.segments)
        assert all(s.string.bridgeEnd == "AtL" for s in g.segments)
        assert all(s.damper is not None and s.damper.hasDamper == (g.key <= 88) for s in g.segments)
        assert g.segments[0].string.f1Target == common.equal_tempered(g.key)
    assert desc.groups[19].segments[0].string.L == 1.5  # anchor at key 40
    assert desc.groups[39].segments[0].string.L == 0.64  # anchor at key 60
    assert [c.name for c in desc.channels] == ["L", "R"]
    assert desc.body.plate.thickness == 0.009 and desc.body.plate.rimStiffness == 1.0e7
    assert len(desc.body.plate.ribs) == 12 and len(desc.body.plate.bridges) == 2
    assert 8 <= len(desc.body.plate.outline) <= 12


def test_b1_r6_hammer_contact_times() -> None:
    """B1-R6: K reproduces the SPEC-HAM contact time at 3 m/s (4 ms at key 21, 0.5 ms at 108)."""
    desc = load(EXAMPLES / "generic-grand-88" / "instrument.json").desc
    for index, target in ((0, 0.004), (len(desc.groups) - 1, 0.0005)):
        e = desc.groups[index].exciter
        assert e is not None
        h = e.e
        t_c = common.hammer_contact_time(h.mass, h.K, h.p, 3.0)  # type: ignore[union-attr]
        assert math.isclose(t_c, target, rel_tol=1e-4)
    mono = load(EXAMPLES / "test-monochord" / "instrument.json").desc.groups[0].exciter
    assert mono is not None
    assert math.isclose(
        common.hammer_contact_time(mono.e.mass, mono.e.K, mono.e.p, 3.0),  # type: ignore[union-attr]
        0.002,
        rel_tol=1e-4,
    )


def test_b1_r6_guitar_tensions() -> None:
    """B1-R6: each guitar string carries 70 N at its open pitch; strings 1-3 plain, 4-6 wound."""
    desc = load(EXAMPLES / "test-guitar-6" / "instrument.json").desc
    assert [g.key for g in desc.groups] == [-1] * 6
    assert [g.segments[0].string.f1Target for g in desc.groups] == [
        82.407, 110.0, 146.832, 195.998, 246.942, 329.628
    ]  # fmt: skip
    for i, g in enumerate(desc.groups):
        s = g.segments[0].string
        assert math.isclose(common.tension(s), 70.0, rel_tol=1e-3)
        assert (s.dOuter == s.dCore) == (i >= 3)
    assert desc.groups[0].segments[0].string.dCore == 0.5e-3


def test_b1_r6_fixtures_are_current() -> None:
    """B1-R6: the committed B1 fixtures are the output of compiler/tests/data/b1/gen_fixtures.py."""
    spec = importlib.util.spec_from_file_location("b1_gen_fixtures", DATA / "gen_fixtures.py")
    assert spec is not None and spec.loader is not None
    module: ModuleType = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for path, data in module.all_outputs().items():
        assert path.read_bytes() == data, path
