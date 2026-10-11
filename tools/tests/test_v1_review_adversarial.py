# SPDX-License-Identifier: MIT
"""V1-T1 (reviewer): adversarial cases for the traceability matrix.

Prefix boundary (A1-T10 is not A1-T1), a Covers: comment separated from a test by code, and a
test carried only by the results of one system.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from test_v1_t1 import CPP, Repo, gaps_of


def _statuses(matrix: dict[str, Any], ids: set[str]) -> dict[str, str]:
    return {
        t["id"]: t["status"]
        for p in matrix["phases"]
        for t in p["tests"] + p["requirements"]
        if t["id"] in ids
    }


def test_v1_t1_a_test_named_with_a_longer_number_does_not_cover_the_shorter_id(
    tmp_path: Path,
) -> None:
    """V1-T1: after A1-T1 is removed, a test 'A1-T10 ...' must not stand in for it."""
    repo = Repo(tmp_path)
    repo.remove_test("A1-T1 coefficients agree with the reference")
    repo.edit(CPP, 'TEST_CASE("ORC-01', 'TEST_CASE("A1-T10 decoy") {}\n\nTEST_CASE("ORC-01')
    for xml in (repo.root / "junit").rglob("ctest-debug.xml"):
        repo.edit(
            str(xml.relative_to(repo.root)),
            "</testsuite>",
            '\t<testcase name="A1-T10 decoy" classname="A1-T10 decoy" time="0.001" status="run"/>\n'
            "</testsuite>",
        )
    _, matrix = repo.build()
    kinds = {(g["id"], g["kind"]) for g in gaps_of(matrix, "A1")}
    assert ("A1-T1", "test_missing") in kinds


def test_v1_t1_a_covers_comment_separated_from_the_test_by_code_does_not_count(
    tmp_path: Path,
) -> None:
    """V1-T1: 'Covers:' must be in the comment block directly above the test."""
    repo = Repo(tmp_path)
    repo.edit(
        CPP,
        '// Covers: A1-R3\nTEST_CASE("A1-T1',
        '// Covers: A1-R3\nint unrelated = 0;\nTEST_CASE("A1-T1',
    )
    _, matrix = repo.build()
    assert _statuses(matrix, {"A1-R3"})["A1-R3"] == "missing"
