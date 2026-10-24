# SPDX-License-Identifier: MIT
"""P0.4-R6: the interfaces compatibility policy is stated in the header and the developer doc."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
POLICY = "source compatibility holds within a major version"


def _flat(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return re.sub(r"[\s/*#>]+", " ", text).lower()


def test_p0_4_r6_policy_text_in_header_and_doc() -> None:
    """P0.4-R6: version.hpp and docs/dev/interfaces.md state the compatibility policy."""
    header = _flat(ROOT / "engine" / "include" / "pme" / "version.hpp")
    doc = _flat(ROOT / "docs" / "dev" / "interfaces.md")
    assert POLICY in header
    assert POLICY in doc
    assert "abi is not promised" in doc
    assert "docs/icr/readme.md" in doc or "icr" in doc
