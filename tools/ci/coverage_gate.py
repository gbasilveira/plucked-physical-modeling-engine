# SPDX-License-Identifier: MIT
"""Line-coverage gate over a Cobertura XML report (P0.2-R6; Testing policy: engine core >= 90%).

Measured set: files under ``engine/src/``, excluding ``engine/src/rtcheck/`` (test infrastructure),
``engine/src/sched/platform/`` (platform glue; none yet) and files named ``*_placeholder.cpp``.
When the measured set is empty the gate reports "no engine sources measured" and passes.

    python3 tools/ci/coverage_gate.py coverage.xml --threshold 90
"""

from __future__ import annotations

import argparse
import fnmatch
import sys
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath

INCLUDE_PREFIX = "engine/src/"
EXCLUDE_PREFIXES = ("engine/src/rtcheck/", "engine/src/sched/platform/")
EXCLUDE_NAMES = ("*_placeholder.cpp",)


def measured(path: str) -> bool:
    """Return True when ``path`` (relative to the repository root) is in the measured set."""
    p = path.replace("\\", "/")
    if not p.startswith(INCLUDE_PREFIX) or p.startswith(EXCLUDE_PREFIXES):
        return False
    return not any(fnmatch.fnmatch(PurePosixPath(p).name, pat) for pat in EXCLUDE_NAMES)


def line_counts(report: Path) -> tuple[int, int, list[str]]:
    """Return (covered, total, files) over the measured set of a Cobertura report."""
    root = ET.parse(report).getroot()
    covered = total = 0
    files: list[str] = []
    for cls in root.iter("class"):
        name = cls.get("filename", "")
        if not measured(name):
            continue
        files.append(name)
        for line in cls.iter("line"):
            total += 1
            if int(line.get("hits", "0")) > 0:
                covered += 1
    return covered, total, sorted(set(files))


def main(argv: list[str] | None = None) -> int:
    """Entry point; returns 0 when the gate holds."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="Cobertura XML")
    parser.add_argument("--threshold", type=float, default=90.0)
    args = parser.parse_args(argv)

    covered, total, files = line_counts(args.report)
    if total == 0:
        print("no engine sources measured: coverage gate passes")
        return 0
    percent = 100.0 * covered / total
    print(f"line coverage of engine/src: {covered}/{total} = {percent:.2f}% ({len(files)} files)")
    if percent < args.threshold:
        print(f"below the {args.threshold:.0f}% threshold", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
