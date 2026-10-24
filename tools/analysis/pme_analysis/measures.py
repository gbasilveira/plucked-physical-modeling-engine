# SPDX-License-Identifier: MIT
"""Scalar measurements (P0.3-R3): Python mirror of ``pme/analysis/measures.hpp``.

Decay rate and T60, double-decay fit, inharmonicity (SPEC-STR), contact time (ORC-08), spectral
envelope (ORC-17) and energy accounting (ORC-02, ORC-09, ORC-15).
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

import numpy as np
from numpy.typing import ArrayLike, NDArray

FloatArray = NDArray[np.float64]


def t60_from_sigma(sigma_per_s: float) -> float:
    """T60 = 3 ln(10) / sigma for amplitude proportional to exp(-sigma t); inf when sigma <= 0."""
    return 3.0 * math.log(10.0) / sigma_per_s if sigma_per_s > 0.0 else math.inf


# ---- Decay -------------------------------------------------------------------------------------


@dataclass
class DecayFit:
    sigma_per_s: float
    log_amplitude_0: float
    t60_s: float
    rms_log_residual: float


@dataclass
class DoubleDecayFit:
    sigma1_per_s: float
    sigma2_per_s: float
    knee_s: float
    log_amplitude_0: float
    rms_log_residual: float


def _log_amplitudes(
    times: ArrayLike, amplitudes: ArrayLike, minimum: int
) -> tuple[FloatArray, FloatArray]:
    t = np.asarray(times, dtype=np.float64)
    a = np.asarray(amplitudes, dtype=np.float64)
    if t.shape != a.shape or t.ndim != 1 or t.size < minimum:
        raise ValueError("decay fit: need matching 1-D arrays with enough points")
    if not np.all(a > 0.0):
        raise ValueError("decay fit: amplitudes must be positive")
    return t, np.log(a)


def fit_decay(times_s: ArrayLike, amplitudes: ArrayLike) -> DecayFit:
    """Least-squares line through ln(amplitude) against time."""
    t, y = _log_amplitudes(times_s, amplitudes, 2)
    design = np.column_stack([np.ones_like(t), t])
    c = np.linalg.lstsq(design, y, rcond=None)[0]
    rms = math.sqrt(float(np.mean((y - design @ c) ** 2)))
    sigma = float(-c[1])
    return DecayFit(sigma, float(c[0]), t60_from_sigma(sigma), rms)


def _solve_knee(t: FloatArray, y: FloatArray, knee: float) -> tuple[float, FloatArray]:
    design = np.column_stack([np.ones_like(t), np.minimum(t, knee), np.maximum(t - knee, 0.0)])
    coeffs = np.linalg.lstsq(design, y, rcond=None)[0]
    return float(np.sum((y - design @ coeffs) ** 2)), coeffs


def fit_double_decay(times_s: ArrayLike, amplitudes: ArrayLike) -> DoubleDecayFit:
    """Continuous two-slope fit of ln(amplitude): grid search for the knee, then golden section."""
    t, y = _log_amplitudes(times_s, amplitudes, 5)
    lo, hi = float(t[1]), float(t[-2])
    grid_n = 200
    grid = lo + (hi - lo) * np.arange(grid_n + 1) / grid_n
    sses = [_solve_knee(t, y, float(k))[0] for k in grid]
    best = float(grid[int(np.argmin(sses))])
    step = (hi - lo) / grid_n
    a, b = max(lo, best - step), min(hi, best + step)
    inv_phi = (math.sqrt(5.0) - 1.0) / 2.0
    c = b - inv_phi * (b - a)
    d = a + inv_phi * (b - a)
    fc = _solve_knee(t, y, c)[0]
    fd = _solve_knee(t, y, d)[0]
    for _ in range(80):
        if fc < fd:
            b, d, fd = d, c, fc
            c = b - inv_phi * (b - a)
            fc = _solve_knee(t, y, c)[0]
        else:
            a, c, fc = c, d, fd
            d = a + inv_phi * (b - a)
            fd = _solve_knee(t, y, d)[0]
    knee = 0.5 * (a + b)
    sse, coeffs = _solve_knee(t, y, knee)
    return DoubleDecayFit(
        float(-coeffs[1]), float(-coeffs[2]), knee, float(coeffs[0]), math.sqrt(sse / t.size)
    )


# ---- Inharmonicity -----------------------------------------------------------------------------


@dataclass
class InharmonicityFit:
    f0_hz: float
    b: float
    rms_cents: float
    max_abs_cents: float


def fit_inharmonicity(
    partial_numbers: Sequence[int] | NDArray[np.int_], frequencies_hz: ArrayLike
) -> InharmonicityFit:
    """Fit f_n = n f0 sqrt(1 + B n^2) (SPEC-STR) by minimising the error in cents."""
    n = np.asarray(partial_numbers, dtype=np.float64)
    f = np.asarray(frequencies_hz, dtype=np.float64)
    if n.shape != f.shape or n.size < 3:
        raise ValueError("fit_inharmonicity: need >= 3 matching partials")
    # Start: (f_n / n)^2 = f0^2 + f0^2 B n^2 is linear in n^2.
    ab = np.linalg.lstsq(np.column_stack([np.ones_like(n), n * n]), (f / n) ** 2, rcond=None)[0]
    f0 = math.sqrt(max(float(ab[0]), 1e-300))
    b = float(ab[1] / ab[0])
    to_cents = 1200.0 / math.log(2.0)

    def residuals(f0v: float, bv: float) -> FloatArray:
        return np.asarray(to_cents * np.log(n * f0v * np.sqrt(1.0 + bv * n * n) / f))

    for _ in range(30):
        r = residuals(f0, b)
        jac = np.column_stack(
            [np.full_like(n, to_cents), to_cents * 0.5 * n * n / (1.0 + b * n * n)]
        )
        scale_b = float(np.max(np.abs(jac[:, 1])))
        jac[:, 1] /= scale_b
        delta = np.linalg.lstsq(jac, -r, rcond=None)[0]
        f0 *= math.exp(float(delta[0]))
        b += float(delta[1]) / scale_b
        if abs(delta[0]) < 1e-15 and abs(delta[1] / scale_b) < 1e-15 * (1.0 + abs(b)):
            break
    r = residuals(f0, b)
    return InharmonicityFit(f0, b, math.sqrt(float(np.mean(r * r))), float(np.max(np.abs(r))))


# ---- Contact time ------------------------------------------------------------------------------


@dataclass
class ContactInterval:
    found: bool
    start_s: float = 0.0
    end_s: float = 0.0
    duration_s: float = 0.0


def measure_contact(force: ArrayLike, sample_rate_hz: float, threshold: float) -> ContactInterval:
    """First upward to last downward crossing of ``threshold`` (linear, sub-sample)."""
    x = np.asarray(force, dtype=np.float64)
    above = np.nonzero(x > threshold)[0]
    if above.size == 0:
        return ContactInterval(False)
    first, last = int(above[0]), int(above[-1])
    dt = 1.0 / sample_rate_hz
    if first == 0:
        start = 0.0
    else:
        f0, f1 = x[first - 1], x[first]
        start = (first - 1 + (threshold - f0) / (f1 - f0)) * dt
    if last + 1 >= x.size:
        end = last * dt
    else:
        f0, f1 = x[last], x[last + 1]
        end = (last + (f0 - threshold) / (f0 - f1)) * dt
    return ContactInterval(True, float(start), float(end), float(end - start))


# ---- Spectral envelope -------------------------------------------------------------------------


@dataclass
class SpectralEnvelopeOptions:
    f_min_hz: float = 20.0
    f_max_hz: float = 20000.0
    points_per_octave: int = 12
    smoothing_octaves: float = 1.0 / 3.0
    window: Literal["hann", "rectangular"] = "hann"
    normalisation: Literal["raw", "coherent"] = "coherent"


@dataclass
class SpectralEnvelope:
    frequencies_hz: FloatArray
    magnitude_db: FloatArray


def spectral_envelope(
    signal: ArrayLike, sample_rate_hz: float, options: SpectralEnvelopeOptions | None = None
) -> SpectralEnvelope:
    """Band-averaged power spectrum on a fixed log-frequency grid, in dB."""
    opt = options or SpectralEnvelopeOptions()
    x = np.asarray(signal, dtype=np.float64)
    if (
        x.size < 8
        or not sample_rate_hz > 0.0
        or opt.points_per_octave < 1
        or not opt.smoothing_octaves > 0.0
        or not opt.f_min_hz > 0.0
    ):
        raise ValueError("spectral_envelope: invalid arguments")
    n = x.size
    m = 2 * (1 if n <= 1 else 1 << (n - 1).bit_length())
    if opt.window == "hann":
        w = 0.5 - 0.5 * np.cos(2.0 * math.pi * np.arange(n) / n)
    else:
        w = np.ones(n)
    spec = np.fft.rfft(x * w, n=m)
    norm = float(np.sum(w)) if opt.normalisation == "coherent" else 1.0
    power = np.abs(spec) ** 2 / (norm * norm)
    half = m // 2
    cum = np.concatenate([[0.0], np.cumsum(power)])

    def cumulative(b: float) -> float:
        t = min(max(b + 0.5, 0.0), float(half + 1))
        k = math.floor(t)
        if k >= half + 1:
            return float(cum[half + 1])
        return float(cum[k] + (t - k) * power[k])

    bin_hz = sample_rate_hz / m
    f_max = min(opt.f_max_hz, 0.5 * sample_rate_hz)
    freqs: list[float] = []
    dbs: list[float] = []
    i = 0
    while True:
        fc = opt.f_min_hz * 2.0 ** (i / opt.points_per_octave)
        i += 1
        if fc > f_max:
            break
        lo = fc * 2.0 ** (-0.5 * opt.smoothing_octaves) / bin_hz
        hi = min(fc * 2.0 ** (0.5 * opt.smoothing_octaves) / bin_hz, float(half))
        if hi > lo:
            mean = (cumulative(hi) - cumulative(lo)) / (hi - lo)
        else:
            mean = float(power[min(round(fc / bin_hz), half)])
        freqs.append(fc)
        dbs.append(10.0 * math.log10(max(mean, 1e-300)))
    return SpectralEnvelope(np.array(freqs), np.array(dbs))


# ---- Energy accounting -------------------------------------------------------------------------


@dataclass
class EnergyReport:
    max_relative_drift: float = 0.0
    max_relative_step_change: float = 0.0
    max_relative_increase: float = 0.0
    non_increasing: bool = True
    first_increase_index: int = 0


def account_energy(energy: ArrayLike, non_increasing_tolerance: float = 0.0) -> EnergyReport:
    """max |H[n]-H[0]|/|H[0]|, max |H[n+1]-H[n]|/|H[n]| and a non-increasing check."""
    h = np.asarray(energy, dtype=np.float64)
    report = EnergyReport()
    if h.size == 0:
        return report
    h0 = abs(h[0])
    drift = np.abs(h - h[0])
    report.max_relative_drift = float(np.max(drift / h0 if h0 > 0.0 else drift))
    if h.size > 1:
        hi = np.abs(h[:-1])
        diff = np.diff(h)
        denom = np.where(hi > 0.0, hi, 1.0)
        rel = np.abs(diff) / denom
        rel_up = np.maximum(diff, 0.0) / denom
        report.max_relative_step_change = float(np.max(rel))
        report.max_relative_increase = float(np.max(rel_up))
        bad = np.nonzero(rel_up > non_increasing_tolerance)[0]
        if bad.size:
            report.non_increasing = False
            report.first_increase_index = int(bad[0])
    return report
