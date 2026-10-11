# SPDX-License-Identifier: MIT
"""Compare two benchmark JSON files and apply the 5% regression rule (P0.3-R6).

Usage: ``compare.py <baseline.json> <current.json>``

Exit status 1 when any benchmark's current median exceeds its baseline median by more than 5%
(strictly greater than 1.05 x baseline); 0 otherwise. Benchmarks present in only one file are
reported, not failed. Exit status 2 for unreadable or malformed input.

Baselines live in ``tools/bench/baselines/<machine-id>.json`` (none yet). Timings are comparable only
between runs on the same machine class (V2 and decision D-4 define the reference machines); the
script prints both files' machine and build information so a mismatch is visible.

Standard library only.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

THRESHOLD = 1.05  # current / baseline strictly above this fails
SCHEMA_VERSION = 1
RESULT_KEYS = ("name", "unit", "work_per_call", "repetitions", "inner_iterations", "median_ns", "min_ns", "iqr_ns")


class BenchFormatError(ValueError):
    """The file is not a benchmark JSON document of schema version 1."""


def validate(doc: Any, source: str = "benchmark file") -> dict[str, Any]:
    """Check the harness format (tools/bench/include/pme/bench/bench.hpp ``to_json``)."""
    if not isinstance(doc, dict):
        raise BenchFormatError(f"{source}: top level must be an object")
    if doc.get("schema_version") != SCHEMA_VERSION:
        raise BenchFormatError(f"{source}: unsupported schema_version {doc.get('schema_version')!r}")
    for section in ("machine", "build", "settings"):
        if not isinstance(doc.get(section), dict):
            raise BenchFormatError(f"{source}: missing object '{section}'")
    results = doc.get("results")
    if not isinstance(results, list):
        raise BenchFormatError(f"{source}: missing array 'results'")
    names = set()
    for i, entry in enumerate(results):
        if not isinstance(entry, dict):
            raise BenchFormatError(f"{source}: results[{i}] must be an object")
        for key in RESULT_KEYS:
            if key not in entry:
                raise BenchFormatError(f"{source}: results[{i}] lacks '{key}'")
        for key in ("median_ns", "min_ns", "iqr_ns"):
            if not isinstance(entry[key], int | float) or isinstance(entry[key], bool):
                raise BenchFormatError(f"{source}: results[{i}].{key} must be a number")
        if entry["name"] in names:
            raise BenchFormatError(f"{source}: duplicate benchmark '{entry['name']}'")
        names.add(entry["name"])
    return doc


def load(path: str | Path) -> dict[str, Any]:
    p = Path(path)
    try:
        doc = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise BenchFormatError(f"{p}: {exc}") from exc
    return validate(doc, str(p))


def compare(baseline: dict[str, Any], current: dict[str, Any]) -> tuple[list[str], list[str]]:
    """Return (regression lines, informational lines)."""
    base = {r["name"]: r for r in baseline["results"]}
    curr = {r["name"]: r for r in current["results"]}
    regressions: list[str] = []
    info: list[str] = []
    for name in sorted(base.keys() | curr.keys()):
        if name not in curr:
            info.append(f"only in baseline: {name}")
            continue
        if name not in base:
            info.append(f"only in current: {name}")
            continue
        b = float(base[name]["median_ns"])
        c = float(curr[name]["median_ns"])
        if b <= 0.0:
            info.append(f"{name}: baseline median is not positive ({b}); not compared")
            continue
        ratio = c / b
        line = f"{name}: median {b:.1f} ns -> {c:.1f} ns ({(ratio - 1.0) * 100.0:+.1f}%)"
        if ratio > THRESHOLD:
            regressions.append(f"REGRESSION {line}, above the {(THRESHOLD - 1.0) * 100.0:.0f}% limit")
        else:
            info.append(f"ok {line}")
    return regressions, info


def describe(doc: dict[str, Any], label: str) -> str:
    m, b = doc["machine"], doc["build"]
    return (
        f"{label}: {m.get('cpu_model', '?')} ({m.get('os', '?')}/{m.get('arch', '?')}), "
        f"{b.get('compiler', '?')} {b.get('build_type', '?')} preset {b.get('preset', '?')}"
    )


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        baseline, current = load(argv[1]), load(argv[2])
    except BenchFormatError as exc:
        print(f"compare.py: {exc}", file=sys.stderr)
        return 2
    print(describe(baseline, "baseline"))
    print(describe(current, "current "))
    if baseline["machine"].get("cpu_model") != current["machine"].get("cpu_model"):
        print("warning: the files come from different CPU models; timings are not comparable")
    regressions, info = compare(baseline, current)
    for line in info:
        print(line)
    for line in regressions:
        print(line)
    if regressions:
        print(f"FAIL: {len(regressions)} benchmark(s) regressed by more than 5%")
        return 1
    print("PASS: no benchmark regressed by more than 5%")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
