# SPDX-License-Identifier: MIT
"""P0.1-R6: the ADR template and ADR-0001 to ADR-0003 exist with the required fields."""

from __future__ import annotations

from pathlib import Path

ADR = Path(__file__).resolve().parents[2] / "docs" / "adr"
FIELDS = ("Status:", "Date:", "Deciders:", "Phase:", "References:")
SECTIONS = ("## Decision", "## Context", "## Options considered", "## Consequences")


def test_p0_1_r6_template_has_required_fields() -> None:
    """P0.1-R6: the ADR template carries the header fields and sections."""
    text = (ADR / "TEMPLATE.md").read_text(encoding="utf-8")
    for token in (*FIELDS, *SECTIONS, "## Verification", "## Revisiting"):
        assert token in text, token


def test_p0_1_r6_first_three_adrs_exist_with_fields() -> None:
    """P0.1-R6: ADR-0001 (Highway), ADR-0002 (scikit-fem), ADR-0003 (pinning) are complete."""
    expected = {
        "ADR-0001": "highway",
        "ADR-0002": "scikit-fem",
        "ADR-0003": "dependency-pinning",
    }
    for number, slug in expected.items():
        matches = list(ADR.glob(f"{number}-*.md"))
        assert len(matches) == 1, number
        assert slug in matches[0].name
        text = matches[0].read_text(encoding="utf-8")
        assert text.startswith(f"# {number}:"), number
        for token in (*FIELDS, *SECTIONS):
            assert token in text, (number, token)
