# SPDX-License-Identifier: MIT
"""V1-R3: every ORC row is shown with its registry status and CI state, and disagreements are
gaps; the gate summary lists the rows still missing. The script never edits the registry."""

from __future__ import annotations

import sys
from pathlib import Path

from test_v1_t1 import Repo, gaps_of

TRACE = Path(__file__).resolve().parents[1] / "trace"
sys.path.insert(0, str(TRACE))

from trace_matrix.plan import parse_oracle_registry  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = "engine/tests/oracles.md"
ORC_TEST = "ORC-01 oscillator accuracy"


def row(matrix: dict[str, object], orc: str) -> dict[str, object]:
    rows = matrix["oracles"]
    assert isinstance(rows, list)
    return next(r for r in rows if r["id"] == orc)


def test_v1_r3_the_real_registry_has_nineteen_rows() -> None:
    """V1-R3: engine/tests/oracles.md parses into ORC-01 to ORC-19 with owner, gate and status."""
    rows = parse_oracle_registry((ROOT / REGISTRY).read_text(encoding="utf-8"))
    assert [r.id for r in rows] == [f"ORC-{n:02d}" for n in range(1, 20)]
    assert next(r for r in rows if r.id == "ORC-17").owning_phases == ("C3", "C7")
    assert next(r for r in rows if r.id == "ORC-10").gate == "G6"


def test_v1_r3_each_row_shows_registry_status_and_ci_state(tmp_path: Path) -> None:
    """V1-R3: ORC-01 passes in CI and is 'passing' in the registry; ORC-02 has no test."""
    repo = Repo(tmp_path)
    _, matrix = repo.build()
    first = row(matrix, "ORC-01")
    assert first["registry_status"] == "passing"
    assert first["ci"]["status"] == "pass"  # type: ignore[index]
    assert first["inconsistencies"] == []
    second = row(matrix, "ORC-02")
    assert second["registry_status"] == "not implemented"
    assert second["ci"]["status"] == "missing"  # type: ignore[index]
    assert second["inconsistencies"] == []


def test_v1_r3_registry_passing_but_ci_not_is_inconsistent(tmp_path: Path) -> None:
    """V1-R3: the registry says passing and the test is gone: inconsistency, and a gap."""
    repo = Repo(tmp_path)
    repo.remove_test(ORC_TEST)
    _, matrix = repo.build()
    assert row(matrix, "ORC-01")["inconsistencies"] == [
        "registry says passing, CI status is missing"
    ]
    assert ("ORC-01", "oracle_inconsistent") in [
        (g["id"], g["kind"]) for g in gaps_of(matrix, "A1")
    ]


def test_v1_r3_ci_passing_but_registry_not_implemented_is_inconsistent(tmp_path: Path) -> None:
    """V1-R3: the test passes in CI and the registry still says not implemented."""
    repo = Repo(tmp_path)
    repo.edit(REGISTRY, "| passing | osc_tests.cpp |", "| not implemented | - |")
    _, matrix = repo.build()
    assert row(matrix, "ORC-01")["inconsistencies"] == [
        "CI passes but the registry says not implemented"
    ]


def test_v1_r3_an_oracle_test_without_the_oracle_label_is_inconsistent(tmp_path: Path) -> None:
    """V1-R3: a ctest ORC test must carry the 'oracle' label (docs/dev/testing.md)."""
    repo = Repo(tmp_path)
    repo.edit(
        "junit/junit-build-linux-gcc/ctest-debug.xml",
        'name="cmake_labels" value="unit;oracle"',
        'name="cmake_labels" value="unit"',
    )
    _, matrix = repo.build()
    problems = row(matrix, "ORC-01")["inconsistencies"]
    assert isinstance(problems, list) and len(problems) == 1
    assert "no 'oracle'" in problems[0]


def test_v1_r3_the_script_never_edits_the_registry(tmp_path: Path) -> None:
    """V1-R3: the registry file is byte-identical after a run."""
    repo = Repo(tmp_path)
    before = (repo.root / REGISTRY).read_bytes()
    repo.remove_test(ORC_TEST)
    repo.build()
    assert (repo.root / REGISTRY).read_bytes() == before
