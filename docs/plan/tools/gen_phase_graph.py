#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate docs/plan/phases.yaml and generate:

- docs/plan/phase-graph.md        Mermaid dependency graph (phases, gates, decisions)
- docs/status/phase-status.md     initial status tracker (only if it does not exist, or with --reset-status)

Usage:  python3 docs/plan/tools/gen_phase_graph.py [--check] [--reset-status]

--check   validate only; exit 1 on any inconsistency (use in CI or before committing plan edits)
"""
from __future__ import annotations

import sys
from pathlib import Path

try:
    import yaml  # PyYAML
except ImportError:  # pragma: no cover
    sys.exit("PyYAML is required: pip install pyyaml")

ROOT = Path(__file__).resolve().parents[3]
PHASES = ROOT / "docs/plan/phases.yaml"
GRAPH = ROOT / "docs/plan/phase-graph.md"
STATUS = ROOT / "docs/status/phase-status.md"


def load():
    data = yaml.safe_load(PHASES.read_text(encoding="utf-8"))
    phases = data["phases"]
    gates = data["gates"]
    decisions = data["decisions"]
    return data, phases, gates, decisions


def validate(phases, gates, decisions) -> list[str]:
    errors: list[str] = []
    ids = [p["id"] for p in phases]
    known = set(ids) | set(gates) | set(decisions)
    if len(ids) != len(set(ids)):
        errors.append("duplicate phase IDs")
    for p in phases:
        for d in p.get("depends_on", []):
            if d not in known:
                errors.append(f"{p['id']}: unknown dependency {d}")
        if not p.get("tests"):
            errors.append(f"{p['id']}: no tests listed")
    for g, spec in gates.items():
        for r in spec.get("requires", []):
            if r not in known:
                errors.append(f"{g}: unknown requirement {r}")
    # cycle check over phases and gates (decisions are leaves)
    graph = {p["id"]: [d for d in p.get("depends_on", []) if d not in decisions] for p in phases}
    for g, spec in gates.items():
        graph[g] = [r for r in spec.get("requires", []) if r not in decisions]
        graph[g] += [r for r in spec.get("instruments", [])]
    state: dict[str, int] = {}

    def visit(n: str, stack: list[str]) -> None:
        if state.get(n) == 1:
            errors.append("cycle: " + " -> ".join(stack + [n]))
            return
        if state.get(n) == 2:
            return
        state[n] = 1
        for m in graph.get(n, []):
            visit(m, stack + [n])
        state[n] = 2

    for n in graph:
        visit(n, [])
    return errors


def node_id(x: str) -> str:
    return x.replace(".", "_").replace("-", "_")


def mermaid(data, phases, gates, decisions) -> str:
    lines = ["flowchart LR"]
    for tkey, tname in data["tracks"].items():
        lines.append(f'  subgraph T_{node_id(tkey)}["Track {tkey}: {tname}"]')
        for p in phases:
            if p["track"] == tkey:
                lines.append(f'    {node_id(p["id"])}["{p["id"]} {p["name"]}"]')
        lines.append("  end")
    for g, spec in gates.items():
        lines.append(f'  {node_id(g)}{{{{"{g} {spec["name"]}"}}}}')
    used_decisions = sorted({d for p in phases for d in p.get("depends_on", []) if d in decisions}
                            | {d for s in gates.values() for d in s.get("decisions", [])})
    for d in used_decisions:
        lines.append(f'  {node_id(d)}[/"{d} {decisions[d]["title"]}"/]')
    for p in phases:
        for d in p.get("depends_on", []):
            style = "-.->" if d in decisions else "-->"
            lines.append(f"  {node_id(d)} {style} {node_id(p['id'])}")
    for g, spec in gates.items():
        for r in spec.get("requires", []):
            lines.append(f"  {node_id(r)} --> {node_id(g)}")
        for r in spec.get("instruments", []):
            lines.append(f"  {node_id(r)} --> {node_id(g)}")
        for d in spec.get("decisions", []):
            lines.append(f"  {node_id(d)} -.-> {node_id(g)}")
    lines.append("  classDef gate fill:#dbeafe,stroke:#1d4ed8;")
    lines.append("  classDef decision fill:#fef3c7,stroke:#b45309;")
    lines.append("  class " + ",".join(node_id(g) for g in gates) + " gate;")
    if used_decisions:
        lines.append("  class " + ",".join(node_id(d) for d in used_decisions) + " decision;")
    return "\n".join(lines)


def write_graph(data, phases, gates, decisions) -> None:
    body = mermaid(data, phases, gates, decisions)
    GRAPH.write_text(
        "# Phase dependency graph\n\n"
        "Generated from [phases.yaml](phases.yaml) by `docs/plan/tools/gen_phase_graph.py`. Do not edit by hand.\n\n"
        "Solid arrows: phase or gate dependencies. Dotted arrows: human decisions (see the Decisions section of "
        "[phasing-plan.md](phasing-plan.md)). Hexagons are gates.\n\n"
        "```mermaid\n" + body + "\n```\n",
        encoding="utf-8",
    )


def write_status(phases, gates) -> None:
    rows = ["| Phase | Name | Track | Status | Branch / PR | Gate report | Notes |",
            "| --- | --- | --- | --- | --- | --- | --- |"]
    for p in phases:
        deps = p.get("depends_on", [])
        ready = all(d == "D-2" for d in deps)  # only D-2 is answered at plan hand-off
        status = "ready" if ready else "not started"
        rows.append(f"| {p['id']} | {p['name']} | {p['track']} | {status} |  |  |  |")
    grows = ["| Gate | Name | Status | Report |", "| --- | --- | --- | --- |"]
    for g, spec in gates.items():
        grows.append(f"| {g} | {spec['name']} | not passed |  |")
    STATUS.write_text(
        "# Phase status\n\n"
        "Maintained by the orchestrator (Opus). Status values: not started, ready, in progress, in review, "
        "complete, blocked, skipped. A phase is `ready` when every entry of its `depends_on` in "
        "[phases.yaml](../plan/phases.yaml) is complete, passed or answered.\n\n"
        "## Phases\n\n" + "\n".join(rows) + "\n\n## Gates\n\n" + "\n".join(grows) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    data, phases, gates, decisions = load()
    errors = validate(phases, gates, decisions)
    if errors:
        print("phases.yaml is inconsistent:")
        for e in errors:
            print("  -", e)
        return 1
    print(f"phases.yaml OK: {len(phases)} phases, {len(gates)} gates, {len(decisions)} decisions")
    if "--check" in sys.argv:
        return 0
    write_graph(data, phases, gates, decisions)
    if "--reset-status" in sys.argv or not STATUS.exists():
        write_status(phases, gates)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
