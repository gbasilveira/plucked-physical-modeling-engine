# SPDX-License-Identifier: MIT
"""P0.3-T1 independent reviewer check: signals the implementer did not use.

12 partials of a stiff string (f0 = 110 Hz, B = 1e-4, sigma_n = 0.2 + 2e-6 f_n^2, 48 kHz, 3 s) and a
pair of partials 1.2 Hz apart near 440 Hz. Same tolerances as P0.3-T1 (0.01 cent, 0.5 %).
"""

from __future__ import annotations

import math

import numpy as np
from numpy.typing import NDArray
from pme_analysis import PartialTrackingOptions, fit_inharmonicity, track_partials

FS = 48000.0
DURATION_S = 3.0


def synth(
    freqs: list[float], sigmas: list[float], amps: list[float], phases: list[float]
) -> NDArray[np.float64]:
    t = np.arange(round(DURATION_S * FS)) / FS
    x = np.zeros_like(t)
    for f, s, a, p in zip(freqs, sigmas, amps, phases, strict=True):
        x += a * np.exp(-s * t) * np.cos(2.0 * math.pi * f * t + p)
    return x


def assert_recovered(
    freqs: list[float], sigmas: list[float], opt: PartialTrackingOptions, x: NDArray[np.float64]
) -> None:
    result = track_partials(x, FS, opt)
    assert len(result.partials) == len(freqs)
    for est, f, s in zip(result.partials, freqs, sigmas, strict=True):
        cents = abs(1200.0 * math.log2(est.frequency_hz / f))
        decay = abs(est.sigma_per_s / s - 1.0)
        assert cents <= 0.01, f"{f} Hz: {cents} cent"
        assert decay <= 0.005, f"{f} Hz: {100 * decay} % decay"


def test_p0_3_t1_review_twelve_stiff_partials() -> None:
    """P0.3-T1: reviewer signal, 12 stiff-string partials with frequency-dependent decay."""
    f0, b = 110.0, 1e-4
    ns = list(range(1, 13))
    freqs = [n * f0 * math.sqrt(1.0 + b * n * n) for n in ns]
    sigmas = [0.2 + 2e-6 * f * f for f in freqs]
    amps = [1.0 / n for n in ns]
    phases = [0.7 * n + 0.3 for n in ns]
    x = synth(freqs, sigmas, amps, phases)
    opt = PartialTrackingOptions(
        approx_frequencies_hz=[f * (1.0 + 2e-4) for f in freqs], envelope_frame_s=0.0
    )
    assert_recovered(freqs, sigmas, opt, x)
    result = track_partials(x, FS, opt)
    fit = fit_inharmonicity(ns, [p.frequency_hz for p in result.partials])
    assert abs(fit.b / b - 1.0) <= 1e-3
    assert abs(1200.0 * math.log2(fit.f0_hz / f0)) <= 0.01
    # And by peak picking, without hints.
    pick = PartialTrackingOptions(envelope_frame_s=0.0, peak_threshold_db=100.0)
    assert_recovered(freqs, sigmas, pick, x)


def test_p0_3_t1_review_pair_1p2_hz_apart() -> None:
    """P0.3-T1: reviewer signal, two partials 1.2 Hz apart near 440 Hz."""
    freqs = [440.0, 441.2]
    sigmas = [1.5, 2.2]
    x = synth(freqs, sigmas, [1.0, 0.6], [0.4, 2.1])
    opt = PartialTrackingOptions(approx_frequencies_hz=[439.95, 441.25], envelope_frame_s=0.0)
    assert_recovered(freqs, sigmas, opt, x)
