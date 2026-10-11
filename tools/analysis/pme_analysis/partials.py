# SPDX-License-Identifier: MIT
"""Partial tracking (P0.3-R3): Python mirror of ``pme/analysis/partials.hpp``.

Frequency, decay rate, amplitude and phase of the damped sinusoids in a signal, fitted jointly in
the frequency domain against the closed-form spectrum of a Hann-windowed damped exponential, plus
amplitude envelopes over time. Serves SPEC-ORC rows ORC-01, ORC-07 and ORC-16 and the golden
features. The algorithm is the C++ one; results agree within the P0.3-T1 tolerances.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np
from numpy.typing import NDArray

FloatArray = NDArray[np.float64]
ComplexArray = NDArray[np.complex128]

_TWO_PI = 2.0 * math.pi
_PARAMS = 4  # frequency, sigma, Re c, Im c


@dataclass
class PartialTrackingOptions:
    """Options of :func:`track_partials` (see the C++ header for the meaning of each field)."""

    approx_frequencies_hz: list[float] = field(default_factory=list)
    peak_threshold_db: float = 60.0
    max_partials: int = 64
    min_separation_hz: float = 0.0
    envelope_frame_s: float = 0.25
    max_iterations: int = 100


@dataclass
class PartialEstimate:
    """x(t) ~ amplitude exp(-sigma t) cos(2 pi frequency t + phase), t = n / sample_rate."""

    frequency_hz: float
    sigma_per_s: float
    t60_s: float
    amplitude: float
    phase_rad: float
    envelope_times_s: list[float] = field(default_factory=list)
    envelope_amplitudes: list[float] = field(default_factory=list)


@dataclass
class PartialTrackingResult:
    partials: list[PartialEstimate]
    relative_residual: float
    iterations: int
    converged: bool


# ---- Closed-form spectrum of a Hann-windowed damped exponential --------------------------------


def _ratio(
    a: float, b: FloatArray, one_minus_q: ComplexArray, n_q: ComplexArray, n: float
) -> tuple[ComplexArray, ComplexArray]:
    """(1 - Q) / (1 - exp(v)) and its derivative with respect to v = a + i b."""
    ea = math.exp(a)
    half_sin = np.sin(0.5 * b)
    den = (-math.expm1(a) + 2.0 * ea * half_sin * half_sin) + 1j * (-ea * np.sin(b))
    small = np.abs(den) < 1e-10
    safe = np.where(small, 1.0 + 0.0j, den)
    g = one_minus_q / safe
    dg = -n_q / safe + one_minus_q * (1.0 - safe) / (safe * safe)
    if np.any(small):
        v = a + 1j * b
        g_series = n + v * (n * (n - 1.0) / 2.0) + v * v * ((n - 1.0) * n * (2.0 * n - 1.0) / 6.0)
        dg_series = n * (n - 1.0) / 2.0 + v * ((n - 1.0) * n * (2.0 * n - 1.0) / 3.0)
        g = np.where(small, g_series, g)
        dg = np.where(small, dg_series, dg)
    return g, dg


def _hann_dtft(
    u: complex, nu: FloatArray, n: float, theta: float
) -> tuple[ComplexArray, ComplexArray]:
    """S(u) and dS/du for bins ``nu`` (see partials.cpp for the derivation)."""
    a = u.real
    b = u.imag - nu
    n_a = n * a
    n_b = n * b
    e_na = math.exp(n_a)
    half_sin = np.sin(0.5 * n_b)
    one_minus_q = (-math.expm1(n_a) + 2.0 * e_na * half_sin * half_sin) + 1j * (-e_na * np.sin(n_b))
    n_q = n * e_na * (np.cos(n_b) + 1j * np.sin(n_b))
    g0, d0 = _ratio(a, b, one_minus_q, n_q, n)
    gp, dp = _ratio(a, b + theta, one_minus_q, n_q, n)
    gm, dm = _ratio(a, b - theta, one_minus_q, n_q, n)
    return 0.5 * g0 - 0.25 * (gp + gm), 0.5 * d0 - 0.25 * (dp + dm)


@dataclass
class _Spectrum:
    fs: float
    n: float
    m: int
    theta: float
    x: ComplexArray  # bins 0 .. M/2

    @property
    def bin_hz(self) -> float:
        return self.fs / self.m

    @property
    def duration(self) -> float:
        return self.n / self.fs

    def nu(self, bins: NDArray[np.intp]) -> FloatArray:
        return _TWO_PI * bins.astype(np.float64) / self.m


def _next_pow2(n: int) -> int:
    return 1 if n <= 1 else 1 << (n - 1).bit_length()


def _spectrum(signal: FloatArray, fs: float) -> _Spectrum:
    n = signal.size
    theta = _TWO_PI / n
    window = 0.5 - 0.5 * np.cos(theta * np.arange(n))
    m = _next_pow2(n)
    x = np.fft.rfft(signal * window, n=m).astype(np.complex128)
    return _Spectrum(fs=fs, n=float(n), m=m, theta=theta, x=x)


def _evaluate(
    sp: _Spectrum, params: FloatArray, bins: NDArray[np.intp], jacobian: bool
) -> tuple[FloatArray, FloatArray | None, float]:
    """Residual (Re, Im interleaved per bin), optional d model / d params, squared residual norm."""
    nu = sp.nu(bins)
    model = np.zeros(bins.size, dtype=np.complex128)
    jac = np.zeros((2 * bins.size, _PARAMS * params.shape[0])) if jacobian else None
    k_f = _TWO_PI / sp.fs
    for k in range(params.shape[0]):
        f, sigma, alpha, beta = params[k]
        c = complex(alpha, beta)
        up = complex(-sigma / sp.fs, k_f * f)
        um = complex(-sigma / sp.fs, -k_f * f)
        sp_s, sp_d = _hann_dtft(up, nu, sp.n, sp.theta)
        sm_s, sm_d = _hann_dtft(um, nu, sp.n, sp.theta)
        model += 0.5 * (c * sp_s + np.conj(c) * sm_s)
        if jac is not None:
            derivs = (
                0.5 * (c * sp_d * (1j * k_f) + np.conj(c) * sm_d * (-1j * k_f)),
                0.5 * (c * sp_d + np.conj(c) * sm_d) * (-1.0 / sp.fs),
                0.5 * (sp_s + sm_s),
                0.5j * (sp_s - sm_s),
            )
            for q, d in enumerate(derivs):
                jac[0::2, _PARAMS * k + q] = d.real
                jac[1::2, _PARAMS * k + q] = d.imag
    r = sp.x[bins] - model
    residual = np.empty(2 * bins.size)
    residual[0::2] = r.real
    residual[1::2] = r.imag
    return residual, jac, float(residual @ residual)


def _solve_amplitudes(sp: _Spectrum, params: FloatArray, bins: NDArray[np.intp]) -> None:
    residual, jac, _ = _evaluate(sp, params, bins, True)
    assert jac is not None
    cols = [_PARAMS * k + q for k in range(params.shape[0]) for q in (2, 3)]
    delta = np.linalg.lstsq(jac[:, cols], residual, rcond=None)[0]
    params[:, 2] += delta[0::2]
    params[:, 3] += delta[1::2]


def _levenberg_marquardt(
    sp: _Spectrum, params: FloatArray, bins: NDArray[np.intp], max_iterations: int
) -> tuple[int, bool, float]:
    """Fits every parameter of every partial in place; returns (iterations, converged, rel. residual)."""
    data_norm2 = float(np.sum(np.abs(sp.x[bins]) ** 2))
    _solve_amplitudes(sp, params, bins)
    residual, jac, cost = _evaluate(sp, params, bins, True)
    assert jac is not None
    lam = 1e-9
    count = _PARAMS * params.shape[0]
    converged = False
    iterations = 0
    for it in range(max_iterations):
        iterations = it + 1
        scale = np.linalg.norm(jac, axis=0)
        scale[scale == 0.0] = 1.0
        accepted = False
        step_measure = math.inf
        for _ in range(30):
            aug = np.vstack([jac / scale, math.sqrt(lam) * np.eye(count)])
            rhs = np.concatenate([residual, np.zeros(count)])
            try:
                delta = np.linalg.lstsq(aug, rhs, rcond=None)[0] / scale
            except np.linalg.LinAlgError:
                lam *= 10.0
                continue
            trial = params + delta.reshape(-1, _PARAMS)
            step_measure = float(
                max(
                    np.max(np.abs(delta[0::_PARAMS])) * sp.duration,
                    np.max(np.abs(delta[1::_PARAMS])) * sp.duration,
                )
            )
            with np.errstate(all="ignore"):
                t_res, t_jac, t_cost = _evaluate(sp, trial, bins, True)
            if math.isfinite(t_cost) and t_cost <= cost:
                params[:] = trial
                residual, jac, cost = t_res, t_jac, t_cost
                assert jac is not None
                lam = max(lam * 0.1, 1e-15)
                accepted = True
                break
            lam *= 10.0
        if not accepted:
            converged = True
            break
        if step_measure < 1e-10:
            converged = True
            break
    rel = math.sqrt(cost / data_norm2) if data_norm2 > 0.0 else 0.0
    return iterations, converged, rel


# ---- Initial estimates -------------------------------------------------------------------------


def _bins_near(sp: _Spectrum, freqs: list[float], radius: int) -> NDArray[np.intp]:
    top = sp.m // 2
    used = np.zeros(top + 1, dtype=bool)
    for f in freqs:
        centre = int(round(f / sp.bin_hz))
        used[max(centre - radius, 0) : min(centre + radius, top) + 1] = True
    return np.nonzero(used)[0].astype(np.intp)


def _refine_peak(x: ComplexArray, m: int) -> float:
    lg, c, r = (math.log(abs(x[i]) + 1e-300) for i in (m - 1, m, m + 1))
    denom = lg - 2.0 * c + r
    delta = 0.5 * (lg - r) / denom if denom < 0.0 else 0.0
    return m + min(max(delta, -0.5), 0.5)


def _initial_sigma(signal: FloatArray, fs: float) -> float:
    half = signal.size // 2
    e1 = float(np.sum(signal[:half] ** 2))
    e2 = float(np.sum(signal[half : 2 * half] ** 2))
    duration = signal.size / fs
    sigma = 1.0 / duration
    if e1 > 0.0 and e2 > 0.0 and e1 > e2:
        sigma = math.log(e1 / e2) / duration
    return min(max(sigma, 0.05 / duration), 100.0 / duration)


def _snap_to_peak(sp: _Spectrum, allf: list[float], index: int) -> float:
    approx = allf[index]
    natural = 1.0 / sp.duration
    if any(j != index and abs(allf[j] - approx) < 4.0 * natural for j in range(len(allf))):
        return approx
    centre = approx / sp.bin_hz
    half_width = int(1.5 * sp.m / sp.n) + 1
    lo = int(centre) - half_width
    hi = int(centre) + half_width
    if lo < 1 or hi > sp.m // 2 - 1:
        return approx
    best = lo + int(np.argmax(np.abs(sp.x[lo : hi + 1])))
    if best in (lo, hi):
        return approx
    return _refine_peak(sp.x, best) * sp.bin_hz


def _pick_peaks(sp: _Spectrum, opt: PartialTrackingOptions, sigma0: float) -> list[float]:
    """Peak picking by successive subtraction of fitted peaks (see partials.cpp)."""
    work = _Spectrum(sp.fs, sp.n, sp.m, sp.theta, sp.x.copy())
    half = sp.m // 2
    per_natural = sp.m / sp.n
    first = max(2, int(1.5 * per_natural))
    fit_radius = math.ceil(8.0 * per_natural)
    clean_radius = math.ceil(64.0 * per_natural)
    min_sep = opt.min_separation_hz if opt.min_separation_hz > 0.0 else 3.0 / sp.duration
    strongest = 0.0
    freqs: list[float] = []
    for rnd in range(2 * opt.max_partials + 8):
        if len(freqs) >= opt.max_partials:
            break
        mag = np.abs(work.x)
        idx = np.arange(first, half - 1)
        is_peak = (mag[idx] > mag[idx - 1]) & (mag[idx] >= mag[idx + 1])
        cand = idx[is_peak]
        if cand.size == 0:
            break
        best = int(cand[np.argmax(mag[cand])])
        best_mag = float(mag[best])
        if rnd == 0:
            strongest = best_mag
        if best_mag < strongest * 10.0 ** (-opt.peak_threshold_db / 20.0):
            break
        f = _refine_peak(work.x, best) * work.bin_hz
        single = np.array([[f, sigma0, 0.0, 0.0]])
        fitted = False
        try:
            _levenberg_marquardt(work, single, _bins_near(work, [f], fit_radius), 40)
            fitted = bool(
                np.all(np.isfinite(single)) and abs(single[0, 0] - f) < 2.0 / work.duration
            )
        except np.linalg.LinAlgError:
            fitted = False
        if fitted:
            f = float(single[0, 0])
            region = _bins_near(work, [f], clean_radius)
            residual, _, _ = _evaluate(work, single, region, False)
            work.x[region] = residual[0::2] + 1j * residual[1::2]
        else:
            work.x[_bins_near(work, [f], fit_radius)] = 0.0
        if fitted and not any(abs(g - f) < min_sep for g in freqs):
            freqs.append(f)
    return sorted(freqs)


# ---- Envelope ----------------------------------------------------------------------------------


def _envelopes(
    signal: FloatArray, fs: float, partials: list[PartialEstimate], frame_s: float
) -> None:
    frame_len = round(frame_s * fs)
    k_count = len(partials)
    if frame_len < 8 or k_count == 0 or frame_len < 2 * k_count:
        return
    freq = np.array([p.frequency_hz for p in partials])
    sigma = np.array([p.sigma_per_s for p in partials])
    for frame in range(signal.size // frame_len):
        start = frame * frame_len
        centre = (start + 0.5 * frame_len) / fs
        dt = (start + np.arange(frame_len)) / fs - centre
        env = np.exp(-np.outer(dt, sigma))
        angle = _TWO_PI * np.outer(dt, freq)
        a = np.empty((frame_len, 2 * k_count))
        a[:, 0::2] = env * np.cos(angle)
        a[:, 1::2] = -env * np.sin(angle)
        coeffs = np.linalg.lstsq(a, signal[start : start + frame_len], rcond=None)[0]
        amps = np.hypot(coeffs[0::2], coeffs[1::2])
        for k, p in enumerate(partials):
            p.envelope_times_s.append(float(centre))
            p.envelope_amplitudes.append(float(amps[k]))


def track_partials(
    signal: FloatArray | list[float],
    sample_rate_hz: float,
    options: PartialTrackingOptions | None = None,
) -> PartialTrackingResult:
    """Fit all partials jointly (see the C++ ``track_partials``)."""
    opt = options or PartialTrackingOptions()
    x = np.asarray(signal, dtype=np.float64)
    if x.size < 16 or not sample_rate_hz > 0.0:
        raise ValueError("track_partials: need >= 16 samples and a positive rate")
    sp = _spectrum(x, sample_rate_hz)
    sigma0 = _initial_sigma(x, sample_rate_hz)
    if opt.approx_frequencies_hz:
        approx = list(opt.approx_frequencies_hz)
        freqs = [_snap_to_peak(sp, approx, k) for k in range(len(approx))]
    else:
        freqs = _pick_peaks(sp, opt, sigma0)
    if not freqs:
        return PartialTrackingResult([], 0.0, 0, False)

    radius = math.ceil(8.0 * sp.m / sp.n)
    params = np.zeros((len(freqs), _PARAMS))
    params[:, 0] = freqs
    params[:, 1] = sigma0
    for k, f in enumerate(freqs):
        single = params[k : k + 1].copy()
        try:
            _levenberg_marquardt(sp, single, _bins_near(sp, [f], radius), 40)
        except np.linalg.LinAlgError:
            continue
        if np.all(np.isfinite(single)) and abs(single[0, 0] - f) < 2.0 / sp.duration:
            params[k] = single[0]
    iterations, converged, rel = _levenberg_marquardt(
        sp, params, _bins_near(sp, freqs, radius), opt.max_iterations
    )
    partials = []
    for f, sigma, alpha, beta in params:
        t60 = 3.0 * math.log(10.0) / sigma if sigma > 0.0 else math.inf
        partials.append(
            PartialEstimate(
                frequency_hz=float(f),
                sigma_per_s=float(sigma),
                t60_s=t60,
                amplitude=float(math.hypot(alpha, beta)),
                phase_rad=float(math.atan2(beta, alpha)),
            )
        )
    if opt.envelope_frame_s > 0.0:
        _envelopes(x, sample_rate_hz, partials, opt.envelope_frame_s)
    return PartialTrackingResult(partials, rel, iterations, converged)
