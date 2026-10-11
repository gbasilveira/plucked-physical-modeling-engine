# SPDX-License-Identifier: MIT
"""P0.5-R7: the Python bindings of the reference solver (module pme_reference).

The module is built with -DPME_BUILD_PYTHON_BINDINGS=ON; PME_REFERENCE_PYDIR names the directory
that holds it (build/<preset>/reference/python). The tests skip when the variable is unset, unless
PME_REFERENCE_REQUIRED=1 (set by the reference-python CI job), where a missing module is a failure.
PME_REFERENCE_CLI optionally names pme_reference_cli; by default it is looked up next to the module.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
SMOKE = ROOT / "reference" / "scenarios" / "smoke.json"


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


def _scenario() -> dict[str, Any]:
    scenario: dict[str, Any] = json.loads(SMOKE.read_text(encoding="utf-8"))
    return scenario


def test_p0_5_r7_python_render_shapes() -> None:
    """P0.5-R7: render returns NumPy arrays of the right shape and the features dict."""
    ref = _module()
    scenario = _scenario()
    result = ref.render(scenario)
    steps = round(scenario["duration"] * scenario["fs"] * scenario["oversampling"])
    assert result["steps"] == steps
    y = result["signals"]["y"]
    assert isinstance(y, np.ndarray)
    assert y.dtype == np.float64
    assert y.shape == (steps,)
    assert np.all(np.isfinite(y))
    assert float(np.max(np.abs(y))) > 0.0
    energy = result["energy"]
    assert energy["total"].shape == (steps + 1,)
    assert energy["loss"].shape == (steps,)
    assert energy["max_relative_imbalance"] < 1e-12
    features = result["features"]
    assert features["schema_version"] == 1
    assert features["scenario"] == "smoke"
    assert len(features["partials"]) >= 3
    assert result["sample_rate_hz"] == scenario["fs"] * scenario["oversampling"]


def test_p0_5_r7_python_features_match_cli_exactly(tmp_path: Path) -> None:
    """P0.5-R7: the features of render() equal the CLI's --features file for the same scenario."""
    ref = _module()
    cli = _cli()
    if not cli.exists():
        if os.environ.get("PME_REFERENCE_REQUIRED") == "1":
            pytest.fail(f"pme_reference_cli not found at {cli}")
        pytest.skip(f"pme_reference_cli not found at {cli}")
    out = tmp_path / "features.json"
    subprocess.run(
        [str(cli), "render", str(SMOKE), "--features", str(out)],
        check=True,
        capture_output=True,
        text=True,
    )
    from_cli = json.loads(out.read_text(encoding="utf-8"))
    from_python = ref.render(_scenario())["features"]
    assert from_python == from_cli


def test_p0_5_r7_python_accepts_json_text_and_rejects_unmodelled_input() -> None:
    """P0.5-R7: a JSON string works like a dict; b2 != 0 raises a ValueError naming it."""
    ref = _module()
    scenario = _scenario()
    from_text = ref.render(json.dumps(scenario))
    from_dict = ref.render(scenario)
    assert from_text["features"] == from_dict["features"]
    assert np.array_equal(from_text["signals"]["y"], from_dict["signals"]["y"])
    scenario["strings"][0]["params"]["b2"] = 0.01
    with pytest.raises(ValueError, match="b2"):
        ref.render(scenario)
    with pytest.raises(ValueError, match="schema_version"):
        ref.render({"schema_version": 2, "fs": 48000, "duration": 1})
