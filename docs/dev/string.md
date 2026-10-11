# Stiff string compiler (A2)

SPEC sections implemented: SPEC-STR "Modes", "Wound strings", "Tension for pitch", "Losses",
"Forces and observations", "Polarisations", "Duplex and rear segments", "Truncation" and
"Contract" (with SI-0001, SI-0004 and, provisionally, SI-0010); SPEC-OSC "Bandwidth" through A1's
`passesBandwidthRule`; SPEC-CONV (float64 at compile time, float32 for `phiBar` and the float32
fields of `OscCoeffs`). The public entry point is `pme::compileString`
(`engine/include/pme/string.hpp`, frozen at interfaces-v2). Everything else is internal, under
`engine/src/string/include/pme/string/string_model.hpp` (namespace `pme::string`), reachable from
tests and the later compilers through the `pme_string` target (which links `pme_osc`).
`compileString` and all helpers run on non-audio threads and allocate where they return vectors.

## Internal API (for C1, A4, A5, A6)

| Function | Meaning | Requirement |
| --- | --- | --- |
| `validate(params)` | `InvalidArgument` for non-physical input (list below), else `Ok` | all |
| `linearDensity(params)`, `bendingStiffness(params)` | ρA, EI | A2-R1 |
| `tuningF1(curve, key, unisonDetuneCents)` | f₁ for a group with key ≥ 0 (C1 calls it) | A2-R2 |
| `tensionForPitch(params, f1)`, `f1ForTension(params, T)` | T from f₁ and its inverse | A2-R2, A2-R7 |
| `modeCount(params, fs, fMax)` | N after truncation (0 when invalid or mode 1 fails) | A2-R5 |
| `windowFactor(kind, k, b)` | W(k) for Point, Rectangular, RaisedCosine | A2-R6 |
| `windowedShapes(modes, x0, kind, b, out)` | φ̄\_n = φ\_n(x₀) W(k\_n), float64 then float32 | A2-R6 |
| `applyMute(modes, muteRatio, sigmaFelt, detuneHCents)` | rear-segment mute | A2-R7 |

`compileString` itself always uses `params.f1Target` as given; compileInstrument replaces it by
`tuningF1(...)` for groups with key ≥ 0 before the call (SI-0004).

**Status (ADR-0006).** `InvalidArgument`: fs ≤ 0 or not finite; fMax NaN; L, dCore, rhoCore, ECore,
f1Target not finite and > 0; dOuter < dCore or not finite; dOuter > dCore with rhoWind ≤ 0, packing
outside (0, 1] or not finite; epsWind, b₁, b₂, b₃ < 0 or not finite; detuneH not finite; a
`sigmaOverride` entry < 0 or not finite; a registered point outside [0, L] (or NaN); the
closed-form tension T ≤ 0. `Rejected`: mode 1 fails the bandwidth rule (either polarisation). `out`
is unchanged unless the result is `Ok`.

## Wound strings and tension (A2-R1, A2-R2)

ρA = (π/4)[ρ\_c d\_c² + η\_w ρ\_w (d\_o² − d\_c²)], EI = (1 + ε\_w) π E\_c d\_c⁴/64, T = 4L²ρA f₁² −
π²EI/L². A plain string has d\_o = d\_c; the winding term is then exactly 0 (the code does not
evaluate it), whatever ρ\_w and η\_w hold. `StringModes` stores T, ρA, EI and B = π²EI/(T L²).
`tuningF1` uses stretch\[key\] = 0 for a key beyond the vector and for key < 0.

## Modes, boundary and losses (A2-R3, A2-R4)

**Evaluation form of ω\_n.** ω\_n = √((T k\_n² + EI k\_n⁴)/ρA) with k\_n = nπ/L. Both terms are
positive, so there is no cancellation; the n²ω₀²(1 + Bn²) form needs the extra products ω₀ and B and
gives the same values (the test compares the two to 10⁻¹³; ORC-03 checks 10⁻⁹).

**Clamped ends.** `Boundary::Clamped` multiplies ω\_n by Fletcher's factor 1 + 2√B/π + 4B/π², with
B the description's inharmonicity. The same factor is applied to every n. Shapes (`k`, `phiBar`)
stay pinned; T and B are not changed.

**Losses.** σ\_n = b₁ + b₂√ω\_n + b₃ω\_n², with ω\_n the undamped, possibly Fletcher-corrected
frequency. If `sigmaOverride` is non-empty, σ\_n = `sigmaOverride[n−1]` for n ≤ its size (value
used exactly, including 0) and the formula for higher n. An override longer than N is simply not
used beyond N. The override participates in truncation like any other σ\_n.

## Truncation and polarisations (A2-R5)

N is the largest n such that every mode 1…n passes `passesBandwidthRule(ω, σ, fs, fMax)` for the
vertical polarisation (ω\_n) **and** the horizontal polarisation (ω\_h,n = ω\_n 2^(Δ\_h/1200), same
σ\_n), where Δ\_h = `detuneH`. One shared table (`k`, `omega`, `sigma`) is kept for both;
`oscV` and `oscH` hold their coefficients from `compileOscillator`. The loop stops at the first
failing mode and is capped at `kMaxModes` = 65 536. `fMax` follows A1's convention: a mode is
above f\_max when ω\_d/2π > f\_max.

Mode counts N (A2-T2; strings: treble plain d = 1 mm, L = 0.1 m, f₁ = 2 kHz, B ≈ 10⁻²; mid plain
d = 1 mm, L = 0.65 m, f₁ = 262 Hz; bass wound d\_c = 1.6 mm, d\_o = 4.7 mm, L = 1.9 m, f₁ = 27.5 Hz;
b₁ = 0.4, b₂ = 10⁻³, b₃ = 10⁻¹¹ (plain), 0.3, 5 × 10⁻⁴, 5 × 10⁻¹² (bass)):

| fs (Hz) | f\_max | treble | mid | bass |
| --- | --- | --- | --- | --- |
| 44 100 | 16 kHz | 6 | 46 | 220 |
| 44 100 | ∞ (0.95π binds) | 8 | 56 | 255 |
| 48 000 | 16 kHz | 6 | 46 | 220 |
| 48 000 | ∞ | 8 | 59 | 266 |
| 96 000 | 16 kHz | 6 | 46 | 220 |
| 96 000 | ∞ | 13 | 91 | 382 |

Special cases (mid string): f\_max = 5 kHz at 48 kHz binds at N = 18; b₃ = 10⁻⁵ at 96 kHz, f\_max =
∞ overdamps at N = 46; `detuneH` = +5 cents with f\_max 0.1 % above vertical mode 40 (12 902.5 Hz)
gives N = 39 (the horizontal polarisation binds; the vertical alone would keep ≥ 40).

## Shapes and windows (A2-R6)

`phiBar[p][n−1]` = φ\_n(x₀,p) = √(2/L) sin(nπ x₀/L) for the registered points in the order given,
W = 1 (SI-0001). The sine uses an exact argument reduction (`sinPi`: fmod and reflections are exact
in binary floating point), so x₀ = 0 and x₀ = L give exactly 0 for every n and the midpoint gives
exactly 0 for even n, then the result is rounded to float32.

`windowFactor` with u = kb/2: Point 1; Rectangular sin(u)/u; RaisedCosine sin(u)/u · π²/(π² − u²).
sinc uses a series for |x| < 10⁻⁴. The raised cosine uses (π − u)(π + u) for the denominator and,
within 0.5 of π, the identity W = sinc(π − u) · π²/(u(π + u)), so W(π) = 1/2 without a 0/0.
Measured against 60-digit mpmath values over 26 abscissae in \[10⁻¹², 19.9\], including π ± 10⁻⁸ and
π ± 10⁻⁵ and the series switch at 10⁻⁴: worst relative error 1.6 × 10⁻¹⁶ (rectangular, away
from its zero at π) and 4.4 × 10⁻¹⁶ (raised cosine; 2.2 × 10⁻¹⁶ near 0, 4.4 × 10⁻¹⁶ near π).
`windowedShapes` writes φ̄\_n for n ≤ min(out.size(), N) from L, k\_n and x₀ (float64), zeros the
rest of `out`.

## Rear and duplex segments (A2-R7, SI-0010)

A segment with `bridgeEnd = At0` goes through the same `compileString` call with its own L (= L\_d),
`f1Target` and positions; `BridgeEnd` only matters to `compileBridgeTerms` (A4).

SI-0010 status (open; orchestrator defaults applied): (1) the tuning rule applies to speaking
(`AtL`) segments only; a rear segment takes the tension of the preceding speaking segment, and the
caller sets `f1Target = f1ForTension(params, T)` first; the test reproduces T to 10⁻¹². (2)
`applyMute(modes, m_d, sigmaFelt, detuneHCents)` adds σ\_mute = m\_d σ\_felt to every σ\_n and
recompiles `oscV`/`oscH`; σ\_felt is an argument with no default (provisional value 20 s⁻¹ at the
caller). It takes a fourth argument, `detuneHCents` (the string's `detuneH`), because `StringModes`
does not store it and the horizontal coefficients need ω\_h,n; this extends the signature the brief
names. `InvalidArgument` for m\_d outside \[0, 1\], σ\_felt < 0 or not finite or a non-finite detune.
With σ\_mute = 0 the tables are untouched (bit-identical). Because σ is added to every mode while ω
grows with n, a mute can only overdamp low modes first, so truncation happens only through mode 1:
the result is then `Rejected` and the tables are unchanged (the code also keeps the longest passing
prefix, which cannot be shorter than that in practice).

## Tests

| Test | File |
| --- | --- |
| A2-T1, A2-R1 (hand-worked ρA and EI, plain strings, invalid input, `Rejected`) | `engine/tests/string/test_wound.cpp` |
| A2-R2 (tuning, tension and inverse) | `test_tuning.cpp` |
| A2-T2, A2-R3, A2-R4, A2-R5 | `test_modes.cpp` |
| A2-R6 | `test_shapes.cpp` (reference values from mpmath 1.3.0 embedded) |
| A2-R7 | `test_mute.cpp` |
| Benchmark `pme_string_bench` (bass and treble at 48 kHz, 4 registered points; label `perf`) | `string_bench.cpp` |

ORC-03 to ORC-05 and A2-T3 belong to the A2 test-author. Benchmark (this machine, one string
per call, 4 points): bass 33 µs, treble 1.4 µs in the release preset (Debug 100 µs and 7 µs).
There is no baseline file until V2 and decision D-4.
