# SPDX-License-Identifier: MIT
"""V1-R4: the matrix is published as a CI artifact on every pull request and nightly."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PR = (ROOT / ".github/workflows/pr.yml").read_text(encoding="utf-8")
NIGHTLY = (ROOT / ".github/workflows/nightly.yml").read_text(encoding="utf-8")


def v1_block(text: str) -> str:
    match = re.search(r"# ---- V1: traceability ----\n(.*?)\n  # ---- end V1 ----", text, re.S)
    assert match, "no delimited V1 block"
    return match.group(1)


def test_v1_r4_pr_workflow_has_the_traceability_job() -> None:
    """V1-R4: pr.yml ends with a delimited traceability job that publishes the artifact."""
    block = v1_block(PR)
    assert "\n  traceability:\n    name: traceability\n" in block
    needs = re.search(r"needs: \[([^\]]*)\]", block)
    assert needs
    # D1 and P0.5 add `plugin` and `reference-python` when the orchestrator merges them.
    assert {"lint", "build", "python", "coverage", "deps"} <= {
        n.strip() for n in needs.group(1).split(",")
    }
    assert "if: always()" in block
    assert "pattern: junit-*" in block
    assert "toJSON(needs)" in block
    assert "--fail-on-gaps" in block
    assert "--known-gaps tools/trace/known-gaps.json" in block
    assert "GITHUB_STEP_SUMMARY" in block
    assert (
        "name: traceability\n          path: |\n            traceability/traceability.html" in block
    )
    assert "traceability/traceability.json" in block


def test_v1_r4_the_traceability_job_is_a_required_check() -> None:
    """V1-R4: `traceability` is in required_checks.json and in the CONTRIBUTING table."""
    required = json.loads((ROOT / "tools/ci/required_checks.json").read_text("utf-8"))
    assert "traceability" in required["required_checks"]
    assert "| `traceability` |" in (ROOT / "CONTRIBUTING.md").read_text(encoding="utf-8")


def test_v1_r4_nightly_traceability_reports_but_does_not_fail() -> None:
    """V1-R4: the nightly job publishes the artifact and never passes --fail-on-gaps."""
    block = v1_block(NIGHTLY)
    assert "\n  traceability:\n" in block
    assert "pattern: junit-*" in block
    assert "name: traceability" in block
    assert "--fail-on-gaps" not in block
    assert "--job-results-workflow nightly.yml" in block
    assert "needs: [sanitizers, coverage-build, scheduled-labels]" in block


def test_v1_r4_every_job_that_uploads_junit_is_awaited_by_the_traceability_job() -> None:
    """V1-R4: a pr.yml job that uploads junit-* artifacts is in the traceability job's needs."""
    jobs = re.split(r"\n  (?=[A-Za-z0-9_-]+:\n)", PR.split("\njobs:\n", 1)[1])
    uploaders = []
    for job in jobs:
        name = job.split(":", 1)[0].lstrip()
        if "name: junit-" in job:
            uploaders.append(name)
    needs = re.search(r"needs: \[([^\]]*)\]", v1_block(PR))
    assert needs
    waited = {n.strip() for n in needs.group(1).split(",")}
    # The orchestrator adds `plugin` and `reference-python` when it merges D1 and P0.5; those jobs
    # have to appear in `needs` too, so the check applies to whatever uploads junit-* today.
    assert set(uploaders) <= waited, f"not awaited: {sorted(set(uploaders) - waited)}"


def test_v1_r4_the_command_line_runs_from_a_clean_checkout(tmp_path: Path) -> None:
    """V1-R4: build_matrix.py --help runs with the standard library only (the CI job has no uv)."""
    done = subprocess.run(
        [sys.executable, "-I", str(ROOT / "tools/trace/build_matrix.py"), "--help"],
        check=True,
        capture_output=True,
        text=True,
        cwd=tmp_path,
    )
    assert "--junit" in done.stdout and "--fail-on-gaps" in done.stdout
