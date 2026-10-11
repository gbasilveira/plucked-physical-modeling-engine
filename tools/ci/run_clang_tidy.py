# SPDX-License-Identifier: MIT
"""clang-tidy over every tracked C++ source under the project directories (P0.2-R4).

Needs a compile database (``compile_commands.json``) in the build directory given by ``-p``,
for example from ``CC=clang CXX=clang++ cmake --preset linux-debug -B build/linux-debug-clang``.
Fetched dependencies live under the build directory and are never listed by ``git ls-files``,
so they are not analysed. The configuration is ``.clang-tidy``.

    python3 tools/ci/run_clang_tidy.py -p build/linux-debug-clang --clang-tidy "uvx clang-tidy==18.1.8"
"""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # runnable as a script

from ci._common import CPP_SOURCE_SUFFIXES, tracked_files  # noqa: E402

CHUNK = 20


def main(argv: list[str] | None = None) -> int:
    """Entry point; returns 0 when clean, 1 on findings, 2 on setup errors."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("-p", dest="build", type=Path, required=True, help="build dir")
    parser.add_argument("--clang-tidy", default="clang-tidy", help="clang-tidy command line")
    args = parser.parse_args(argv)

    database = args.build / "compile_commands.json"
    if not database.is_file():
        print(f"no compile database at {database}", file=sys.stderr)
        return 2
    in_database = {Path(entry["file"]).resolve() for entry in json.loads(database.read_text())}

    sources = [
        f
        for f in tracked_files(args.root, CPP_SOURCE_SUFFIXES)
        if (args.root / f).resolve() in in_database
    ]
    if not sources:
        print("clang-tidy: no project sources in the compile database")
        return 0

    tidy = shlex.split(args.clang_tidy)
    failed = False
    for start in range(0, len(sources), CHUNK):
        chunk = sources[start : start + CHUNK]
        proc = subprocess.run(
            [*tidy, "-p", str(args.build), "--quiet", *chunk],
            cwd=args.root,
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0:
            failed = True
            sys.stderr.write(proc.stdout)
            sys.stderr.write(proc.stderr)
    if failed:
        print("clang-tidy: findings above", file=sys.stderr)
        return 1
    print(f"clang-tidy: {len(sources)} source(s) clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
