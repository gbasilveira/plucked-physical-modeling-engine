# Signal-analysis library (P0.3)

SPEC sections served: SPEC-ORC (oracle rows ORC-01, ORC-02, ORC-07, ORC-08, ORC-09, ORC-15, ORC-16,
ORC-17), SPEC-STR (inharmonicity formula `f_n = n f0 sqrt(1 + B n^2)`). PLAN line served: PLAN-VAL.
The library implements no engine behaviour and is never linked into the engine.

Two implementations with the same function set:

- **C++**: static library `pme_analysis` (alias `pme::analysis`) in `tools/analysis/`, headers under
  `tools/analysis/include/pme/analysis/`, namespace `pme::analysis`, float64 throughout. The only
  third-party dependency is nlohmann/json (ADR-0005), used inside the golden-feature and benchmark
  code and never in public headers.
- **Python**: package `pme_analysis` in `tools/analysis/pme_analysis/` (NumPy only; `tools/analysis`
  is on the pytest `pythonpath` and the mypy path). Same algorithms, results within the P0.3-T1
  tolerances.

## Functions

| Function (C++ / Python) | Header / module | Serves |
| --- | --- | --- |
| `track_partials` | `partials.hpp` / `partials.py` | ORC-01, ORC-07, ORC-16, golden features |
| `fit_decay`, `t60_from_sigma`, `fit_double_decay` | `measures.hpp` / `measures.py` | ORC-01, ORC-16 |
| `fit_inharmonicity` | `measures.hpp` | SPEC-STR, ORC-03 (measured side) |
| `measure_contact` | `measures.hpp` | ORC-08 |
| `spectral_envelope` | `measures.hpp` | ORC-17, golden features |
| `account_energy` | `measures.hpp` | ORC-02, ORC-09, ORC-15 |
| `parse_golden`, `serialize_golden`, `compare_golden`, `format_comparison` | `golden.hpp` / `golden.py` | golden-feature tests |
| `parse_wav`, `encode_wav`, `read_wav`, `write_wav` | `wav.hpp` / `wav.py` | tools and tests |
| `fft`, `ifft`, `fft_real`, `dft_direct`, `solve_least_squares`, `solve_square` | `fft.hpp`, `linalg.hpp` | internals (C++ only) |

### Partial tracking

Input: a signal, a sample rate and either approximate frequencies or a peak-picking rule. Output per
partial: `frequency_hz`, `sigma_per_s` (amplitude proportional to `exp(-sigma t)`), `t60_s`
(`3 ln(10) / sigma`), `amplitude` and `phase_rad` at t = 0 (`x ~ amplitude exp(-sigma t)
cos(2 pi f t + phase)`), and an amplitude envelope (`envelope_times_s`, `envelope_amplitudes`).

**Method.** FFT peak estimates are not accurate enough for 0.01 cent (about 6 x 10^-6 relative), so
the fit is parametric and done in the frequency domain:

1. The signal is multiplied by a periodic Hann window `w[n] = 1/2 - 1/2 cos(2 pi n / N)` and
   transformed (zero-padded to the next power of two, `M`).
2. A damped sinusoid `x[n] = Re(c z^n)`, `z = exp((-sigma + i 2 pi f) / fs)`, has the closed-form
   windowed spectrum `(c S(u) + conj(c) S(conj u)) / 2` with
   `S(u) = (1 - Q) [ A(u_nu) / 2 - A(u_nu + i theta) / 4 - A(u_nu - i theta) / 4 ]`,
   `A(v) = 1 / (1 - exp(v))`, `u_nu = u - i 2 pi m / M`, `Q = exp(N u_nu)`, `theta = 2 pi / N`
   (a sum of three geometric series; `1 - exp(v)` is evaluated without cancellation). The model is
   exact, so the fit is limited only by rounding and by whatever is not a sum of damped sinusoids.
3. All partials are fitted jointly over the bins within 8 natural resolutions (`8 / duration`) of each
   one, by Levenberg-Marquardt on frequency, decay and the complex amplitude of every partial, with an
   analytic Jacobian and Householder-QR least squares. Starting values come from a linear solve for
   the amplitudes and a one-partial fit for each decay. Partials 1 Hz apart (ORC-07) are separated
   because the model is joint.
4. With approximate frequencies, each is snapped to the nearest isolated spectral peak (parabolic
   interpolation of the log magnitude). Without them, peaks are picked by successive subtraction: the
   strongest peak is fitted and its closed-form spectrum is removed, so window sidelobes are never
   mistaken for partials; picking stops when the strongest remaining peak is `peak_threshold_db` below
   the first (default 60 dB). Partials that decay fast sit far below the strongest in a Hann window of
   a few seconds; raise the threshold to find them (Case B needs 100 dB).
5. Envelope: for each frame (`envelope_frame_s`, default 0.25 s, non-overlapping), the amplitudes of
   all partials are fitted by linear least squares with the fitted frequencies and decay rates fixed,
   so close partials separate; the value is the amplitude at the frame centre. `envelope_frame_s <= 0`
   disables it. Cost grows with frame length times the square of the partial count.

**Limits.** Peak picking assumes isolated peaks; unison partners closer than about 3 / duration must
be given as approximate frequencies (the P0.3-T1 Case C does). Starting frequencies must lie within
about half a main-lobe width (0.5 / duration) of the truth, or be isolated enough to snap. The decay
starting value is global, so very different decay rates in one signal rely on the one-partial stage.
Noise and unmodelled components bias the fit rather than stopping it.

### Decay, T60 and double decay

`fit_decay`: least-squares line through `ln(amplitude)` against time, giving `sigma`, the amplitude at
t = 0 and `T60 = 3 ln(10) / sigma`. `fit_double_decay`: continuous two-slope fit of `ln(amplitude)`
(slope `-sigma1` before the knee, `-sigma2` after); the knee is found by a 200-point grid search over
`[t[1], t[n-2]]` refined by golden-section search, with a closed-form linear solve at each candidate.

### Inharmonicity

`fit_inharmonicity` fits `f0` and `B` of `f_n = n f0 sqrt(1 + B n^2)` (SPEC-STR) by minimising the
error in cents: a linear start on `(f_n / n)^2 = f0^2 + f0^2 B n^2`, then Gauss-Newton. It returns
`f0_hz`, `b`, the rms and the maximum residual in cents.

### Contact time

`measure_contact`: from the first upward to the last downward crossing of a threshold, each crossing
located by linear interpolation between samples. For a half-sine pulse the error is below 0.15 sample
at a threshold of 1% of peak (the pulse's onset kink costs about 0.1 sample); ORC-08 allows
max(1%, 1 sample).

### Spectral envelope

`spectral_envelope`: windowed (Hann or rectangular), zero-padded power spectrum, averaged over a
band of `smoothing_octaves` (default 1/3 octave) around each point of a fixed log grid
(`f_min * 2^(i / points_per_octave)`, default 20 Hz to 20 kHz, 12 per octave), in dB
(`10 log10` of the mean power). `coherent` normalisation divides by the window sum (a sinusoid of
amplitude A peaks at A / 2 before band averaging); `raw` leaves `|X|` unscaled (use it with the
rectangular window for impulse responses).

### Energy accounting

`account_energy` takes the per-step float64 energy series `H` and returns `max_relative_drift =
max|H[n] - H[0]| / |H[0]|`, `max_relative_step_change = max|H[n+1] - H[n]| / |H[n]|`,
`max_relative_increase = max (H[n+1] - H[n]) / |H[n]|` (never negative) and `non_increasing`, true
when no step increases by more than the given relative tolerance (ORC-15 uses 1e-9 per step in
float64). A zero denominator falls back to the absolute value.

## Golden features (P0.3-R4)

A golden-feature file is JSON described by `tools/analysis/golden-features.schema.json` (JSON Schema
draft 2020-12), `schema_version` 1. Fields, all SI:

- `scenario`, `sample_rate_hz`, `producer` (`tool`, `version`), optional informational `units`.
- `partials`: `index` (partial number, the matching key), `frequency_hz`, `amplitude`, `sigma_per_s`,
  `t60_s`, optional `second_decay` (`sigma_per_s`, `knee_s`).
- Optional `spectral_envelope` (`frequencies_hz`, `magnitude_db`), `contact_time_s`, `energy`
  (`max_relative_drift`, `max_relative_step_change`).
- `tolerances`: `default` per feature kind and optional `partials` overrides keyed by index.

**Default tolerances** (a golden file may override any of them):

| Feature | Unit | Default | Source |
| --- | --- | --- | --- |
| Frequency | cent | 0.01 | SPEC-ORC ORC-01, ORC-16 |
| Decay rate (`sigma`, also `second_decay`) | relative | 0.005 | SPEC-ORC ORC-16 |
| Spectral envelope | dB, largest difference | 1 | SPEC-ORC ORC-17 |
| Contact time | relative | 0.01 | SPEC-ORC ORC-08 |
| Amplitude | dB | 0.1 | planner's choice (no SPEC-ORC row sets it) |
| Energy values | absolute | 1e-9 | ORC-15's per-step float64 figure |
| Knee time `second_decay.knee_s` | relative | 0.05 (`knee_relative`) | planner's choice; the knee of a two-slope fit is poorly conditioned |

`t60_s` is stored for readers but not compared (T60 follows from `sigma`). `knee_s` is compared when
both files have a second decay for the partial, as `partial[n].knee_s` with `knee_relative`; the
tolerance can be overridden per file (`tolerances.default.knee_relative`) and per partial
(`tolerances.partials.<n>.knee_relative`) like the others. The field is optional with a default, so
`schema_version` stays 1.

**Oracle tests never rely on these defaults.** The golden defaults apply to golden renders. They are
looser than some oracles (`energy_absolute = 1e-9` against ORC-02's 10^-12 relative in float64 and
10^-5 in float32, and ORC-09's 10^-10 relative), so an oracle test passes the SPEC-ORC tolerance
explicitly to `account_energy` or to the comparator.

**Comparison.** `compare_golden(reference, candidate)` uses the reference's tolerances. Every
feature in the reference must be present in the candidate and within tolerance; partials are matched
by index; partials only in the candidate are reported in `notes`, not failed; spectral grids must be
identical (relative 1e-9). The result lists each feature with its measured difference, tolerance,
unit and verdict, for example `partial[2].decay_rate`. The CLI `pme_golden_compare [-v]
<reference.json> <candidate.json>` prints failing features (all with `-v`) and exits 0 (all within
tolerance), 1 (outside tolerance) or 2 (usage or file error). The Python comparator
(`pme_analysis.compare_golden`) gives the same verdicts and feature names.

## WAV (P0.3-R5)

`read_wav`/`write_wav` (and `parse_wav`/`encode_wav` for memory buffers) handle RIFF/WAVE PCM 16-,
24- and 32-bit integer and IEEE float 32- and 64-bit, any channel count, `WAVE_FORMAT_EXTENSIBLE`
on read. Samples are float64, interleaved; integer formats scale by `2^(bits-1)` so the range is
[-1, 1) and out-of-range values clamp on write. Little-endian on every system. Unknown chunks are
skipped; RF64/BW64, compressed formats, truncated `fmt`/`data` chunks and inconsistent block
alignment raise `WavError` with a message naming the problem. The writer emits the plain 16-byte
`fmt ` chunk (format tag 1 or 3), even for more than two channels. WAV code is never linked into the
engine (see [testing.md](testing.md), "Isolation from the engine"); generate test signals in code
because audio files route to Git LFS.

**Cross-language check.** The tool `pme_wav_roundtrip` (`tools/analysis/wav_roundtrip_main.cpp`;
`write <file> <format> <channels>` and `read <file>`) and
`tools/tests/test_p0_3_r5_cross_language.py` prove that a WAV written by the C++ library reads
identically in Python and the reverse, for 16-, 24- and 32-bit integer and 32- and 64-bit float with
1 and 3 channels (sample-exact; the signal is built from dyadic rationals k/1024, which every format
represents exactly; the two writers also emit identical bytes). The tool path comes from the
environment variable `PME_WAV_TOOL`; the test is skipped without it, and the `build` job of
`pr.yml` runs it on all three systems and fails the step on a skip.

## Achieved accuracy (P0.3-T1)

Noise-free synthetic signals from `tools/analysis/testdata/p0_3_t1_signals.json` (Cases A, B and C of
the brief), Linux x86-64, GCC 13 and Clang 18, Debug and Release; C++ and Python agree to the digits
shown. Worst error over all partials of each case:

| Case | Frequency (cent) | Decay rate (%) |
| --- | --- | --- |
| A 27.5 Hz | 3.8e-13 | 2.8e-12 |
| A 261.6 Hz | 0 | 4.1e-11 |
| A 4186 Hz | 0 | 5.2e-10 |
| B 44.1 kHz, 30 partials | 3.8e-13 | 2.0e-10 |
| B 48 kHz | 7.7e-13 | 1.2e-10 |
| B 96 kHz | 3.8e-13 | 7.4e-11 |
| C unison 261.6 and 262.6 Hz | 3.8e-13 | 2.6e-11 |
| A 27.5 Hz rounded to float32 | 9.8e-10 | 1.9e-8 |
| A 261.6 Hz rounded to float32 | 6.7e-11 | 4.4e-8 |
| A 4186 Hz rounded to float32 | 0 | 1.3e-8 |

The pass criteria are 0.01 cent and 0.5%. Case B's `fit_inharmonicity` recovers `B` to 1.4e-16
relative and `f0` exactly; both Case B and the peak-picking variants find all 30 partials.
