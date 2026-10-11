# SPDX-License-Identifier: MIT
"""Command line of the traceability matrix (V1-R1, V1-R4)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .ids import SYSTEMS
from .matrix import SCHEMA_VERSION, Inputs, KnownGap, build_matrix
from .plan import (
    PlanError,
    parse_gates,
    parse_oracle_registry,
    parse_phase_cards,
    parse_phase_status,
)
from .render import render_html, render_json, render_markdown
from .results import ResultsError, read_job_results, read_junit_dir
from .sources import scan_sources, scan_workflows

DEFAULT_SOURCE_DIRS = ["engine", "tools", "reference", "compiler", "plugin", "bindings"]


def load_known_gaps(path: Path) -> list[KnownGap]:
    """Read the allow-list; raises ValueError on a malformed file."""
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ValueError(f"{path}: cannot read the allow-list: {exc}") from exc
    if not isinstance(document, dict) or document.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(f"{path}: expected an object with schema_version {SCHEMA_VERSION}")
    entries = document.get("known_gaps")
    if not isinstance(entries, list):
        raise ValueError(f"{path}: 'known_gaps' must be a list")
    known: list[KnownGap] = []
    for number, entry in enumerate(entries, start=1):
        if not isinstance(entry, dict):
            raise ValueError(f"{path}: entry {number} is not an object")
        for key in ("id", "phase", "reason", "owner"):
            if not isinstance(entry.get(key), str) or not entry[key].strip():
                raise ValueError(f"{path}: entry {number} needs a non-empty string '{key}'")
        kind = entry.get("kind", "")
        if not isinstance(kind, str):
            raise ValueError(f"{path}: entry {number}: 'kind' must be a string")
        known.append(KnownGap(entry["id"], entry["phase"], entry["reason"], entry["owner"], kind))
    return known


def build_parser() -> argparse.ArgumentParser:
    """The argument parser (documented in docs/dev/traceability.md)."""
    parser = argparse.ArgumentParser(
        description="Build the traceability matrix from the plan, the code and the test results."
    )
    parser.add_argument("--root", type=Path, default=Path("."), help="repository root (default .)")
    parser.add_argument("--plan", type=Path, default=Path("docs/plan/phasing-plan.md"))
    parser.add_argument("--status", type=Path, default=Path("docs/status/phase-status.md"))
    parser.add_argument("--oracles", type=Path, default=Path("engine/tests/oracles.md"))
    parser.add_argument(
        "--source-dir",
        action="append",
        dest="source_dirs",
        help=f"directory scanned for tests, relative to --root (repeatable; default "
        f"{' '.join(DEFAULT_SOURCE_DIRS)})",
    )
    parser.add_argument("--workflows", default=".github/workflows", help="workflow directory")
    parser.add_argument(
        "--junit",
        type=Path,
        required=True,
        help="directory of JUnit XML files; one sub-directory per workflow artifact (junit-*)",
    )
    parser.add_argument(
        "--job-results",
        type=Path,
        help="JSON of the workflow's job conclusions (toJSON(needs)); needed to verify "
        "'Covers:' annotations on workflow steps",
    )
    parser.add_argument(
        "--job-results-workflow",
        default="pr.yml",
        help="workflow file the job results belong to (default pr.yml)",
    )
    parser.add_argument(
        "--known-gaps", type=Path, help="allow-list JSON (tools/trace/known-gaps.json)"
    )
    parser.add_argument(
        "--systems",
        default=",".join(SYSTEMS),
        help="comma-separated systems a test must pass on (default linux,macos,windows); "
        "use 'linux' for a local run",
    )
    parser.add_argument(
        "--default-system",
        default="linux",
        help="system assumed for an artifact whose name names none (default linux)",
    )
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    parser.add_argument(
        "--fail-on-gaps",
        action="store_true",
        help="exit 1 when a complete phase (or a passed gate's oracle) has a gap that is not "
        "on the allow-list",
    )
    return parser


def run(argv: list[str] | None = None) -> int:
    """Build the matrix; exit code 0, 1 (gaps with --fail-on-gaps) or 2 (input error)."""
    args = build_parser().parse_args(argv)
    root: Path = args.root

    def under_root(path: Path) -> Path:
        return path if path.is_absolute() else root / path

    systems = tuple(s.strip() for s in args.systems.split(",") if s.strip())
    unknown = [s for s in systems if s not in SYSTEMS]
    if not systems or unknown:
        print(f"error: --systems must name some of {', '.join(SYSTEMS)}", file=sys.stderr)
        return 2
    try:
        plan_text = under_root(args.plan).read_text(encoding="utf-8")
        cards = parse_phase_cards(plan_text)
        gates = parse_gates(plan_text)
        phase_status, gate_status = parse_phase_status(
            under_root(args.status).read_text(encoding="utf-8")
        )
        oracles = parse_oracle_registry(under_root(args.oracles).read_text(encoding="utf-8"))
        results = read_junit_dir(under_root(args.junit), args.default_system)
        job_results = read_job_results(args.job_results) if args.job_results else None
        known = load_known_gaps(args.known_gaps) if args.known_gaps else []
    except (PlanError, ResultsError, ValueError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    source_dirs = args.source_dirs or DEFAULT_SOURCE_DIRS
    inputs = Inputs(
        cards=cards,
        gates=gates,
        phase_status=phase_status,
        gate_status=gate_status,
        oracles=oracles,
        source_tests=scan_sources(root, source_dirs),
        step_annotations=scan_workflows(root, args.workflows),
        results=results,
        job_results=job_results,
        job_results_workflow=args.job_results_workflow,
        required_systems=systems,
        known_gaps=known,
    )
    matrix = build_matrix(inputs)
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    (out / "traceability.json").write_text(render_json(matrix), encoding="utf-8")
    (out / "traceability.html").write_text(render_html(matrix), encoding="utf-8")
    summary = render_markdown(matrix)
    (out / "summary.md").write_text(summary, encoding="utf-8")
    sys.stdout.write(summary)
    enforced = matrix["totals"]["gaps_enforced"]
    if args.fail_on_gaps and enforced:
        print(f"error: {enforced} enforced gap(s); see the list above", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    """Entry point."""
    return run()
