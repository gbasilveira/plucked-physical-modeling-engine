# SPDX-License-Identifier: MIT
"""P0.3-T2 and P0.3-R4 (Python): the golden-feature comparator passes identical features and flags
a 1% decay change; file format round trip, overrides and structural failures."""

from __future__ import annotations

import copy
import json
import math
from collections.abc import Callable
from pathlib import Path

import pytest
from pme_analysis import (
    GoldenDecay2,
    GoldenEnergy,
    GoldenEnvelope,
    GoldenError,
    GoldenFeatures,
    GoldenPartial,
    PartialTolerance,
    compare_golden,
    format_comparison,
    parse_golden,
    read_golden_file,
    serialize_golden,
    write_golden_file,
)

TESTDATA = Path(__file__).resolve().parents[1] / "analysis" / "testdata"
SCHEMA = Path(__file__).resolve().parents[1] / "analysis" / "golden-features.schema.json"


def make_reference() -> GoldenFeatures:
    g = GoldenFeatures("unit-test", 48000.0, "pme-tests", "0")
    for n in (1, 2, 3):
        sigma = 0.5 + 0.25 * n
        g.partials.append(GoldenPartial(n, 110.0 * n, 1.0 / n, sigma, 3.0 * math.log(10.0) / sigma))
    g.partials[1].second_decay = GoldenDecay2(0.2, 1.5)
    g.spectral_envelope = GoldenEnvelope([100.0, 200.0, 400.0], [-20.0, -25.0, -40.0])
    g.contact_time_s = 0.0031
    g.energy = GoldenEnergy(1e-12, 2e-13)
    return g


def test_p0_3_t2_identical_features_pass() -> None:
    """P0.3-T2: a golden file compared with an identical copy passes with zero differences."""
    ref = make_reference()
    result = compare_golden(ref, parse_golden(serialize_golden(ref)))
    assert result.passed
    assert len(result.results) > 10
    assert all(r.passed and r.difference == 0.0 for r in result.results)


def test_p0_3_t2_one_percent_decay_change_fails_naming_partial_and_feature() -> None:
    """P0.3-T2: +1% on one partial's sigma fails, naming that partial and the feature."""
    ref = make_reference()
    cand = copy.deepcopy(ref)
    cand.partials[1].sigma_per_s *= 1.01
    result = compare_golden(ref, cand)
    assert not result.passed
    failed = [r for r in result.results if not r.passed]
    assert [r.feature for r in failed] == ["partial[2].decay_rate"]
    assert failed[0].difference == pytest.approx(0.01, rel=1e-9)
    assert failed[0].tolerance == 0.005
    text = format_comparison(result)
    assert "partial[2].decay_rate" in text and "FAIL" in text


def test_p0_3_t2_point_four_percent_decay_change_passes() -> None:
    """P0.3-T2: +0.4% on one partial's sigma passes at the 0.5% default (control)."""
    ref = make_reference()
    cand = copy.deepcopy(ref)
    cand.partials[1].sigma_per_s *= 1.004
    assert compare_golden(ref, cand).passed


def passes_after(ref: GoldenFeatures, edit: Callable[[GoldenFeatures], None]) -> bool:
    cand = copy.deepcopy(ref)
    edit(cand)
    return compare_golden(ref, cand).passed


def test_p0_3_r4_defaults_flag_changes_just_outside() -> None:
    """P0.3-R4: each default tolerance flags a change just outside it and passes just inside."""
    ref = make_reference()

    def freq(cents: float) -> Callable[[GoldenFeatures], None]:
        def edit(c: GoldenFeatures) -> None:
            c.partials[0].frequency_hz = 110.0 * 2 ** (cents / 1200)

        return edit

    def amp(db: float) -> Callable[[GoldenFeatures], None]:
        def edit(c: GoldenFeatures) -> None:
            c.partials[2].amplitude = (1.0 / 3.0) * 10 ** (db / 20)

        return edit

    def envelope(db: float) -> Callable[[GoldenFeatures], None]:
        def edit(c: GoldenFeatures) -> None:
            assert c.spectral_envelope is not None
            c.spectral_envelope.magnitude_db[1] = -25.0 + db

        return edit

    def contact(factor: float) -> Callable[[GoldenFeatures], None]:
        def edit(c: GoldenFeatures) -> None:
            c.contact_time_s = 0.0031 * factor

        return edit

    def energy(delta: float) -> Callable[[GoldenFeatures], None]:
        def edit(c: GoldenFeatures) -> None:
            assert c.energy is not None
            c.energy.max_relative_drift += delta

        return edit

    assert not passes_after(ref, freq(0.011))
    assert passes_after(ref, freq(0.009))
    assert not passes_after(ref, amp(0.11))
    assert passes_after(ref, amp(0.09))
    assert not passes_after(ref, envelope(1.1))
    assert passes_after(ref, envelope(0.9))
    assert not passes_after(ref, contact(1.011))
    assert passes_after(ref, contact(1.009))
    assert not passes_after(ref, energy(2e-9))
    assert passes_after(ref, energy(5e-10))


def test_p0_3_r4_per_partial_overrides_replace_default() -> None:
    """P0.3-R4: a per-partial tolerance override replaces the default and survives a round trip."""
    ref = make_reference()
    ref.tolerances.partials[2] = PartialTolerance(decay_rate_relative=0.05)
    cand = copy.deepcopy(ref)
    cand.partials[1].sigma_per_s *= 1.04
    assert compare_golden(ref, cand).passed
    cand.partials[0].sigma_per_s *= 1.04
    result = compare_golden(ref, cand)
    assert not result.passed
    again = parse_golden(serialize_golden(ref))
    assert again.tolerances.partials[2].decay_rate_relative == 0.05
    assert again.tolerances.partials[2].frequency_cents is None


def test_p0_3_r4_missing_features_and_grids_fail() -> None:
    """P0.3-R4: missing partials, features and mismatched grids fail; extra partials are noted."""
    ref = make_reference()
    cand = copy.deepcopy(ref)
    cand.partials.pop()
    result = compare_golden(ref, cand)
    assert not result.passed
    assert any(r.feature == "partial[3]" and "missing" in r.note for r in result.results)

    cand = copy.deepcopy(ref)
    assert cand.spectral_envelope is not None
    cand.spectral_envelope.frequencies_hz[1] = 201.0
    assert not compare_golden(ref, cand).passed

    cand = copy.deepcopy(ref)
    cand.contact_time_s = None
    cand.energy = None
    cand.partials[1].second_decay = None
    assert not compare_golden(ref, cand).passed

    cand = copy.deepcopy(ref)
    extra = copy.deepcopy(ref.partials[0])
    extra.index = 9
    cand.partials.append(extra)
    result = compare_golden(ref, cand)
    assert result.passed and len(result.notes) == 1


def test_p0_3_r4_parser_rejects_malformed_files() -> None:
    """P0.3-R4: the parser raises GoldenError naming the offending field."""
    with pytest.raises(GoldenError):
        parse_golden("{")
    with pytest.raises(GoldenError, match="schema_version"):
        parse_golden("{}")
    with pytest.raises(GoldenError, match="schema_version"):
        parse_golden('{"schema_version": 2}')
    text = serialize_golden(make_reference()).replace('"scenario"', '"scenari0"')
    with pytest.raises(GoldenError, match="scenario"):
        parse_golden(text)


def test_p0_3_r4_files_round_trip_and_cli_fixtures_agree(tmp_path: Path) -> None:
    """P0.3-R4: files round trip and the C++ CLI fixtures give the same verdicts here."""
    ref = make_reference()
    path = tmp_path / "ref.json"
    write_golden_file(path, ref)
    assert read_golden_file(path) == ref

    reference = read_golden_file(TESTDATA / "golden_reference.json")
    assert compare_golden(reference, reference).passed
    assert compare_golden(
        reference, read_golden_file(TESTDATA / "golden_decay_plus_0p4pct.json")
    ).passed
    result = compare_golden(reference, read_golden_file(TESTDATA / "golden_decay_plus_1pct.json"))
    assert not result.passed
    assert [r.feature for r in result.results if not r.passed] == ["partial[2].decay_rate"]


def test_p0_3_r4_schema_file_is_valid_json_and_lists_the_written_fields() -> None:
    """P0.3-R4: the schema parses, requires what the writer emits and allows all its keys."""
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    assert schema["$schema"].endswith("2020-12/schema")
    doc = json.loads(serialize_golden(make_reference()))
    assert set(schema["required"]) <= set(doc)
    assert set(doc) <= set(schema["properties"])
    partial_schema = schema["properties"]["partials"]["items"]
    for p in doc["partials"]:
        assert set(partial_schema["required"]) <= set(p)
        assert set(p) <= set(partial_schema["properties"])


def test_p0_3_r4_report_names_the_failing_feature() -> None:
    """P0.3-R4: the readable report names the failing partial and feature."""
    ref = read_golden_file(TESTDATA / "golden_reference.json")
    cand = read_golden_file(TESTDATA / "golden_decay_plus_1pct.json")
    text = format_comparison(compare_golden(ref, cand))
    assert "FAIL" in text and "partial[2].decay_rate" in text
