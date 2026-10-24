# SPDX-License-Identifier: MIT
"""V1-T2: the matrix of the repository has no gaps for completed phases.

The authoritative check is the `traceability` job, which reads the real results of the run. This
test checks the half that does not depend on a run: every requirement, test and oracle of every
phase marked `complete` in docs/status/phase-status.md is linked to a test or annotated step in the
code (by name or by `Covers:`). It gives every test found in the sources a passing result on every
system, builds the matrix with the real plan, status, registry and allow-list, and fails naming each
enforced gap. A phase marked complete without its links, or an allow-list entry that no longer
matches a gap, fails here and not only in CI.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

TRACE = Path(__file__).resolve().parents[1] / "trace"
sys.path.insert(0, str(TRACE))

from trace_matrix.cli import DEFAULT_SOURCE_DIRS, load_known_gaps  # noqa: E402
from trace_matrix.ids import SYSTEMS  # noqa: E402
from trace_matrix.matrix import Inputs, build_matrix  # noqa: E402
from trace_matrix.plan import (  # noqa: E402
    parse_gates,
    parse_oracle_registry,
    parse_phase_cards,
    parse_phase_status,
)
from trace_matrix.results import TestResult  # noqa: E402
from trace_matrix.sources import scan_sources, scan_workflows  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def real_matrix() -> dict[str, Any]:
    plan_text = (ROOT / "docs/plan/phasing-plan.md").read_text(encoding="utf-8")
    phase_status, gate_status = parse_phase_status(
        (ROOT / "docs/status/phase-status.md").read_text(encoding="utf-8")
    )
    sources = scan_sources(ROOT, DEFAULT_SOURCE_DIRS)
    # Every test found in the sources "passes" on every system: only the links are checked.
    results = [
        TestResult(t.name, "pass", f"synthetic-{system}", system, t.key[0], ("oracle",), t.file)
        for t in sources
        for system in SYSTEMS
    ]
    inputs = Inputs(
        cards=parse_phase_cards(plan_text),
        gates=parse_gates(plan_text),
        phase_status=phase_status,
        gate_status=gate_status,
        oracles=parse_oracle_registry((ROOT / "engine/tests/oracles.md").read_text("utf-8")),
        source_tests=sources,
        step_annotations=scan_workflows(ROOT),
        results=results,
        known_gaps=load_known_gaps(ROOT / "tools/trace/known-gaps.json"),
    )
    return build_matrix(inputs)


def test_v1_t2_completed_phases_are_linked_to_tests() -> None:
    """V1-T2: no enforced gap for completed phases when every test in the sources passes."""
    matrix = real_matrix()
    enforced = [g for g in matrix["gaps"] if g["enforced"]]
    listing = "\n".join(f"{g['id']} ({g['phase']}, {g['kind']}): {g['detail']}" for g in enforced)
    assert not enforced, f"gaps in completed phases:\n{listing}"
    complete = [p["id"] for p in matrix["phases"] if p["status"] == "complete"]
    assert {"P0.1", "P0.2", "P0.3", "P0.4"} <= set(complete)


def test_v1_t2_the_allow_list_has_no_stale_entry() -> None:
    """V1-T2: every allow-list entry still matches a gap (a closed gap is removed from the list)."""
    matrix = real_matrix()
    assert matrix["stale_known_gaps"] == []


def test_v1_t2_gate_owned_entries_are_evidenced_by_a_passed_gate_report() -> None:
    """V1-T2: an entry owned by `gate G<n>` needs a passed gate G<n> whose report names the ID."""
    _, gate_status = parse_phase_status(
        (ROOT / "docs/status/phase-status.md").read_text(encoding="utf-8")
    )
    for entry in load_known_gaps(ROOT / "tools/trace/known-gaps.json"):
        owner = entry.owner
        if not owner.startswith("gate "):
            continue
        gate = owner.removeprefix("gate ").strip()
        assert gate_status.get(gate) == "passed", f"{entry.id}: gate {gate} has not passed"
        report = ROOT / "docs" / "gates" / f"{gate}.md"
        assert entry.id in report.read_text(encoding="utf-8"), f"{entry.id} not in {report}"
