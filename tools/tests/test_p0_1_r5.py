# SPDX-License-Identifier: MIT
"""P0.1-R5: governance and repository configuration files exist with the required content."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_p0_1_r5_governance_files_exist() -> None:
    """P0.1-R5: LICENSE, CONTRIBUTING, CODEOWNERS and the tool configurations exist."""
    for name in (
        "LICENSE",
        "CONTRIBUTING.md",
        "CODEOWNERS",
        ".clang-format",
        ".clang-tidy",
        ".editorconfig",
        ".gitattributes",
    ):
        path = ROOT / name
        assert path.is_file(), name
        assert path.stat().st_size > 0, name


def test_p0_1_r5_license_is_mit() -> None:
    """P0.1-R5: the licence is MIT."""
    assert "MIT License" in (ROOT / "LICENSE").read_text(encoding="utf-8")


def test_p0_1_r5_contributing_has_phase_id_rules() -> None:
    """P0.1-R5: CONTRIBUTING.md states the phase-ID commit, title and branch rules."""
    text = (ROOT / "CONTRIBUTING.md").read_text(encoding="utf-8")
    assert "phase/<ID>-<slug>" in text
    assert "phase ID" in text


def test_p0_1_r5_gitattributes_routes_data_and_audio_to_lfs() -> None:
    """P0.1-R5: /data and audio files go through Git LFS."""
    lines = (ROOT / ".gitattributes").read_text(encoding="utf-8").splitlines()
    lfs = {ln.split()[0] for ln in lines if "filter=lfs" in ln}
    assert "data/**" in lfs
    for pattern in ("*.wav", "*.flac", "*.aif", "*.aiff", "*.ogg", "*.mp3"):
        assert pattern in lfs, pattern


def test_p0_1_r5_codeowners_names_an_owner() -> None:
    """P0.1-R5: CODEOWNERS has at least one rule with an owner."""
    rules = [
        ln
        for ln in (ROOT / "CODEOWNERS").read_text(encoding="utf-8").splitlines()
        if ln.strip() and not ln.lstrip().startswith("#")
    ]
    assert any(len(r.split()) >= 2 and r.split()[1].startswith("@") for r in rules)
