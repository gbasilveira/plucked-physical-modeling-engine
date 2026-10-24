# SPDX-License-Identifier: MIT
"""P0.2-R7: required checks of ``main`` match the jobs of pr.yml; branch-protection dry run."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "pr.yml"
CI = ROOT / "tools" / "ci"


def _job_blocks(text: str) -> dict[str, str]:
    """Split the ``jobs:`` mapping of a workflow into {job id: block text} (2-space indent)."""
    jobs_text = text.split("\njobs:\n", 1)[1]
    blocks: dict[str, str] = {}
    current: str | None = None
    for line in jobs_text.splitlines(keepends=True):
        match = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if match:
            current = match.group(1)
            blocks[current] = ""
        elif current is not None:
            blocks[current] += line
    return blocks


def _matrix_values(block: str, key: str) -> list[str]:
    """Values of ``matrix.<key>`` in a job block (``include`` entries or an inline list)."""
    values = re.findall(rf"^\s+(?:- )?{key}: (?!\[)(\S+)\s*$", block, flags=re.M)
    for inline in re.findall(rf"^\s+{key}: \[([^\]]*)\]", block, flags=re.M):
        values += [v.strip() for v in inline.split(",")]
    return values


def expanded_job_names(text: str) -> set[str]:
    """Names of every job (matrix-expanded) in a workflow."""
    names: set[str] = set()
    for job_id, block in _job_blocks(text).items():
        match = re.search(r"^    name: (.+?)\s*$", block, flags=re.M)
        template = match.group(1).strip('"') if match else job_id
        keys = re.findall(r"\$\{\{ matrix\.(\w+) \}\}", template)
        if not keys:
            names.add(template)
            continue
        for value in _matrix_values(block, keys[0]):
            names.add(template.replace("${{ matrix." + keys[0] + " }}", value))
    return names


def test_p0_2_r7_required_checks_match_workflow() -> None:
    """P0.2-R7: every required check is a job name (or matrix-expanded name) of pr.yml."""
    required = json.loads((CI / "required_checks.json").read_text(encoding="utf-8"))
    names = expanded_job_names(WORKFLOW.read_text(encoding="utf-8"))
    assert required["required_checks"], "the list must not be empty"
    missing = [c for c in required["required_checks"] if c not in names]
    assert not missing, f"not jobs of pr.yml: {missing}; jobs are {sorted(names)}"


def test_p0_2_r7_every_pr_job_is_required() -> None:
    """P0.2-R7: no job of pr.yml is silently left out of the required checks."""
    required = set(json.loads((CI / "required_checks.json").read_text("utf-8"))["required_checks"])
    names = expanded_job_names(WORKFLOW.read_text(encoding="utf-8"))
    assert names == required


def test_p0_2_r7_contributing_lists_the_checks() -> None:
    """P0.2-R7: CONTRIBUTING.md names every required check."""
    contributing = (ROOT / "CONTRIBUTING.md").read_text(encoding="utf-8")
    required = json.loads((CI / "required_checks.json").read_text(encoding="utf-8"))
    for check in required["required_checks"]:
        assert f"`{check}`" in contributing, check


def test_p0_2_r7_branch_protection_dry_run_prints_the_request() -> None:
    """P0.2-R7: the default mode sends nothing and prints the PUT request."""
    result = subprocess.run(
        [sys.executable, str(CI / "apply_branch_protection.py"), "--repo", "o/r"],
        capture_output=True,
        text=True,
        check=False,
        env={"PATH": ""},  # no token, no network: a dry run must not need either
    )
    assert result.returncode == 0, result.stderr
    assert "PUT https://api.github.com/repos/o/r/branches/main/protection" in result.stdout
    assert '"strict": true' in result.stdout
    assert "lint" in result.stdout
    assert "dry run: nothing was sent" in result.stdout


def test_p0_2_r7_apply_without_token_fails() -> None:
    """P0.2-R7: --apply without GITHUB_TOKEN exits 2 before any request."""
    result = subprocess.run(
        [sys.executable, str(CI / "apply_branch_protection.py"), "--repo", "o/r", "--apply"],
        capture_output=True,
        text=True,
        check=False,
        env={"PATH": ""},
    )
    assert result.returncode == 2
