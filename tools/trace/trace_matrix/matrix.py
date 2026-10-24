# SPDX-License-Identifier: MIT
"""The traceability matrix: join plan IDs, code and results, and compute the gaps (V1-R2, V1-R3).

Definitions (docs/dev/traceability.md):

* An ID (requirement, test or oracle) is **pass** when at least one test carries it (by name prefix
  or ``Covers:``), no carrying result failed in any job, and a passing result exists on every
  required system. A ``Covers:`` annotation on a workflow step counts through the conclusion of its
  job; with no conclusion at hand it is *unverified* (counted as covered, flagged in the matrix).
* It is **fail** when any carrying result failed, **missing** when nothing carries it, and
  **not_run_all_systems** otherwise.
* A **gap** is an uncovered requirement, a failing or missing test ID, a test ID that did not pass
  on every required system, an oracle row named by a card or required by a passed gate that is not
  passing, or a registry row that disagrees with CI.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .ids import PHASE_SYSTEMS, SYSTEMS
from .plan import Gate, OracleRow, PhaseCard
from .results import TestResult
from .sources import SourceTest, StepAnnotation

SCHEMA_VERSION = 1


@dataclass
class KnownGap:
    """An allow-list entry (tools/trace/known-gaps.json)."""

    id: str
    phase: str
    reason: str
    owner: str
    kind: str = ""


@dataclass
class Gap:
    """A gap of the matrix."""

    id: str
    kind: str
    phase: str
    detail: str
    enforced: bool = False
    known: bool = False


@dataclass
class Evidence:
    """A test or workflow step that carries an ID."""

    kind: str  # 'test' | 'step'
    name: str
    where: str
    outcomes: dict[str, int]
    systems: list[str]
    jobs: list[str]


@dataclass
class IdStatus:
    """The state of one requirement, test or oracle ID."""

    id: str
    status: str  # pass | fail | missing | not_run_all_systems
    unverified: bool
    systems_passed: list[str]
    systems_missing: list[str]
    jobs: list[str]
    jobs_failed: list[str] = field(default_factory=list)
    evidence: list[Evidence] = field(default_factory=list)


@dataclass
class _Entry:
    """A test known from the sources and/or the results."""

    key: tuple[str, str]
    name: str
    source: SourceTest | None
    ids: list[str]
    results: list[TestResult] = field(default_factory=list)


@dataclass
class _Step:
    annotation: StepAnnotation
    conclusion: str  # pass | fail | skipped | unverified


@dataclass
class Inputs:
    """Everything the matrix is computed from."""

    cards: list[PhaseCard]
    gates: list[Gate]
    phase_status: dict[str, str]
    gate_status: dict[str, str]
    oracles: list[OracleRow]
    source_tests: list[SourceTest]
    step_annotations: list[StepAnnotation]
    results: list[TestResult]
    job_results: dict[str, str] | None = None
    job_results_workflow: str = "pr.yml"
    required_systems: tuple[str, ...] = SYSTEMS
    known_gaps: list[KnownGap] = field(default_factory=list)


class Evaluator:
    """Computes the state of every ID from the sources, results and job conclusions."""

    def __init__(self, inputs: Inputs) -> None:
        self.required = tuple(inputs.required_systems)
        self.entries: dict[tuple[str, str], _Entry] = {}
        for test in inputs.source_tests:
            entry = self.entries.get(test.key)
            if entry is None:
                self.entries[test.key] = _Entry(test.key, test.name, test, list(test.ids))
            else:
                # The same name in two files: the ids of both apply.
                for item in test.ids:
                    if item not in entry.ids:
                        entry.ids.append(item)
        self.unmapped_results: list[str] = []
        for result in inputs.results:
            found = self.entries.get(result.key)
            if found is None:
                own = result.own_id()
                found = _Entry(result.key, result.name, None, [own] if own else [])
                self.entries[result.key] = found
            found.results.append(result)
        self.by_id: dict[str, list[_Entry]] = {}
        for entry in self.entries.values():
            for item in entry.ids:
                self.by_id.setdefault(item, []).append(entry)
        self.steps: dict[str, list[_Step]] = {}
        for annotation in inputs.step_annotations:
            conclusion = self._conclusion(inputs, annotation)
            for item in annotation.ids:
                self.steps.setdefault(item, []).append(_Step(annotation, conclusion))
        self._cache: dict[str, IdStatus] = {}

    @staticmethod
    def _conclusion(inputs: Inputs, annotation: StepAnnotation) -> str:
        if inputs.job_results is None or annotation.workflow != inputs.job_results_workflow:
            return "unverified"
        result = inputs.job_results.get(annotation.job)
        if result is None:
            return "unverified"
        if result == "success":
            return "pass"
        if result == "failure":
            return "fail"
        return "skipped"  # cancelled or skipped: the step did not verify anything

    def tests_without_id(self) -> list[str]:
        """Names of tests that ran but carry no ID."""
        return sorted(e.name for e in self.entries.values() if e.results and not e.ids)

    def known_ids(self) -> set[str]:
        """Every ID some test or step carries."""
        return set(self.by_id) | set(self.steps)

    def status(self, item_id: str, phase: str | None = None) -> IdStatus:
        """State of one ID; ``phase`` narrows the required systems for a Linux-only phase."""
        cache_key = f"{phase or ''}/{item_id}"
        cached = self._cache.get(cache_key)
        if cached is not None:
            return cached
        entries = self.by_id.get(item_id, [])
        steps = self.steps.get(item_id, [])
        results = [r for e in entries for r in e.results]
        evidence: list[Evidence] = []
        for entry in entries:
            outcomes: dict[str, int] = {}
            for r in entry.results:
                outcomes[r.outcome] = outcomes.get(r.outcome, 0) + 1
            where = f"{entry.source.file}:{entry.source.line}" if entry.source else "(results only)"
            systems = sorted({r.system for r in entry.results if r.outcome == "pass" and r.system})
            jobs = sorted({r.job for r in entry.results})
            evidence.append(Evidence("test", entry.name, where, outcomes, systems, jobs))
        for step in steps:
            ann = step.annotation
            evidence.append(
                Evidence(
                    "step",
                    f"{ann.workflow} / {ann.job} / {ann.step}",
                    f"{ann.workflow}:{ann.line}",
                    {step.conclusion: 1},
                    [],
                    [ann.job],
                )
            )
        passed = sorted({r.system for r in results if r.outcome == "pass" and r.system})
        narrowed = PHASE_SYSTEMS.get(phase or "")
        required = (
            self.required if narrowed is None else [s for s in self.required if s in narrowed]
        )
        missing_systems = [s for s in required if s not in passed]
        failed = any(r.outcome == "fail" for r in results) or any(
            s.conclusion == "fail" for s in steps
        )
        step_pass = any(s.conclusion == "pass" for s in steps)
        step_unverified = any(s.conclusion == "unverified" for s in steps)
        unverified = False
        if not entries and not steps:
            status = "missing"
        elif failed:
            status = "fail"
        elif step_pass or not missing_systems:
            status = "pass"
        elif step_unverified:
            status = "pass"
            unverified = True
        else:
            status = "not_run_all_systems"
        jobs = sorted({r.job for r in results} | {s.annotation.job for s in steps})
        failed_jobs = sorted(
            {r.job for r in results if r.outcome == "fail"}
            | {s.annotation.job for s in steps if s.conclusion == "fail"}
        )
        result = IdStatus(
            item_id, status, unverified, passed, missing_systems, jobs, failed_jobs, evidence
        )
        self._cache[cache_key] = result
        return result

    def oracle_label_problems(self, item_id: str) -> list[str]:
        """ctest results of an oracle test that lack the 'oracle' label."""
        problems: list[str] = []
        for entry in self.by_id.get(item_id, []):
            for r in entry.results:
                if r.kind == "c" and r.labels and "oracle" not in r.labels:
                    problems.append(
                        f"{entry.name}: ctest labels {','.join(r.labels)} (no 'oracle')"
                    )
                    break
        return problems


_GAP_KIND = {
    "missing": "missing",
    "fail": "failing",
    "not_run_all_systems": "not_run_all_systems",
}


def build_matrix(inputs: Inputs) -> dict[str, Any]:
    """Compute the matrix as a JSON-serialisable dictionary (``traceability.json``)."""
    ev = Evaluator(inputs)
    gaps: list[Gap] = []
    seen_gaps: set[tuple[str, str, str]] = set()

    def add_gap(item_id: str, kind: str, phase: str, detail: str) -> None:
        key = (phase, item_id, kind)
        if key not in seen_gaps:
            seen_gaps.add(key)
            gaps.append(Gap(item_id, kind, phase, detail))

    def as_dict(status: IdStatus) -> dict[str, Any]:
        return {
            "status": status.status,
            "unverified": status.unverified,
            "systems_passed": status.systems_passed,
            "systems_missing": status.systems_missing,
            "jobs": status.jobs,
            "jobs_failed": status.jobs_failed,
            "evidence": [
                {
                    "kind": e.kind,
                    "name": e.name,
                    "where": e.where,
                    "outcomes": e.outcomes,
                    "systems": e.systems,
                    "jobs": e.jobs,
                }
                for e in status.evidence
            ],
        }

    phases: list[dict[str, Any]] = []
    plan_ids: set[str] = set()
    for card in inputs.cards:
        phase_status = inputs.phase_status.get(card.id, "unknown")
        requirements: list[dict[str, Any]] = []
        for req in card.requirements:
            plan_ids.add(req.id)
            st = ev.status(req.id, card.id)
            requirements.append(
                {
                    "id": req.id,
                    "text": req.text,
                    **as_dict(st),
                    "state": "uncovered" if st.status == "missing" else st.status,
                }
            )
            if st.status != "pass":
                kind = (
                    "requirement_uncovered"
                    if st.status == "missing"
                    else f"requirement_{_GAP_KIND[st.status]}"
                )
                add_gap(req.id, kind, card.id, _gap_detail(st))
        tests: list[dict[str, Any]] = []
        for test in card.tests:
            plan_ids.add(test.id)
            st = ev.status(test.id, card.id)
            tests.append({"id": test.id, "criterion": test.criterion, **as_dict(st)})
            if st.status != "pass":
                add_gap(test.id, f"test_{_GAP_KIND[st.status]}", card.id, _gap_detail(st))
        oracle_tests: list[dict[str, Any]] = []
        for orc in card.oracle_ids:
            plan_ids.add(orc)
            st = ev.status(orc, card.id)
            oracle_tests.append({"id": orc, **as_dict(st)})
            if st.status != "pass":
                add_gap(orc, f"oracle_{_GAP_KIND[st.status]}", card.id, _gap_detail(st))
        phases.append(
            {
                "id": card.id,
                "name": card.name,
                "status": phase_status,
                "requirements": requirements,
                "tests": tests,
                "oracles": oracle_tests,
            }
        )

    # Oracle registry (V1-R3).
    gate_by_id = {g.id: g for g in inputs.gates}
    oracle_rows: list[dict[str, Any]] = []
    for row in inputs.oracles:
        plan_ids.add(row.id)
        st = ev.status(row.id)
        problems: list[str] = []
        if row.status == "passing" and st.status != "pass":
            problems.append(f"registry says passing, CI status is {st.status}")
        if st.status == "pass" and row.status == "not implemented":
            problems.append("CI passes but the registry says not implemented")
        problems.extend(ev.oracle_label_problems(row.id))
        owners = list(row.owning_phases) or ["?"]
        for owner in owners:
            for problem in problems:
                add_gap(row.id, "oracle_inconsistent", owner, problem)
        gate = gate_by_id.get(row.gate)
        gate_passed = gate is not None and inputs.gate_status.get(gate.id) == "passed"
        required_by = gate.id if gate and row.id in gate.oracle_ids else ""
        if required_by and gate_passed and st.status != "pass":
            for owner in owners:
                add_gap(
                    row.id,
                    "oracle_gate",
                    owner,
                    f"required by passed gate {required_by}; {st.status}",
                )
        oracle_rows.append(
            {
                "id": row.id,
                "name": row.name,
                "owning_phases": list(row.owning_phases),
                "gate": row.gate,
                "registry_status": row.status,
                "registry_location": row.location,
                "required_by_gate": required_by,
                "ci": as_dict(st),
                "inconsistencies": problems,
            }
        )

    # Known-gap allow-list and enforcement.
    for gap in gaps:
        for known in inputs.known_gaps:
            if known.id == gap.id and known.phase == gap.phase and known.kind in ("", gap.kind):
                gap.known = True
        status = inputs.phase_status.get(gap.phase, "unknown")
        gap.enforced = (status == "complete" or gap.kind == "oracle_gate") and not gap.known
    stale = [
        k
        for k in inputs.known_gaps
        if not any(g.id == k.id and g.phase == k.phase and k.kind in ("", g.kind) for g in gaps)
    ]

    gap_dicts = [
        {
            "id": g.id,
            "kind": g.kind,
            "phase": g.phase,
            "detail": g.detail,
            "enforced": g.enforced,
            "known": g.known,
        }
        for g in gaps
    ]
    for phase in phases:
        mine = [g for g in gaps if g.phase == phase["id"]]
        reqs = phase["requirements"]
        tests = phase["tests"]
        orcs = phase["oracles"]
        phase["summary"] = {
            "requirements": len(reqs),
            "requirements_covered": sum(1 for r in reqs if r["status"] == "pass"),
            "tests": len(tests),
            "tests_pass": sum(1 for t in tests if t["status"] == "pass"),
            "tests_fail": sum(1 for t in tests if t["status"] == "fail"),
            "tests_missing": sum(1 for t in tests if t["status"] == "missing"),
            "tests_not_run_all_systems": sum(
                1 for t in tests if t["status"] == "not_run_all_systems"
            ),
            "oracles": len(orcs),
            "oracles_pass": sum(1 for o in orcs if o["status"] == "pass"),
            "gaps": len(mine),
            "gaps_known": sum(1 for g in mine if g.known),
            "gaps_enforced": sum(1 for g in mine if g.enforced),
        }

    phase_gap_counts = {p["id"]: p["summary"]["gaps"] - p["summary"]["gaps_known"] for p in phases}
    gate_rows: list[dict[str, Any]] = []
    orc_by_id = {o["id"]: o for o in oracle_rows}
    for gate in inputs.gates:
        gate_oracles = [
            {
                "id": orc,
                "ci_status": orc_by_id[orc]["ci"]["status"],
                "registry_status": orc_by_id[orc]["registry_status"],
            }
            for orc in gate.oracle_ids
            if orc in orc_by_id
        ]
        orc_missing = [o["id"] for o in gate_oracles if o["ci_status"] != "pass"]
        incomplete = [p for p in gate.requires_phases if inputs.phase_status.get(p) != "complete"]
        gate_rows.append(
            {
                "id": gate.id,
                "name": gate.name,
                "status": inputs.gate_status.get(gate.id, "unknown"),
                "requires_phases": gate.requires_phases,
                "requires_gates": gate.requires_gates,
                "phases_not_complete": incomplete,
                "phase_gaps": sum(phase_gap_counts.get(p, 0) for p in gate.requires_phases),
                "oracles": gate_oracles,
                "oracles_missing": orc_missing,
                "criteria": gate.criteria,
            }
        )

    unknown_ids = sorted(i for i in ev.known_ids() if i not in plan_ids)
    totals = {
        "gaps": len(gaps),
        "gaps_known": sum(1 for g in gaps if g.known),
        "gaps_enforced": sum(1 for g in gaps if g.enforced),
        "phases": len(phases),
        "phases_complete": sum(1 for p in phases if p["status"] == "complete"),
    }
    return {
        "schema_version": SCHEMA_VERSION,
        "required_systems": list(ev.required),
        "job_results_supplied": inputs.job_results is not None,
        "totals": totals,
        "phases": phases,
        "gates": gate_rows,
        "oracles": oracle_rows,
        "gaps": gap_dicts,
        "stale_known_gaps": [
            {"id": k.id, "phase": k.phase, "reason": k.reason, "owner": k.owner} for k in stale
        ],
        "unknown_ids": unknown_ids,
        "tests_without_id": ev.tests_without_id(),
    }


def _gap_detail(st: IdStatus) -> str:
    if st.status == "missing":
        return "no test or annotated step carries this ID"
    if st.status == "fail":
        return "a carrying test or step failed (jobs: " + ", ".join(st.jobs_failed) + ")"
    return "no passing result on: " + ", ".join(st.systems_missing)
