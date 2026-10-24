# SPDX-License-Identifier: MIT
"""P0.2-R6: JUnit summary and the coverage gate."""

from __future__ import annotations

from pathlib import Path

from ci import coverage_gate, junit_summary

COBERTURA = """<?xml version="1.0"?>
<coverage><packages><package name="p"><classes>
{classes}
</classes></package></packages></coverage>
"""


def _class(filename: str, hits: list[int]) -> str:
    lines = "".join(f'<line number="{i}" hits="{h}"/>' for i, h in enumerate(hits, start=1))
    return f'<class filename="{filename}"><lines>{lines}</lines></class>'


def _report(tmp_path: Path, classes: list[str]) -> Path:
    path = tmp_path / "coverage.xml"
    path.write_text(COBERTURA.format(classes="\n".join(classes)), encoding="utf-8")
    return path


def test_p0_2_r6_gate_passes_on_empty_measured_set(tmp_path: Path, capsys) -> None:  # type: ignore[no-untyped-def]
    """P0.2-R6: placeholders and rtcheck only: "no engine sources measured" and success."""
    report = _report(
        tmp_path,
        [
            _class("engine/src/osc/osc_placeholder.cpp", [1, 0]),
            _class("engine/src/rtcheck/rtcheck.cpp", [0, 0]),
            _class("engine/src/sched/platform/glue.cpp", [0]),
            _class("engine/tests/x.cpp", [0]),
        ],
    )
    assert coverage_gate.main([str(report)]) == 0
    assert "no engine sources measured" in capsys.readouterr().out


def test_p0_2_r6_gate_enforces_threshold(tmp_path: Path) -> None:
    """P0.2-R6: 89% of the measured lines fails, 90% passes."""
    low = _report(tmp_path, [_class("engine/src/osc/osc.cpp", [1] * 89 + [0] * 11)])
    assert coverage_gate.main([str(low), "--threshold", "90"]) == 1
    high = _report(tmp_path, [_class("engine/src/osc/osc.cpp", [1] * 90 + [0] * 10)])
    assert coverage_gate.main([str(high), "--threshold", "90"]) == 0


def test_p0_2_r6_gate_ignores_excluded_files_in_the_ratio(tmp_path: Path) -> None:
    """P0.2-R6: uncovered excluded files do not lower the measured percentage."""
    report = _report(
        tmp_path,
        [
            _class("engine/src/string/string.cpp", [1, 1, 1, 1, 1, 1, 1, 1, 1, 1]),
            _class("engine/src/rtcheck/rtcheck.cpp", [0] * 100),
            _class("engine/src/body/body_placeholder.cpp", [0] * 100),
        ],
    )
    assert coverage_gate.main([str(report)]) == 0


def test_p0_2_r6_junit_summary_counts(tmp_path: Path) -> None:
    """P0.2-R6: the summary reports totals and names failed test cases."""
    xml = tmp_path / "ctest.xml"
    xml.write_text(
        '<testsuite name="x" tests="3" failures="1" errors="0" skipped="1">'
        '<testcase classname="c" name="ok"/>'
        '<testcase classname="c" name="bad"><failure message="m"/></testcase>'
        '<testcase classname="c" name="skip"><skipped/></testcase>'
        "</testsuite>",
        encoding="utf-8",
    )
    text = junit_summary.summarize("title", [xml, tmp_path / "missing.xml"])
    assert "### title" in text
    assert "| `ctest.xml` | 3 | 1 | 0 | 1 |" in text
    assert "`c.bad`" in text
    assert "unreadable" in text
