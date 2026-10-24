# SPDX-License-Identifier: MIT
"""Stage and archive the release build outputs (P0.2-R3). Nothing is published.

Layout of the archive ``pme-<version>-<name>.zip`` (Windows) or ``.tar.gz``:

    pme-<version>-<name>/
        VERSION
        LICENSE
        include/pme/...          public headers (engine/include/pme)
        lib/<library file>       every static library the project built (``libpme_*.a`` or
                                 ``pme_*.lib``), plus Highway's, which ``pme_simd`` needs

    python3 tools/ci/stage_release.py --build-dir build/linux-release --name linux-gcc \\
        --version 1.2.3 --out dist
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

LIBRARY_SUFFIXES = (".a", ".lib")
# Static libraries that belong in the archive: ours and Highway's.
LIBRARY_STEMS = ("pme_", "libpme_", "hwy", "libhwy")


def find_libraries(build_dir: Path) -> list[Path]:
    """Return the project's static libraries found under ``build_dir``."""
    found = [
        p
        for p in build_dir.rglob("*")
        if p.is_file() and p.suffix in LIBRARY_SUFFIXES and p.name.startswith(LIBRARY_STEMS)
    ]
    return sorted(found)


def stage(root: Path, build_dir: Path, name: str, version: str, out: Path) -> Path:
    """Copy the outputs into ``out/pme-<version>-<name>/`` and return the archive path."""
    base = f"pme-{version}-{name}"
    stage_dir = out / base
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    (stage_dir / "lib").mkdir(parents=True)

    libraries = find_libraries(build_dir)
    if not libraries:
        raise SystemExit(f"no static libraries under {build_dir}")
    for lib in libraries:
        shutil.copy2(lib, stage_dir / "lib" / lib.name)

    headers = root / "engine" / "include" / "pme"
    if headers.is_dir():
        shutil.copytree(headers, stage_dir / "include" / "pme")
    shutil.copy2(root / "LICENSE", stage_dir / "LICENSE")
    (stage_dir / "VERSION").write_text(version + "\n", encoding="utf-8")

    fmt = "zip" if name.startswith("windows") else "gztar"
    return Path(shutil.make_archive(str(out / base), fmt, root_dir=out, base_dir=base))


def main(argv: list[str] | None = None) -> int:
    """Entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--name", required=True, help="system label, e.g. linux-gcc")
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", type=Path, default=Path("dist"))
    args = parser.parse_args(argv)
    archive = stage(args.root, args.build_dir, args.name, args.version, args.out)
    print(f"staged {archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
