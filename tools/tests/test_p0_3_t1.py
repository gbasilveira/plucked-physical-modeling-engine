# SPDX-License-Identifier: MIT
"""P0.3-T1 (Python): the analysis tools recover frequencies within 0.01 cent and decay rates
within 0.5% from synthetic sums of damped sinusoids.

Reads the same signal definitions as the C++ tests (tools/analysis/testdata/p0_3_t1_signals.json).
"""

from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Any

import numpy as np
import pytest
from numpy.typing import NDArray
from pme_analysis import (
    PartialTrackingOptions,
    PartialTrackingResult,
    fit_inharmonicity,
    track_partials,
)

DATA = Path(__file__).resolve().parents[1] / "analysis" / "testdata" / "p0_3_t1_signals.json"


def load_cases() -> list[dict[str, Any]]:
    cases: list[dict[str, Any]] = json.loads(DATA.read_text(encoding="utf-8"))["cases"]
    return cases


def synthesize(case: dict[str, Any], fs: float) -> NDArray[np.float64]:
    n = round(case["duration_s"] * fs)
    t = np.arange(n) / fs
    x = np.zeros(n)
    for p in case["partials"]:
        x += (
            p["amplitude"]
            * np.exp(-p["sigma_per_s"] * t)
            * np.cos(2.0 * math.pi * p["frequency_hz"] * t + p["phase_rad"])
        )
    return x


def cents(a: float, b: float) -> float:
    return 1200.0 * math.log2(a / b)


def check(case: dict[str, Any], result: PartialTrackingResult) -> tuple[float, float]:
    """Assert the P0.3-T1 pass criteria; return the worst (cents, percent decay) errors."""
    assert len(result.partials) == len(case["partials"])
    worst_c = worst_d = 0.0
    for est, truth in zip(result.partials, case["partials"], strict=True):
        c = abs(cents(est.frequency_hz, truth["frequency_hz"]))
        d = abs(est.sigma_per_s / truth["sigma_per_s"] - 1.0)
        assert c <= 0.01, f"{case['name']}: {c} cent at {truth['frequency_hz']} Hz"
        assert d <= 0.005, f"{case['name']}: {100 * d} % decay at {truth['frequency_hz']} Hz"
        worst_c, worst_d = max(worst_c, c), max(worst_d, 100 * d)
    return worst_c, worst_d


def options(case: dict[str, Any], offset: float, **kw: Any) -> PartialTrackingOptions:
    return PartialTrackingOptions(
        approx_frequencies_hz=[p["frequency_hz"] * (1.0 + offset) for p in case["partials"]],
        envelope_frame_s=0.0,
        **kw,
    )


def cases_of(kind: str) -> list[dict[str, Any]]:
    return [c for c in load_cases() if c["case"] == kind]


@pytest.mark.parametrize("case", cases_of("A"), ids=lambda c: c["name"])
def test_p0_3_t1_case_a_single_partial(case: dict[str, Any]) -> None:
    """P0.3-T1: case A recovers a single partial at 27.5, 261.6 and 4186 Hz."""
    fs = case["sample_rates_hz"][0]
    result = track_partials(synthesize(case, fs), fs, options(case, 3e-4))
    check(case, result)
    assert result.converged


@pytest.mark.parametrize("case", cases_of("A"), ids=lambda c: c["name"])
def test_p0_3_t1_case_a_peak_picking(case: dict[str, Any]) -> None:
    """P0.3-T1: case A by peak picking needs no frequency hint."""
    fs = case["sample_rates_hz"][0]
    check(
        case,
        track_partials(synthesize(case, fs), fs, PartialTrackingOptions(envelope_frame_s=0.0)),
    )


@pytest.mark.parametrize("fs", [44100.0, 48000.0, 96000.0])
def test_p0_3_t1_case_b_stiff_string(fs: float) -> None:
    """P0.3-T1: case B recovers 30 stiff-string partials at 44.1, 48 and 96 kHz."""
    (case,) = cases_of("B")
    result = track_partials(synthesize(case, fs), fs, options(case, 2e-4))
    check(case, result)
    assert result.relative_residual < 1e-8


def test_p0_3_t1_case_b_peak_picking() -> None:
    """P0.3-T1: case B by peak picking finds all 30 partials."""
    (case,) = cases_of("B")
    opt = PartialTrackingOptions(envelope_frame_s=0.0, peak_threshold_db=100.0)
    check(case, track_partials(synthesize(case, 48000.0), 48000.0, opt))


def test_p0_3_t1_case_c_unison() -> None:
    """P0.3-T1: case C separates unison partials 1 Hz apart."""
    (case,) = cases_of("C")
    opt = PartialTrackingOptions(
        approx_frequencies_hz=[
            case["partials"][0]["frequency_hz"] + 0.05,
            case["partials"][1]["frequency_hz"] - 0.05,
        ],
        envelope_frame_s=0.0,
    )
    check(case, track_partials(synthesize(case, 48000.0), 48000.0, opt))


def test_p0_3_r3_case_b_inharmonicity() -> None:
    """P0.3-R3: the case B inharmonicity fit recovers f0 (0.01 cent) and B (0.1%)."""
    (case,) = cases_of("B")
    result = track_partials(synthesize(case, 48000.0), 48000.0, options(case, 2e-4))
    fit = fit_inharmonicity(case["partial_numbers"], [p.frequency_hz for p in result.partials])
    assert abs(fit.b / case["inharmonicity_b"] - 1.0) <= 1e-3
    assert abs(cents(fit.f0_hz, case["f0_hz"])) <= 0.01


def test_p0_3_r3_envelope_and_phase() -> None:
    """P0.3-R3: partial tracking returns the amplitude envelope and the phase at t = 0."""
    (case,) = cases_of("C")
    opt = options(case, 1e-4)
    opt.envelope_frame_s = 0.5
    result = track_partials(synthesize(case, 48000.0), 48000.0, opt)
    for est, truth in zip(result.partials, case["partials"], strict=True):
        assert est.amplitude == pytest.approx(truth["amplitude"], rel=1e-6)
        assert est.phase_rad == pytest.approx(truth["phase_rad"], abs=1e-6)
        assert len(est.envelope_times_s) == 8
        for t, a in zip(est.envelope_times_s, est.envelope_amplitudes, strict=True):
            assert a == pytest.approx(
                truth["amplitude"] * math.exp(-truth["sigma_per_s"] * t), rel=1e-5
            )


@pytest.mark.parametrize("case", cases_of("A"), ids=lambda c: c["name"])
def test_p0_3_t1_case_a_float32_input(case: dict[str, Any]) -> None:
    """P0.3-T1: case A survives rounding the signal to float32 (achieved accuracy is reported)."""
    fs = case["sample_rates_hz"][0]
    x = synthesize(case, fs).astype(np.float32).astype(np.float64)
    result = track_partials(x, fs, options(case, 3e-4))
    check(case, result)
