# SPDX-License-Identifier: MIT
"""clang-tidy over every tracked C++ source under the project directories (P0.2-R4).

Needs a compile database (``compile_commands.json``) in each build directory given by ``-p``
(repeatable), for example from
``CC=clang CXX=clang++ cmake --preset linux-debug -B build/lint-debug``.
Use ``linux-debug`` and ``linux-rt-safety`` databases together: sources that only exist in the
rt-safety build (the trap itself) are in the second.
Fetched dependencies live under the build directory and are never listed by ``git ls-files``,
so they are not analysed. The configuration is ``.clang-tidy``.

    python3 tools/ci/run_clang_tidy.py -p build/lint-debug -p build/lint-rt \\
        --clang-tidy clang-tidy-18
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # runnable as a script

from ci._common import CPP_SOURCE_SUFFIXES, tracked_files


def tidy_file(root: Path, build: Path, tidy: list[str], source: str) -> tuple[int, str]:
    """Run clang-tidy on one source; returns its exit status and combined output."""
    proc = subprocess.run(
        [*tidy, "-p", str(build), "--quiet", source],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    return proc.returncode, proc.stdout + proc.stderr


def tidy_build(root: Path, build: Path, tidy: list[str], jobs: int) -> int:
    """Run clang-tidy over the project sources of one compile database; 0 clean, 1 findings.

    Files run concurrently (``jobs`` at a time); output is printed in sorted file order so the
    log is deterministic. Any non-zero clang-tidy exit (findings are errors) fails the build.
    """
    database = build / "compile_commands.json"
    in_database = {Path(entry["file"]).resolve() for entry in json.loads(database.read_text())}
    sources = [
        f for f in tracked_files(root, CPP_SOURCE_SUFFIXES) if (root / f).resolve() in in_database
    ]
    failed = False
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        # map() yields results in submission (file) order while the work runs concurrently.
        for returncode, output in pool.map(lambda f: tidy_file(root, build, tidy, f), sources):
            if returncode != 0:
                failed = True
                sys.stderr.write(output)
    print(f"clang-tidy -p {build}: {len(sources)} source(s), {'findings' if failed else 'clean'}")
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    """Entry point; returns 0 when clean, 1 on findings, 2 on setup errors."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument(
        "-p",
        dest="builds",
        type=Path,
        action="append",
        required=True,
        help="build dir (repeatable)",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="concurrent clang-tidy processes (default: CPU count)",
    )
    parser.add_argument("--clang-tidy", default="clang-tidy", help="clang-tidy command line")
    args = parser.parse_args(argv)

    for build in args.builds:
        if not (build / "compile_commands.json").is_file():
            print(f"no compile database in {build}", file=sys.stderr)
            return 2
    tidy = shlex.split(args.clang_tidy)
    status = 0
    for build in args.builds:
        status |= tidy_build(args.root, build, tidy, max(1, args.jobs))
    return status


if __name__ == "__main__":
    sys.exit(main())
