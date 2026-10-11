# SPDX-License-Identifier: MIT
"""Markdown summary of JUnit XML reports for ``$GITHUB_STEP_SUMMARY`` (P0.2-R6).

    python3 tools/ci/junit_summary.py --title "linux-debug" reports/*.xml >> "$GITHUB_STEP_SUMMARY"

Handles the files written by ``ctest --output-junit`` and ``pytest --junitxml``. The script never
fails on a failing test (the test step does); a missing or unparsable file is reported in the table.
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Counts:
    """Totals of one report."""

    tests: int = 0
    failures: int = 0
    errors: int = 0
    skipped: int = 0


def _suites(root: ET.Element) -> list[ET.Element]:
    return [root] if root.tag == "testsuite" else list(root.iter("testsuite"))


def read_report(path: Path) -> tuple[Counts, list[str]]:
    """Return the totals of a JUnit file and the names of failed test cases."""
    root = ET.parse(path).getroot()
    counts = Counts()
    failed: list[str] = []
    top = _suites(root)
    # Nested suites would double count; only the outermost ones carry the totals.
    for suite in top:
        counts.tests += int(suite.get("tests", "0"))
        counts.failures += int(suite.get("failures", "0"))
        counts.errors += int(suite.get("errors", "0"))
        counts.skipped += int(suite.get("skipped", suite.get("disabled", "0")))
    for case in root.iter("testcase"):
        if case.find("failure") is not None or case.find("error") is not None:
            failed.append(f"{case.get('classname', '')}.{case.get('name', '')}".strip("."))
    return counts, failed


def summarize(title: str, paths: list[Path]) -> str:
    """Render the Markdown summary for ``paths``."""
    lines = [
        f"### {title}",
        "",
        "| report | tests | failed | errors | skipped |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    failed_names: list[str] = []
    for path in paths:
        try:
            counts, failed = read_report(path)
        except (OSError, ET.ParseError) as exc:
            lines.append(f"| `{path.name}` (unreadable: {exc}) | | | | |")
            continue
        failed_names.extend(failed)
        row = [counts.tests, counts.failures, counts.errors, counts.skipped]
        lines.append(f"| `{path.name}` | " + " | ".join(str(n) for n in row) + " |")
    if not paths:
        lines.append("| (no report files) | | | | |")
    if failed_names:
        lines += ["", "Failed:", *[f"- `{name}`" for name in failed_names]]
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    """Entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--title", default="Test reports")
    parser.add_argument("reports", nargs="*", type=Path)
    args = parser.parse_args(argv)
    sys.stdout.write(summarize(args.title, args.reports))
    return 0


if __name__ == "__main__":
    sys.exit(main())
