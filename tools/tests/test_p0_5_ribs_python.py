# SPDX-License-Identifier: MIT
"""P0.5-R7 follow-up P0.5-ribs: pme_reference.plate_modes and the CLI subcommand plate-modes.

Same environment as test_p0_5_r7_python.py: PME_REFERENCE_PYDIR names the directory with the module
(skip when unset, failure with PME_REFERENCE_REQUIRED=1); PME_REFERENCE_CLI optionally names
pme_reference_cli.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest

ROOT = Path(__file__).resolve().parents[2]
RIBBED = ROOT / "reference" / "scenarios" / "ribbed_plate.json"

# Frequencies (Hz) of the B2-T4 configuration (three spruce ribs parallel to y at x = 0.15, 0.3,
# 0.45 m on the P0.5-T4 plate) on the 24 x 16 grid, from the C++ test "P0.5-R4 ribbed plate
# frequencies converge at second order" (first six modes).
GRID_24X16_FIRST_SIX = [
    134.026840,
    146.706131,
    189.988840,
    302.419632,
    321.039107,
    346.853206,
]


def _pydir() -> Path:
    value = os.environ.get("PME_REFERENCE_PYDIR")
    if not value:
        if os.environ.get("PME_REFERENCE_REQUIRED") == "1":
            pytest.fail("PME_REFERENCE_PYDIR is not set but PME_REFERENCE_REQUIRED=1")
        pytest.skip("PME_REFERENCE_PYDIR is not set (build with -DPME_BUILD_PYTHON_BINDINGS=ON)")
    return Path(value)


def _module() -> Any:
    pydir = _pydir()
    sys.path.insert(0, str(pydir))
    try:
        import pme_reference
    finally:
        sys.path.remove(str(pydir))
    return pme_reference


def _cli() -> Path:
    value = os.environ.get("PME_REFERENCE_CLI")
    if value:
        return Path(value)
    exe = "pme_reference_cli.exe" if sys.platform == "win32" else "pme_reference_cli"
    return _pydir().parents[1] / "tools" / "reference-cli" / exe


def _plate() -> dict[str, Any]:
    scenario: dict[str, Any] = json.loads(RIBBED.read_text(encoding="utf-8"))
    plate: dict[str, Any] = scenario["plate"]
    return plate


def test_p0_5_r7_plate_modes_binding_matches_the_cxx_test() -> None:
    """P0.5-R7: plate_modes returns the frequencies of the C++ test configuration."""
    ref = _module()
    plate = _plate()
    result = ref.plate_modes(plate, 24, 16, 6)
    freqs = result["frequencies_hz"]
    assert len(freqs) == 6
    assert result["count"] == 6
    assert result["nx"] == 24
    assert result["ny"] == 16
    assert freqs == sorted(freqs)
    for got, expected in zip(freqs, GRID_24X16_FIRST_SIX, strict=True):
        assert got == pytest.approx(expected, rel=1e-7)
    assert result["max_residual"] < 1e-8
    assert "values" not in result


def test_p0_5_r7_plate_modes_binding_returns_shapes_at_points() -> None:
    """P0.5-R7: with points the result holds one mass-normalised value per mode and point."""
    ref = _module()
    plate = _plate()
    points = [(0.21, 0.13), (0.43, 0.27)]
    result = ref.plate_modes(plate, 24, 16, 4, points)
    assert result["points"] == [[0.21, 0.13], [0.43, 0.27]]
    values = result["values"]
    assert len(values) == 4
    assert all(len(row) == 2 for row in values)
    # Deterministic: a second call gives bit-identical output.
    again = ref.plate_modes(plate, 24, 16, 4, points)
    assert again == result


def test_p0_5_r7_plate_modes_rejects_bad_ribs() -> None:
    """P0.5-R7: a diagonal rib and an off-grid rib raise ValueError naming ribs[0]."""
    ref = _module()
    plate = _plate()
    diagonal = json.loads(json.dumps(plate))
    diagonal["ribs"][0]["end"] = {"x": 0.2, "y": 0.4}
    with pytest.raises(ValueError, match=r"ribs\[0\]"):
        ref.plate_modes(diagonal, 24, 16, 3)
    off_grid = json.loads(json.dumps(plate))
    for end in ("start", "end"):
        off_grid["ribs"][0][end]["x"] = 0.151
    with pytest.raises(ValueError, match=r"ribs\[0\].*grid"):
        ref.plate_modes(off_grid, 24, 16, 3)


def test_p0_5_r7_plate_modes_cli_prints_the_same_json() -> None:
    """P0.5-R7: pme_reference_cli plate-modes prints the dict that the binding returns."""
    ref = _module()
    cli = _cli()
    if not cli.exists():
        if os.environ.get("PME_REFERENCE_REQUIRED") == "1":
            pytest.fail(f"pme_reference_cli not found at {cli}")
        pytest.skip(f"pme_reference_cli not found at {cli}")
    completed = subprocess.run(
        [str(cli), "plate-modes", str(RIBBED), "--count", "6", "--points", "0.21,0.13,0.43,0.27"],
        check=True,
        capture_output=True,
        text=True,
    )
    from_cli = json.loads(completed.stdout)
    from_python = ref.plate_modes(_plate(), 24, 16, 6, [(0.21, 0.13), (0.43, 0.27)])
    assert from_cli == from_python
