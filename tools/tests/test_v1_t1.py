# SPDX-License-Identifier: MIT
"""V1-T1: a deliberately removed test shows up as a gap in the traceability matrix.

The fixture repository under tools/trace/testdata/fixture/ is a small plan (two phase cards, one
gate), a few test sources, a workflow and JUnit files for the three systems. Each test copies it,
edits it and rebuilds the matrix with the real command line.
"""

from __future__ import annotations

import json
import re
import shutil
import sys
from pathlib import Path
from typing import Any

import pytest

TRACE = Path(__file__).resolve().parents[1] / "trace"
sys.path.insert(0, str(TRACE))

from trace_matrix.cli import run  # noqa: E402

FIXTURE = TRACE / "testdata" / "fixture"
CPP = "engine/tests/osc/osc_tests.cpp"


class Repo:
    """A writable copy of the fixture repository."""

    def __init__(self, path: Path) -> None:
        self.root = path / "repo"
        shutil.copytree(FIXTURE, self.root)
        self.write_junit()
        self.out = path / "out"
        self.known: Path | None = None
        self.job_results: Path | None = None

    def write_junit(self) -> None:
        """Write the JUnit XML files of junit.json (ctest and pytest style) under junit/."""
        spec = json.loads((self.root / "junit.json").read_text(encoding="utf-8"))
        for artifact in spec["ctest_artifacts"]:
            rows = "".join(
                f'\t<testcase name="{name}" classname="{name}" time="0.001" status="run">\n'
                f'\t\t<properties>\n\t\t\t<property name="cmake_labels" value="{labels}"/>\n'
                f"\t\t</properties>\n\t</testcase>\n"
                for name, labels in spec["ctest_cases"]
            )
            count = len(spec["ctest_cases"])
            self.put(
                f"junit/{artifact}/ctest-debug.xml",
                '<?xml version="1.0" encoding="UTF-8"?>\n<testsuite name="(empty)"\n'
                f'\ttests="{count}"\n\tfailures="0"\n\tdisabled="0"\n\tskipped="0"\n\t>\n'
                f"{rows}</testsuite>\n",
            )
        classname = spec["pytest_classname"]
        for artifact in spec["pytest_artifacts"]:
            cases = "".join(
                f'<testcase classname="{classname}" name="{name}" time="0.000" />'
                for name in spec["pytest_cases"]
            )
            self.put(
                f"junit/{artifact}/pytest.xml",
                '<?xml version="1.0" encoding="utf-8"?><testsuites name="pytest tests">'
                f'<testsuite name="pytest" errors="0" failures="0" skipped="0">{cases}'
                "</testsuite></testsuites>\n",
            )

    def put(self, rel: str, text: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def edit(self, rel: str, old: str, new: str) -> None:
        path = self.root / rel
        text = path.read_text(encoding="utf-8")
        assert old in text, f"{old!r} not found in {rel}"
        path.write_text(text.replace(old, new), encoding="utf-8")

    def remove_test(self, name: str, also_from_sources: bool = True) -> None:
        """Delete one C++ test from the sources and from every JUnit file."""
        if also_from_sources:
            path = self.root / CPP
            text = path.read_text(encoding="utf-8")
            text, count = re.subn(rf'TEST_CASE\("{re.escape(name)}"\) \{{\}}\n\n?', "", text)
            assert count == 1
            path.write_text(text, encoding="utf-8")
        for xml in (self.root / "junit").rglob("ctest-debug.xml"):
            self.remove_from_junit(xml, name)

    @staticmethod
    def remove_from_junit(xml: Path, name: str) -> None:
        text = xml.read_text(encoding="utf-8")
        text, count = re.subn(
            rf'\t<testcase name="{re.escape(name)}".*?</testcase>\n', "", text, flags=re.DOTALL
        )
        assert count == 1
        xml.write_text(text, encoding="utf-8")

    def write_job_results(self, document: dict[str, Any]) -> None:
        self.job_results = self.root / "job-results.json"
        self.job_results.write_text(json.dumps(document), encoding="utf-8")

    def write_known(self, entries: list[dict[str, str]]) -> None:
        self.known = self.root / "known-gaps.json"
        self.known.write_text(
            json.dumps({"schema_version": 1, "known_gaps": entries}), encoding="utf-8"
        )

    def build(self, *extra: str) -> tuple[int, dict[str, Any]]:
        argv = [
            "--root",
            str(self.root),
            "--junit",
            str(self.root / "junit"),
            "--out",
            str(self.out),
            "--source-dir",
            "engine",
            "--source-dir",
            "compiler",
        ]
        if self.known:
            argv += ["--known-gaps", str(self.known)]
        if self.job_results:
            argv += ["--job-results", str(self.job_results)]
        code = run([*argv, *extra])
        matrix: dict[str, Any] = json.loads((self.out / "traceability.json").read_text("utf-8"))
        return code, matrix


@pytest.fixture
def repo(tmp_path: Path) -> Repo:
    return Repo(tmp_path)


def gaps_of(matrix: dict[str, Any], phase: str) -> list[dict[str, Any]]:
    return [g for g in matrix["gaps"] if g["phase"] == phase]


def test_v1_t1_fixture_without_edits_has_no_gaps(repo: Repo) -> None:
    """V1-T1: the unmodified fixture has no gap (the baseline the other cases edit)."""
    repo.write_job_results({"lint": {"result": "success"}})
    code, matrix = repo.build("--fail-on-gaps")
    assert code == 0
    assert matrix["schema_version"] == 1
    assert matrix["gaps"] == []
    by_id = {t["id"]: t for p in matrix["phases"] for t in p["tests"] + p["requirements"]}
    assert by_id["A1-T1"]["status"] == "pass"
    assert by_id["A1-T1"]["systems_passed"] == ["linux", "macos", "windows"]
    assert by_id["A1-R3"]["status"] == "pass"  # covered by the 'Covers:' annotation


# Covers: V1-R2
def test_v1_t1_a_removed_test_shows_up_as_a_gap(repo: Repo) -> None:
    """V1-T1: removing a test (sources and results) reports its ID as missing: one gap."""
    repo.remove_test("A1-T2 bandwidth rejections at three sample rates")
    code, matrix = repo.build("--fail-on-gaps")
    a1 = gaps_of(matrix, "A1")
    assert [(g["id"], g["kind"]) for g in a1] == [("A1-T2", "test_missing")]
    assert a1[0]["enforced"] is True  # A1 is complete
    phase = next(p for p in matrix["phases"] if p["id"] == "A1")
    test = next(t for t in phase["tests"] if t["id"] == "A1-T2")
    assert test["status"] == "missing"
    assert phase["summary"]["tests_missing"] == 1
    assert phase["summary"]["gaps"] == 1
    assert code == 1


def test_v1_t1_a_failing_result_is_a_gap(repo: Repo) -> None:
    """V1-T1: a failing JUnit case shows as fail, in the job that ran it."""
    repo.edit(
        "junit/junit-build-linux-gcc/ctest-debug.xml",
        'name="A1-T1 coefficients agree with the reference" '
        'classname="A1-T1 coefficients agree with the reference" time="0.001" status="run">',
        'name="A1-T1 coefficients agree with the reference" '
        'classname="A1-T1 coefficients agree with the reference" time="0.001" status="fail">'
        '<failure message="Failed"/>',
    )
    code, matrix = repo.build("--fail-on-gaps")
    a1 = gaps_of(matrix, "A1")
    # A1-T1 also carries 'Covers: A1-R3', so that requirement fails with it.
    assert [(g["id"], g["kind"]) for g in a1] == [
        ("A1-R3", "requirement_failing"),
        ("A1-T1", "test_failing"),
    ]
    assert a1[1]["detail"] == "a carrying test or step failed (jobs: junit-build-linux-gcc)"
    assert code == 1


def test_v1_t1_a_test_that_ran_only_on_linux_is_not_run_on_all_systems(repo: Repo) -> None:
    """V1-T1: a test with results on Linux only is reported as not run on all systems."""
    name = "A1-T1 coefficients agree with the reference"
    repo.remove_from_junit(repo.root / "junit/junit-build-macos-arm64/ctest-debug.xml", name)
    repo.remove_from_junit(repo.root / "junit/junit-build-windows-msvc/ctest-debug.xml", name)
    _, matrix = repo.build()
    a1 = gaps_of(matrix, "A1")
    assert [(g["id"], g["kind"]) for g in a1] == [
        ("A1-R3", "requirement_not_run_all_systems"),  # A1-T1 also carries 'Covers: A1-R3'
        ("A1-T1", "test_not_run_all_systems"),
    ]
    assert "macos" in a1[1]["detail"] and "windows" in a1[1]["detail"]
    # The local-run flag accepts Linux alone.
    _, local = repo.build("--systems", "linux")
    assert gaps_of(local, "A1") == []


def test_v1_t1_an_uncovered_requirement_is_a_gap(repo: Repo) -> None:
    """V1-T1: a requirement no test names or annotates is uncovered."""
    repo.remove_test("A1-R2 bandwidth rule rejects high modes")
    _, matrix = repo.build()
    a1 = gaps_of(matrix, "A1")
    assert [(g["id"], g["kind"]) for g in a1] == [("A1-R2", "requirement_uncovered")]
    phase = next(p for p in matrix["phases"] if p["id"] == "A1")
    assert next(r for r in phase["requirements"] if r["id"] == "A1-R2")["state"] == "uncovered"


def test_v1_t1_a_covers_annotation_counts_and_its_removal_is_a_gap(repo: Repo) -> None:
    """V1-T1: the 'Covers:' comment above a test is what covers A1-R3."""
    repo.edit(CPP, "// Covers: A1-R3\n", "")
    _, matrix = repo.build()
    assert [(g["id"], g["kind"]) for g in gaps_of(matrix, "A1")] == [
        ("A1-R3", "requirement_uncovered")
    ]


def test_v1_t1_an_oracle_required_by_a_passed_gate_must_pass(repo: Repo) -> None:
    """V1-T1: ORC-01 missing after gate G1 passed is a gap, and G1 lists it as missing."""
    repo.edit(
        "docs/status/phase-status.md",
        "| G1 | Fixture gate | not passed |",
        "| G1 | Fixture gate | passed |",
    )
    repo.remove_test("ORC-01 oscillator accuracy")
    code, matrix = repo.build("--fail-on-gaps")
    kinds = sorted((g["id"], g["kind"]) for g in gaps_of(matrix, "A1"))
    assert ("ORC-01", "oracle_gate") in kinds
    assert ("ORC-01", "oracle_missing") in kinds  # named by the A1 card
    assert ("ORC-01", "oracle_inconsistent") in kinds  # registry says passing
    gate = next(g for g in matrix["gates"] if g["id"] == "G1")
    assert gate["oracles_missing"] == ["ORC-01"]
    assert code == 1


def test_v1_t1_an_oracle_gap_before_the_gate_passes_is_listed_but_not_enforced(
    repo: Repo,
) -> None:
    """V1-T1: before the gate passes, a missing ORC row is listed in the gate summary only."""
    repo.edit(
        "engine/tests/oracles.md", "| passing | osc_tests.cpp |", "| implemented | osc_tests.cpp |"
    )
    repo.remove_test("ORC-01 oscillator accuracy")
    _, matrix = repo.build()
    assert all(g["kind"] != "oracle_gate" for g in matrix["gaps"])
    gate = next(g for g in matrix["gates"] if g["id"] == "G1")
    assert gate["oracles_missing"] == ["ORC-01"]


def test_v1_t1_a_gap_in_a_phase_in_progress_is_not_enforced(repo: Repo) -> None:
    """V1-T1: the gap is listed for A2 (in progress) but the command line still exits 0."""
    path = repo.root / "compiler/tests/test_a2_fixture.py"
    text = path.read_text(encoding="utf-8")
    text = text.replace("def test_a2_t1_wound_string_mass", "def test_other_wound")
    path.write_text(text.replace('"""A2-T1: wound', '"""Other: wound'), encoding="utf-8")
    for system in ("linux", "macos", "windows"):
        xml = repo.root / f"junit/junit-python-{system}/pytest.xml"
        case = (
            '<testcase classname="compiler.tests.test_a2_fixture" '
            'name="test_a2_t1_wound_string_mass" time="0.000" />'
        )
        xml.write_text(xml.read_text(encoding="utf-8").replace(case, ""), encoding="utf-8")
    repo.write_job_results({"lint": {"result": "success"}})
    code, matrix = repo.build("--fail-on-gaps")
    a2 = gaps_of(matrix, "A2")
    assert [(g["id"], g["kind"], g["enforced"]) for g in a2] == [("A2-T1", "test_missing", False)]
    assert code == 0


def test_v1_t1_the_allow_list_accepts_a_known_gap(repo: Repo) -> None:
    """V1-T1: an allow-listed gap stays visible but no longer fails; a stale entry is reported."""
    repo.remove_test("A1-T2 bandwidth rejections at three sample rates")
    repo.write_known(
        [
            {"id": "A1-T2", "phase": "A1", "reason": "test moves to A3", "owner": "A3"},
            {"id": "A1-T1", "phase": "A1", "reason": "stale", "owner": "A1"},
        ]
    )
    code, matrix = repo.build("--fail-on-gaps")
    assert code == 0
    gap = gaps_of(matrix, "A1")[0]
    assert gap["known"] is True and gap["enforced"] is False
    assert [k["id"] for k in matrix["stale_known_gaps"]] == ["A1-T1"]
    assert matrix["totals"]["gaps_enforced"] == 0


def test_v1_t1_a_step_annotation_takes_its_result_from_the_job(repo: Repo) -> None:
    """V1-T1: 'Covers:' on a workflow step passes, fails or stays unverified with the job."""

    def state(document: dict[str, Any] | None) -> tuple[str, bool]:
        if document is not None:
            repo.write_job_results(document)
        _, matrix = repo.build()
        phase = next(p for p in matrix["phases"] if p["id"] == "A2")
        req = next(r for r in phase["requirements"] if r["id"] == "A2-R2")
        assert [e["kind"] for e in req["evidence"]] == ["step"]
        return req["status"], req["unverified"]

    assert state(None) == ("pass", True)  # no job results: covered but flagged
    assert state({"lint": {"result": "success"}}) == ("pass", False)
    assert state({"lint": {"result": "failure"}}) == ("fail", False)
    # A skipped job verified nothing: the annotation is a carrier that did not run.
    assert state({"lint": {"result": "skipped"}}) == ("not_run_all_systems", False)


# Covers: V1-R2
def test_v1_t1_outputs_are_self_contained(repo: Repo) -> None:
    """V1-T1: traceability.html has no script and no external reference; JSON has the version."""
    repo.remove_test("A1-T2 bandwidth rejections at three sample rates")
    repo.build()
    html = (repo.out / "traceability.html").read_text(encoding="utf-8")
    assert "<script" not in html
    assert not re.search(r"""(?:src|href)=["']https?://""", html)
    assert "A1-T2" in html and "MISSING" in html
    assert json.loads((repo.out / "traceability.json").read_text("utf-8"))["schema_version"] == 1
    assert (repo.out / "summary.md").read_text("utf-8").startswith("### Traceability")
