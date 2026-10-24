# SPDX-License-Identifier: MIT
"""P0.3-T3: the benchmark harness flags a synthetic 10% regression (5% rule, compare.py)."""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest

COMPARE = Path(__file__).resolve().parents[1] / "bench" / "compare.py"


def bench_doc(medians: dict[str, float], cpu: str = "Test CPU") -> dict[str, Any]:
    return {
        "schema_version": 1,
        "machine": {"os": "linux", "arch": "x86_64", "cpu_model": cpu, "logical_cores": 8},
        "build": {
            "compiler": "gcc",
            "compiler_version": "13",
            "build_type": "Release",
            "preset": "linux-release",
            "cxx_standard": 202002,
        },
        "settings": {"repetitions": 11, "warmup": 3, "min_repetition_ns": 1e6},
        "results": [
            {
                "name": name,
                "unit": "samples",
                "work_per_call": 4096,
                "repetitions": 11,
                "inner_iterations": 100,
                "median_ns": median,
                "min_ns": median * 0.98,
                "iqr_ns": median * 0.01,
                "throughput_per_s": 4096 * 1e9 / median,
            }
            for name, median in medians.items()
        ],
    }


def run_compare(
    tmp_path: Path, baseline: dict[str, Any], current: dict[str, Any]
) -> tuple[int, str]:
    b, c = tmp_path / "baseline.json", tmp_path / "current.json"
    b.write_text(json.dumps(baseline), encoding="utf-8")
    c.write_text(json.dumps(current), encoding="utf-8")
    proc = subprocess.run(
        [sys.executable, str(COMPARE), str(b), str(c)], capture_output=True, text=True, check=False
    )
    return proc.returncode, proc.stdout + proc.stderr


def load_compare() -> ModuleType:
    spec = importlib.util.spec_from_file_location("pme_bench_compare", COMPARE)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


# Covers: P0.3-R6
def test_p0_3_t3_flags_10_percent_regression(tmp_path: Path) -> None:
    """P0.3-T3: +10% on one benchmark exits 1 naming it; +4% and -10% exit 0."""
    base = bench_doc({"alpha": 1000.0, "beta": 2000.0})

    code, out = run_compare(tmp_path, base, bench_doc({"alpha": 1100.0, "beta": 2000.0}))
    assert code == 1
    assert "REGRESSION alpha" in out
    assert "beta" in out and "REGRESSION beta" not in out

    code, out = run_compare(tmp_path, base, bench_doc({"alpha": 1040.0, "beta": 2080.0}))
    assert code == 0, out

    code, out = run_compare(tmp_path, base, bench_doc({"alpha": 900.0, "beta": 1800.0}))
    assert code == 0, out


# Covers: P0.3-R6
def test_p0_3_t3_boundary_is_strictly_greater_than_five_percent(tmp_path: Path) -> None:
    """P0.3-T3: exactly +5% passes, just above +5% fails."""
    base = bench_doc({"alpha": 100.0})
    assert run_compare(tmp_path, base, bench_doc({"alpha": 105.0}))[0] == 0
    assert run_compare(tmp_path, base, bench_doc({"alpha": 105.01}))[0] == 1


def test_p0_3_t3_benchmarks_in_only_one_file_are_reported_not_failed(tmp_path: Path) -> None:
    """P0.3-T3: a benchmark present in only one file is reported and does not fail the run."""
    code, out = run_compare(
        tmp_path,
        bench_doc({"alpha": 100.0, "gone": 50.0}),
        bench_doc({"alpha": 100.0, "new": 70.0}),
    )
    assert code == 0
    assert "only in baseline: gone" in out
    assert "only in current: new" in out


def test_p0_3_t3_malformed_input_exits_2(tmp_path: Path) -> None:
    """P0.3-T3: unreadable or malformed files exit 2 rather than passing silently."""
    bad = bench_doc({"alpha": 100.0})
    bad["schema_version"] = 7
    assert run_compare(tmp_path, bad, bench_doc({"alpha": 100.0}))[0] == 2
    good = bench_doc({"alpha": 100.0})
    missing = subprocess.run(
        [sys.executable, str(COMPARE), str(tmp_path / "nope.json"), str(tmp_path / "nope2.json")],
        capture_output=True,
        text=True,
        check=False,
    )
    assert missing.returncode == 2
    del good["results"][0]["median_ns"]
    assert run_compare(tmp_path, good, bench_doc({"alpha": 100.0}))[0] == 2


@pytest.mark.skipif("PME_BENCH_SMOKE" not in os.environ, reason="PME_BENCH_SMOKE not set")
def test_p0_3_t3_smoke_benchmark_writes_valid_json(tmp_path: Path) -> None:
    """P0.3-T3: the smoke benchmark runs and its JSON matches the harness format."""
    exe = os.environ["PME_BENCH_SMOKE"]
    out = tmp_path / "smoke.json"
    subprocess.run(
        [exe, "--repetitions", "10", "--min-time-ms", "0.2", "--out", str(out)],
        check=True,
        capture_output=True,
    )
    module = load_compare()
    doc = module.load(out)
    assert [r["name"] for r in doc["results"]] == ["smoke_sum_loop"]
    result = doc["results"][0]
    assert result["repetitions"] == 10
    assert result["median_ns"] > 0
    assert result["min_ns"] <= result["median_ns"]
    # A file compared with itself never regresses.
    assert module.compare(doc, doc)[0] == []
