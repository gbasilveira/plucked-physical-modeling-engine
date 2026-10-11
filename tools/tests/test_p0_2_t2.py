# SPDX-License-Identifier: MIT
"""P0.2-T2: a mis-formatted file fails the format check that the pull-request ``lint`` job runs."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "ci" / "check_format.py"

GOOD = "// SPDX-License-Identifier: MIT\nint add(int a, int b) { return a + b; }\n"
BAD = "// SPDX-License-Identifier: MIT\nint   add( int a,int b ){return a+b;}\n"


def _clang_format_command() -> str:
    """The clang-format to use: ``$PME_CLANG_FORMAT``, one on PATH, or the pinned PyPI wheel."""
    env = os.environ.get("PME_CLANG_FORMAT")
    if env:
        return env
    if shutil.which("clang-format"):
        return "clang-format"
    if shutil.which("uvx"):
        return "uvx clang-format==18.1.8"
    pytest.skip("no clang-format available")


def _repo(tmp_path: Path, files: dict[str, str]) -> Path:
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    shutil.copy(ROOT / ".clang-format", tmp_path / ".clang-format")
    for name, text in files.items():
        target = tmp_path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")
    subprocess.run(["git", "add", "-A"], cwd=tmp_path, check=True)
    return tmp_path


def _run(repo: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--root",
            str(repo),
            "--clang-format",
            _clang_format_command(),
        ],
        capture_output=True,
        text=True,
        check=False,
    )


def test_p0_2_t2_misformatted_file_fails(tmp_path: Path) -> None:
    """P0.2-T2: the format script exits non-zero and names the mis-formatted file."""
    repo = _repo(tmp_path, {"engine/src/osc/good.cpp": GOOD, "engine/src/osc/bad.cpp": BAD})
    result = _run(repo)
    assert result.returncode == 1, result.stdout + result.stderr
    assert "bad.cpp" in result.stderr
    assert "good.cpp" not in result.stderr


def test_p0_2_t2_formatted_files_pass(tmp_path: Path) -> None:
    """P0.2-T2: the control, a well-formatted tree, passes."""
    repo = _repo(tmp_path, {"engine/src/osc/good.cpp": GOOD, "tools/x/helper.hpp": GOOD})
    result = _run(repo)
    assert result.returncode == 0, result.stdout + result.stderr


def test_p0_2_t2_untracked_directories_are_ignored(tmp_path: Path) -> None:
    """P0.2-T2: files outside the project directories (build output, fetched code) are skipped."""
    repo = _repo(tmp_path, {"build/_deps/x/vendor.cpp": BAD, "engine/src/osc/good.cpp": GOOD})
    assert _run(repo).returncode == 0


def test_p0_2_t2_workflow_has_the_injection_hook() -> None:
    """P0.2-T2: pr.yml carries the dispatch input and the commit-message marker."""
    text = (ROOT / ".github" / "workflows" / "pr.yml").read_text(encoding="utf-8")
    assert "p0_2_t2_inject_misformat" in text
    assert "[p0.2-t2-misformat]" in text
