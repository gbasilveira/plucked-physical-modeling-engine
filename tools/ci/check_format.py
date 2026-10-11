# SPDX-License-Identifier: MIT
"""clang-format check over every tracked C++ file under the project directories (P0.2-R4).

The pull-request ``lint`` job and developers run the same command::

    python3 tools/ci/check_format.py --clang-format "uvx clang-format==18.1.8"

Exit status 0 when every file is formatted, 1 when any file would change (the files are named on
standard error), 2 when clang-format could not be run.
"""

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # runnable as a script

from ci._common import CPP_SUFFIXES, tracked_files

CHUNK = 100  # files per clang-format invocation, to stay below command-line limits


def check(root: Path, clang_format: list[str]) -> int:
    """Run clang-format in dry-run mode over the tracked C++ files; return the exit status."""
    files = tracked_files(root, CPP_SUFFIXES)
    if not files:
        print("clang-format: no C++ files to check")
        return 0
    failed = False
    for start in range(0, len(files), CHUNK):
        chunk = files[start : start + CHUNK]
        try:
            proc = subprocess.run(
                [*clang_format, "--dry-run", "--Werror", "--style=file", *chunk],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError as exc:
            print(f"cannot run {clang_format[0]}: {exc}", file=sys.stderr)
            return 2
        if proc.returncode != 0:
            failed = True
            sys.stderr.write(proc.stdout)
            sys.stderr.write(proc.stderr)
    if failed:
        print("clang-format: the files named above are not formatted", file=sys.stderr)
        return 1
    print(f"clang-format: {len(files)} file(s) formatted")
    return 0


def main(argv: list[str] | None = None) -> int:
    """Entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[2], help="repository root"
    )
    parser.add_argument(
        "--clang-format",
        default="clang-format",
        help='clang-format command, split like a shell line (e.g. "uvx clang-format==18.1.8")',
    )
    args = parser.parse_args(argv)
    return check(args.root, shlex.split(args.clang_format))


if __name__ == "__main__":
    sys.exit(main())
