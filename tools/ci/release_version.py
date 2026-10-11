# SPDX-License-Identifier: MIT
"""Version check of the release workflow (P0.2-R3).

Compares the tag's version with ``project(... VERSION x.y.z ...)`` in the root CMakeLists.txt.
Exit 1 on a mismatch, except in ``--dry-run`` mode where both are only printed.

    python3 tools/ci/release_version.py --tag v1.2.3
    python3 tools/ci/release_version.py --dry-run --sha 0123456789abcdef
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TAG_RE = re.compile(r"^v(?P<version>[0-9]+\.[0-9]+\.[0-9]+)(?P<suffix>[-+].*)?$")
PROJECT_VERSION_RE = re.compile(r"project\s*\([^)]*?\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", re.S)


def cmake_version(cmakelists: Path) -> str:
    """Return the VERSION of the first ``project()`` call."""
    match = PROJECT_VERSION_RE.search(cmakelists.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"no project(... VERSION x.y.z) in {cmakelists}")
    return match.group(1)


def main(argv: list[str] | None = None) -> int:
    """Entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--tag", help="tag name, for example v1.2.3 or v1.2.3-rc.1")
    parser.add_argument("--sha", default="unknown", help="commit SHA (dry-run version)")
    parser.add_argument("--dry-run", action="store_true", help="push-triggered run: print only")
    args = parser.parse_args(argv)

    declared = cmake_version(args.root / "CMakeLists.txt")
    if args.dry_run:
        version = f"0.0.0-dryrun+{args.sha[:7]}"
        print(f"release version (dry run): {version}")
        print(f"CMakeLists.txt project(VERSION): {declared}")
        print(f"VERSION={version}")
        return 0

    if not args.tag:
        parser.error("--tag is required unless --dry-run")
    match = TAG_RE.match(args.tag)
    if match is None:
        print(f"tag '{args.tag}' is not v<major>.<minor>.<patch>[suffix]", file=sys.stderr)
        return 1
    tag_version = match.group("version")
    print(f"tag version: {tag_version}; CMakeLists.txt project(VERSION): {declared}")
    if tag_version != declared:
        print("the tag does not match project(VERSION) in CMakeLists.txt", file=sys.stderr)
        return 1
    print(f"VERSION={args.tag.removeprefix('v')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
