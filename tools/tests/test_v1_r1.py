# SPDX-License-Identifier: MIT
"""V1-R1: the extractors read IDs from the plan, the code and the test results."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

TRACE = Path(__file__).resolve().parents[1] / "trace"
sys.path.insert(0, str(TRACE))

from trace_matrix.ids import detect_system, name_id, python_name_to_id  # noqa: E402
from trace_matrix.plan import (  # noqa: E402
    PlanError,
    parse_gates,
    parse_orc_ids,
    parse_phase_cards,
    parse_phase_status,
)
from trace_matrix.results import ResultsError, read_job_results, read_junit_dir  # noqa: E402
from trace_matrix.sources import (  # noqa: E402
    parse_covers,
    scan_cpp,
    scan_python,
    scan_workflow,
)

ROOT = Path(__file__).resolve().parents[2]
CARD = """### A9 Example

- **Requirements:**
  1. A9-R1 first thing
  2. A9-R2 second thing,
     wrapped on a second line.
- **Outputs:** x
- **Tests:** ORC-03; A9-T1 first test; A9-T2 second test; common tests.
- **References:** SPEC-OSC
"""


def test_v1_r1_the_real_plan_parses_without_error() -> None:
    """V1-R1: every phase card and gate of docs/plan/phasing-plan.md parses (a bad card fails)."""
    text = (ROOT / "docs/plan/phasing-plan.md").read_text(encoding="utf-8")
    cards = {c.id: c for c in parse_phase_cards(text)}
    assert len(cards["P0.1"].requirements) == 6
    assert [t.id for t in cards["P0.3"].tests] == ["P0.3-T1", "P0.3-T2", "P0.3-T3"]
    assert cards["A1"].oracle_ids == ["ORC-01", "ORC-02"]
    assert cards["V1"].requirements[2].id == "V1-R3"
    gates = {g.id: g for g in parse_gates(text)}
    assert gates["G1"].oracle_ids[:3] == ["ORC-01", "ORC-02", "ORC-03"]
    assert "ORC-10" not in gates["G1"].oracle_ids
    assert gates["G2"].oracle_ids == ["ORC-16", "ORC-17", "ORC-18"]
    assert gates["G6"].oracle_ids == ["ORC-10", "ORC-14", "ORC-15"]
    assert gates["G1"].requires_phases == [
        "A1",
        "A2",
        "A3",
        "A4",
        "A5",
        "A6",
        "A7",
        "A8",
        "A9",
        "P0.5",
    ]


def test_v1_r1_a_card_gives_requirements_tests_and_oracles() -> None:
    """V1-R1: requirement text (with wrapped lines), test criteria and ORC rows of a card."""
    (card,) = parse_phase_cards(CARD)
    assert [r.id for r in card.requirements] == ["A9-R1", "A9-R2"]
    assert card.requirements[1].text == "second thing, wrapped on a second line."
    assert [(t.id, t.criterion) for t in card.tests] == [
        ("A9-T1", "first test"),
        ("A9-T2", "second test"),
    ]
    assert card.oracle_ids == ["ORC-03"]


@pytest.mark.parametrize(
    ("old", "new"),
    [
        ("- **Tests:** ORC-03; A9-T1 first test; A9-T2 second test; common tests.\n", ""),
        ("  2. A9-R2", "  2. A9-R3"),
        ("A9-T2 second test", "A9-T3 second test"),
        ("ORC-03;", "nonsense;"),
        ("- **Requirements:**", "- **Requirements (none):**"),
    ],
)
def test_v1_r1_a_malformed_card_fails_loudly(old: str, new: str) -> None:
    """V1-R1: a card that cannot be parsed raises PlanError instead of being skipped."""
    with pytest.raises(PlanError):
        parse_phase_cards(CARD.replace(old, new))


def test_v1_r1_oracle_ranges_expand() -> None:
    """V1-R1: 'ORC-01 to 09, 11 to 13 and 19' names the rows a gate row needs."""
    ids = parse_orc_ids("ORC-01 to 09, 11 to 13 and 19 pass; and ORC-16, 17 and 18")
    assert ids[:9] == [f"ORC-0{n}" for n in range(1, 10)]
    assert ids[9:] == ["ORC-11", "ORC-12", "ORC-13", "ORC-19", "ORC-16", "ORC-17", "ORC-18"]


def test_v1_r1_python_test_names_map_to_ids() -> None:
    """V1-R1: test_<id>_<what> maps to the ID with the P0.3-R1 rule; C++ names use the prefix."""
    assert python_name_to_id("test_p0_3_r1_names") == "P0.3-R1"
    assert python_name_to_id("test_a2_t1_wound") == "A2-T1"
    assert python_name_to_id("test_orc_01_accuracy") == "ORC-01"
    assert python_name_to_id("test_p1_t2_x") == "P1-T2"
    assert python_name_to_id("test_something_else") is None
    assert name_id("P0.4-T3 parameter IDs are unique") == "P0.4-T3"
    assert name_id("ORC-17 sympathetic") == "ORC-17"
    assert name_id("pme_bench_smoke") is None
    assert name_id("P0.4-R4_params_json_matches_registry") == "P0.4-R4"
    assert name_id("A1-T10x") is None  # the ID must be followed by a space
    assert detect_system("junit-build-macos-arm64", None) == "macos"
    assert detect_system("junit-sanitizers-windows-asan", None) == "windows"
    assert detect_system("junit-performance", "linux") == "linux"


def test_v1_r1_covers_annotations_are_read_above_tests() -> None:
    """V1-R1: 'Covers:' in the comment block above a TEST_CASE or def adds IDs."""
    assert parse_covers("// Covers: P0.2-R4, P0.2-R5 and A1-T1") == ["P0.2-R4", "P0.2-R5", "A1-T1"]
    assert parse_covers("// an ordinary comment P0.2-R4") == []
    cpp = (
        '// Covers: P0.2-R4,\n// Covers: P0.2-R5\nTEST_CASE("P0.2-T1 name") {}\n'
        '// plain comment\nTEST_CASE("P0.2-T2 other") {}\n'
        '\n// Covers: P0.9-R1\n\nTEST_CASE("P0.2-T3 gap before it") {}\n'
    )
    tests = scan_cpp(cpp, "x.cpp")
    assert [t.ids for t in tests] == [["P0.2-T1", "P0.2-R4", "P0.2-R5"], ["P0.2-T2"], ["P0.2-T3"]]
    py = (
        "import pytest\n\n# Covers: A1-R2\n@pytest.mark.parametrize('x', [1])\n"
        'def test_a1_t1_thing(x):\n    """A1-T1: thing."""\n\n'
        'def test_zz_unnamed():\n    """Docstring A1-T9 not at the start."""\n'
    )
    found = scan_python(py, "t.py")
    assert [(t.name, t.ids) for t in found] == [
        ("test_a1_t1_thing", ["A1-T1", "A1-R2"]),
        ("test_zz_unnamed", []),
    ]


def test_v1_r1_workflow_annotations_find_steps_and_jobs() -> None:
    """V1-R1: '# Covers:' above '- name:' covers that step; above a job key it covers the job."""
    text = """name: x
on: push
jobs:
  # Covers: P0.2-R1
  lint:
    steps:
      - uses: actions/checkout@v4
      # Covers: P0.2-R4, P0.2-T2
      - name: clang-format
        run: echo
      # Covers: Z9
      env:
        a: b
  build:
    steps:
      # Covers: P0.2-R6
      - name: "upload"
"""
    found = scan_workflow(text, "pr.yml")
    assert [(a.job, a.step, a.ids) for a in found] == [
        ("lint", "(job)", ("P0.2-R1",)),
        ("lint", "clang-format", ("P0.2-R4", "P0.2-T2")),
        ("build", "upload", ("P0.2-R6",)),
    ]


def test_v1_r1_junit_files_are_read_per_artifact_and_system(tmp_path: Path) -> None:
    """V1-R1: ctest and pytest results map to names, outcomes, jobs and systems."""
    art = tmp_path / "junit-build-windows-msvc"
    art.mkdir()
    (art / "ctest.xml").write_text(
        '<testsuite name="(empty)"><testcase name="A1-T1 x y" classname="A1-T1 x y" status="run">'
        '<properties><property name="cmake_labels" value="unit;oracle"/></properties></testcase>'
        '<testcase name="A1-T2 z" classname="A1-T2 z" status="fail"><failure message="m"/>'
        "</testcase></testsuite>",
        encoding="utf-8",
    )
    py = tmp_path / "junit-python-linux"
    py.mkdir()
    (py / "pytest.xml").write_text(
        '<testsuites><testsuite name="pytest">'
        '<testcase classname="tools.tests.test_a" name="test_a1_t1_x[fmt-1]"/>'
        '<testcase classname="tools.tests.test_a" name="test_a1_t2_y"><skipped/></testcase>'
        "</testsuite></testsuites>",
        encoding="utf-8",
    )
    results = read_junit_dir(tmp_path, "linux")
    summary = [(r.kind, r.name, r.outcome, r.job, r.system) for r in results]
    assert summary == [
        ("c", "A1-T1 x y", "pass", "junit-build-windows-msvc", "windows"),
        ("c", "A1-T2 z", "fail", "junit-build-windows-msvc", "windows"),
        ("p", "test_a1_t1_x", "pass", "junit-python-linux", "linux"),
        ("p", "test_a1_t2_y", "skipped", "junit-python-linux", "linux"),
    ]
    assert results[0].labels == ("unit", "oracle")
    (art / "bad.xml").write_text("<testsuite", encoding="utf-8")
    with pytest.raises(ResultsError):
        read_junit_dir(tmp_path, "linux")


def test_v1_r1_job_results_accept_the_needs_json_shape(tmp_path: Path) -> None:
    """V1-R1: toJSON(needs) and a flat {job: result} object are both read."""
    path = tmp_path / "needs.json"
    path.write_text(json.dumps({"lint": {"result": "success", "outputs": {}}, "build": "failure"}))
    assert read_job_results(path) == {"lint": "success", "build": "failure"}
    path.write_text("[1]")
    with pytest.raises(ResultsError):
        read_job_results(path)


def test_v1_r1_status_file_gives_phase_and_gate_status() -> None:
    """V1-R1: phase and gate status are read from docs/status/phase-status.md."""
    text = (ROOT / "docs/status/phase-status.md").read_text(encoding="utf-8")
    phases, gates = parse_phase_status(text)
    assert phases["P0.1"] == "complete"
    assert gates["G0"] == "passed"
    assert gates["G1"] == "not passed"
