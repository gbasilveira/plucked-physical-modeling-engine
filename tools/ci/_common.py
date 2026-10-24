# SPDX-License-Identifier: MIT
"""Helpers shared by the CI scripts: tracked-file listing and project directories."""

from __future__ import annotations

import subprocess
from pathlib import Path

# Every directory that holds project-owned sources (Software guidelines, Repository layout).
# Checks are scoped by directory, never by a list of files, so that later phases are covered.
PROJECT_DIRS = ("engine", "tools", "reference", "plugin", "bindings", "compiler")

CPP_SOURCE_SUFFIXES = (".cpp", ".cc")
CPP_SUFFIXES = (".cpp", ".cc", ".hpp", ".h")


def tracked_files(root: Path, suffixes: tuple[str, ...]) -> list[str]:
    """Return tracked files (POSIX paths relative to ``root``) under the project directories.

    Only files with one of ``suffixes`` are returned, sorted for a stable order.
    """
    out = subprocess.run(
        ["git", "ls-files", "-z", "--", *PROJECT_DIRS],
        cwd=root,
        check=True,
        capture_output=True,
    ).stdout.decode()
    files = [p for p in out.split("\0") if p and p.endswith(suffixes)]
    return sorted(p for p in files if (root / p).is_file())
