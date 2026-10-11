# SPDX-License-Identifier: MIT
"""P0.2-R3: release version check and staging; the workflow does not publish."""

from __future__ import annotations

import re
import tarfile
from pathlib import Path

import pytest

from ci import release_version, stage_release

ROOT = Path(__file__).resolve().parents[2]
RELEASE = ROOT / ".github" / "workflows" / "release.yml"


def _cmake(tmp_path: Path, version: str) -> Path:
    (tmp_path / "CMakeLists.txt").write_text(
        f"cmake_minimum_required(VERSION 3.25)\nproject(pme VERSION {version}\n  LANGUAGES CXX)\n",
        encoding="utf-8",
    )
    return tmp_path


def test_p0_2_r3_tag_must_match_project_version(tmp_path: Path) -> None:
    """P0.2-R3: a tag whose version differs from project(VERSION) fails; a match passes."""
    root = _cmake(tmp_path, "1.2.3")
    assert release_version.main(["--root", str(root), "--tag", "v1.2.3"]) == 0
    assert release_version.main(["--root", str(root), "--tag", "v1.2.3-rc.1"]) == 0
    assert release_version.main(["--root", str(root), "--tag", "v1.2.4"]) == 1
    assert release_version.main(["--root", str(root), "--tag", "release-1.2.3"]) == 1


def test_p0_2_r3_dry_run_only_prints(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """P0.2-R3: in dry-run mode a mismatch is not an error and the version is 0.0.0-dryrun+sha."""
    root = _cmake(tmp_path, "9.9.9")
    assert release_version.main(["--root", str(root), "--dry-run", "--sha", "0123456789ab"]) == 0
    out = capsys.readouterr().out
    assert "VERSION=0.0.0-dryrun+0123456" in out
    assert "9.9.9" in out


def test_p0_2_r3_repository_version_is_readable() -> None:
    """P0.2-R3: the real root CMakeLists.txt declares a parsable project(VERSION)."""
    assert re.fullmatch(r"\d+\.\d+\.\d+", release_version.cmake_version(ROOT / "CMakeLists.txt"))


def test_p0_2_r3_stage_collects_libraries_and_headers(tmp_path: Path) -> None:
    """P0.2-R3: the archive holds our libraries, Highway's, headers, LICENSE and VERSION."""
    root = tmp_path / "src"
    (root / "engine" / "include" / "pme").mkdir(parents=True)
    (root / "engine" / "include" / "pme" / "a.hpp").write_text("//", encoding="utf-8")
    (root / "LICENSE").write_text("MIT", encoding="utf-8")
    build = tmp_path / "build"
    (build / "_deps" / "highway-build").mkdir(parents=True)
    (build / "libpme_osc.a").write_bytes(b"x")
    (build / "_deps" / "highway-build" / "libhwy.a").write_bytes(b"x")
    (build / "libother.a").write_bytes(b"x")
    archive = stage_release.stage(root, build, "linux-gcc", "1.2.3", tmp_path / "dist")
    assert archive.name == "pme-1.2.3-linux-gcc.tar.gz"
    with tarfile.open(archive) as tar:
        names = set(tar.getnames())
    base = "pme-1.2.3-linux-gcc"
    assert {f"{base}/lib/libpme_osc.a", f"{base}/lib/libhwy.a", f"{base}/VERSION"} <= names
    assert f"{base}/include/pme/a.hpp" in names
    assert f"{base}/lib/libother.a" not in names


def test_p0_2_r3_release_workflow_does_not_publish() -> None:
    """P0.2-R3: no write permission, no secrets, no release or registry step."""
    lines = RELEASE.read_text(encoding="utf-8").splitlines()
    text = "\n".join(line for line in lines if not line.lstrip().startswith("#"))
    assert "contents: write" not in text
    assert "secrets." not in text
    assert "gh release" not in text
    assert "softprops" not in text
    assert "v[0-9]+.[0-9]+.[0-9]+*" in text
