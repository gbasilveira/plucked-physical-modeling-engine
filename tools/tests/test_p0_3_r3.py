# SPDX-License-Identifier: MIT
"""P0.3-R3 (Python): decay and T60 fits, double decay, inharmonicity, contact time, spectral
envelope and energy accounting; mirrors the C++ tests in tools/analysis/tests/measures_tests.cpp."""

from __future__ import annotations

import math

import numpy as np
import pytest
from pme_analysis import (
    SpectralEnvelopeOptions,
    account_energy,
    fit_decay,
    fit_double_decay,
    fit_inharmonicity,
    measure_contact,
    spectral_envelope,
    t60_from_sigma,
)


def test_p0_3_r3_t60_follows_three_ln10_over_sigma() -> None:
    """P0.3-R3: T60 = 3 ln(10) / sigma, infinite for non-positive sigma."""
    assert t60_from_sigma(3.0 * math.log(10.0) / 20.0) == pytest.approx(20.0, rel=1e-12)
    assert math.isinf(t60_from_sigma(0.0))
    assert math.isinf(t60_from_sigma(-1.0))


def test_p0_3_r3_decay_fit_recovers_sigma_and_amplitude() -> None:
    """P0.3-R3: the decay fit recovers sigma and the amplitude at t = 0 from an exponential."""
    t = 0.1 * np.arange(40)
    fit = fit_decay(t, 0.7 * np.exp(-1.25 * t))
    assert fit.sigma_per_s == pytest.approx(1.25, rel=1e-12)
    assert math.exp(fit.log_amplitude_0) == pytest.approx(0.7, rel=1e-12)
    assert fit.t60_s == pytest.approx(t60_from_sigma(1.25), rel=1e-12)
    assert fit.rms_log_residual < 1e-12
    with pytest.raises(ValueError, match="positive"):
        fit_decay(t, np.zeros(40))


def test_p0_3_r3_double_decay_fit_returns_both_rates_and_knee() -> None:
    """P0.3-R3: the two-slope fit returns both rates and the knee time."""
    sigma1, sigma2, knee = 2.0, 0.4, 1.337
    t = 0.05 * np.arange(100)
    ln_a = np.where(t < knee, -sigma1 * t, -sigma1 * knee - sigma2 * (t - knee))
    fit = fit_double_decay(t, np.exp(ln_a))
    assert fit.sigma1_per_s == pytest.approx(sigma1, rel=1e-6)
    assert fit.sigma2_per_s == pytest.approx(sigma2, rel=1e-6)
    assert fit.knee_s == pytest.approx(knee, rel=1e-6)
    assert fit.rms_log_residual < 1e-9


def test_p0_3_r3_inharmonicity_fit_recovers_f0_and_b() -> None:
    """P0.3-R3: the inharmonicity fit recovers f0 and B from exact partials (SPEC-STR)."""
    f0, b = 110.3, 2.0e-4
    n = np.arange(1, 25)
    fit = fit_inharmonicity(n, n * f0 * np.sqrt(1.0 + b * n * n))
    assert fit.f0_hz == pytest.approx(f0, rel=1e-9)
    assert fit.b == pytest.approx(b, rel=1e-7)
    assert fit.rms_cents < 1e-6
    with pytest.raises(ValueError, match="3"):
        fit_inharmonicity([1, 2], [100.0, 200.0])


def test_p0_3_r3_contact_time_of_half_sine_is_sub_sample_accurate() -> None:
    """P0.3-R3: contact time of a half-sine pulse is accurate to a fraction of a sample (ORC-08)."""
    fs, width, threshold = 48000.0, 0.003, 0.01
    expected = width * (1.0 - 2.0 * math.asin(threshold) / math.pi)
    for offset in (0.0, 0.13, 0.5, 0.77, 0.999):
        t0 = 0.0101 + offset / fs
        t = np.arange(1000) / fs
        force = np.where((t >= t0) & (t <= t0 + width), np.sin(math.pi * (t - t0) / width), 0.0)
        c = measure_contact(force, fs, threshold)
        assert c.found
        assert abs(c.duration_s - expected) * fs < 0.15
    assert not measure_contact(np.zeros(10), fs, 0.5).found


def test_p0_3_r3_spectral_envelope_of_one_pole_matches_response() -> None:
    """P0.3-R3: the spectral envelope of a one-pole low-pass matches its band-averaged response."""
    fs = 48000.0
    pole = math.exp(-2.0 * math.pi * 1000.0 / fs)
    h = (1.0 - pole) * pole ** np.arange(65536)
    env = spectral_envelope(
        h, fs, SpectralEnvelopeOptions(window="rectangular", normalisation="raw")
    )
    assert env.frequencies_hz[0] == 20.0
    assert env.frequencies_hz[-1] <= 20000.0

    def power(f: np.ndarray) -> np.ndarray:
        w = 2.0 * math.pi * f / fs
        return (1.0 - pole) ** 2 / (1.0 - 2.0 * pole * np.cos(w) + pole * pole)

    worst = 0.0
    for fc, db in zip(env.frequencies_hz, env.magnitude_db, strict=True):
        band = np.linspace(fc * 2 ** (-1 / 6), fc * 2 ** (1 / 6), 2000)
        worst = max(worst, abs(db - 10.0 * math.log10(float(np.mean(power(band))))))
    assert worst < 0.05
    assert env.magnitude_db[0] - env.magnitude_db[-1] > 20.0


def test_p0_3_r3_energy_accounting_reports_drift_and_monotonicity() -> None:
    """P0.3-R3: energy drift, per-step change and the non-increasing check (ORC-02, ORC-09, ORC-15)."""
    h = np.full(1000, 2.0)
    flat = account_energy(h)
    assert flat.max_relative_drift == 0.0 and flat.non_increasing

    h[500] = 2.0 * (1.0 + 3e-10)
    blip = account_energy(h, 1e-9)
    assert blip.max_relative_drift == pytest.approx(3e-10, rel=1e-6)
    assert blip.max_relative_step_change == pytest.approx(3e-10, rel=1e-6)
    assert blip.non_increasing
    strict = account_energy(h, 1e-11)
    assert not strict.non_increasing and strict.first_increase_index == 499

    decaying = np.exp(-0.01 * np.arange(100))
    d = account_energy(decaying)
    assert d.non_increasing and d.max_relative_increase == 0.0
    assert d.max_relative_step_change == pytest.approx(1.0 - math.exp(-0.01), rel=1e-12)
    assert account_energy([]).non_increasing
