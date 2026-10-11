# SPDX-License-Identifier: MIT
"""Self-test of tools/check_spdx.py."""

import subprocess
from pathlib import Path

import check_spdx


def _repo(tmp_path: Path, files: dict[str, str]) -> Path:
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    for name, text in files.items():
        target = tmp_path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
    subprocess.run(["git", "add", "-A"], cwd=tmp_path, check=True)
    return tmp_path


def test_p0_1_t3_passes_on_headed_files(tmp_path: Path) -> None:
    """P0.1-T3: headed files, exempt files and empty files pass."""
    root = _repo(
        tmp_path,
        {
            "a.cpp": "// SPDX-License-Identifier: MIT\nint x;\n",
            "CMakeLists.txt": "# SPDX-License-Identifier: MIT\n",
            "x.md": "no header\n",
            "p.json": "{}\n",
            ".gitkeep": "",
            "data/blob.bin": "anything",
        },
    )
    assert check_spdx.run(root) == []


def test_p0_1_t3_fails_on_missing_header(tmp_path: Path) -> None:
    """P0.1-T3: a source file without a header fails."""
    root = _repo(tmp_path, {"a.py": "x = 1\n"})
    assert len(check_spdx.run(root)) == 1


def test_p0_1_t3_fails_on_wrong_identifier(tmp_path: Path) -> None:
    """P0.1-T3: a different SPDX identifier fails."""
    root = _repo(tmp_path, {"a.py": "# SPDX-License-Identifier: GPL-3.0-only\n"})
    assert len(check_spdx.run(root)) == 1


def test_p0_1_t3_fails_on_unknown_file_type(tmp_path: Path) -> None:
    """P0.1-T3: an unknown file type fails rather than passing silently."""
    root = _repo(tmp_path, {"a.xyz": "text\n"})
    problems = check_spdx.run(root)
    assert len(problems) == 1
    assert "unknown file type" in problems[0]


def test_p0_1_t3_fails_on_identifier_with_suffix(tmp_path: Path) -> None:
    """P0.1-T3: ``MIT-0`` is not ``MIT``; the identifier must match exactly."""
    root = _repo(tmp_path, {"a.py": "# SPDX-License-Identifier: MIT-0\n"})
    assert len(check_spdx.run(root)) == 1


def test_p0_1_t3_fails_on_compound_expression(tmp_path: Path) -> None:
    """P0.1-T3: ``MIT OR Apache-2.0`` is not ``MIT``; the identifier must match exactly."""
    root = _repo(tmp_path, {"a.cpp": "// SPDX-License-Identifier: MIT OR Apache-2.0\n"})
    assert len(check_spdx.run(root)) == 1


def test_p0_1_t3_accepts_trailing_whitespace_and_comment_styles(tmp_path: Path) -> None:
    """P0.1-T3: trailing whitespace and block-comment closers are allowed."""
    root = _repo(
        tmp_path,
        {
            "a.cpp": "// SPDX-License-Identifier: MIT   \n",
            "b.h": "/* SPDX-License-Identifier: MIT */\n",
            "c.py": "# SPDX-License-Identifier: MIT\t\n",
        },
    )
    assert check_spdx.run(root) == []
