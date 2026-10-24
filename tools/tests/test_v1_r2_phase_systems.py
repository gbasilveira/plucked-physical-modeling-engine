# SPDX-License-Identifier: MIT
"""V1-R2: a phase that exists on fewer systems than linux,macos,windows is judged on its own.

D3 (LV2) is Linux-only by its card ("a native LV2 build for Linux hosts"), so a test that passes
on Linux alone is complete for D3 and for no other phase.
"""

from __future__ import annotations

import sys
from pathlib import Path

TRACE = Path(__file__).resolve().parents[1] / "trace"
sys.path.insert(0, str(TRACE))

from trace_matrix.ids import PHASE_SYSTEMS  # noqa: E402
from trace_matrix.matrix import Evaluator, Inputs  # noqa: E402
from trace_matrix.results import TestResult  # noqa: E402


def evaluator(test_id: str, systems: tuple[str, ...], required: tuple[str, ...]) -> Evaluator:
    name = f"{test_id} some test"
    results = [TestResult(name, "pass", f"junit-{s}", s, "c", (), f"{s}.xml") for s in systems]
    inputs = Inputs([], [], {}, {}, [], [], [], results, required_systems=required)
    return Evaluator(inputs)


ALL = ("linux", "macos", "windows")


def test_v1_r2_d3_is_linux_only_by_its_card() -> None:
    """V1-R2: see the module docstring."""
    assert PHASE_SYSTEMS == {"D3": ("linux",)}


def test_v1_r2_linux_only_phase_passes_with_a_linux_result() -> None:
    """V1-R2: see the module docstring."""
    ev = evaluator("D3-T1", ("linux",), ALL)
    st = ev.status("D3-T1", "D3")
    assert st.status == "pass"
    assert st.systems_missing == []


def test_v1_r2_other_phases_still_need_every_system() -> None:
    """V1-R2: see the module docstring."""
    ev = evaluator("D2-T1", ("linux",), ALL)
    st = ev.status("D2-T1", "D2")
    assert st.status == "not_run_all_systems"
    assert st.systems_missing == ["macos", "windows"]


def test_v1_r2_linux_only_phase_still_needs_linux() -> None:
    """V1-R2: see the module docstring."""
    ev = evaluator("D3-T1", ("macos",), ALL)
    st = ev.status("D3-T1", "D3")
    assert st.status == "not_run_all_systems"
    assert st.systems_missing == ["linux"]


def test_v1_r2_local_linux_run_is_unchanged() -> None:
    """V1-R2: see the module docstring."""
    ev = evaluator("D3-T1", ("linux",), ("linux",))
    assert ev.status("D3-T1", "D3").status == "pass"
