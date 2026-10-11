# Modal oscillator (A1)

SPEC sections implemented: SPEC-OSC (all of "The modal oscillator": discrete scheme, difference
form, derived quantities, discrete energy, bandwidth, time-varying parameters, contract) and
SPEC-CONV (float64 at compile time, float32 difference-form states at run time, sub-block
S = 16 or 32). The public entry point is `pme::compileOscillator` (`engine/include/pme/osc.hpp`,
frozen at interfaces-v2); everything else is internal to the engine and lives under
`engine/src/osc/include/pme/osc/` (namespace `pme::osc`), reachable from tests and the later
compilers through the `pme_osc` target.

| File | Content | Requirement |
| --- | --- | --- |
| `oscillator.hpp`, `oscillator.cpp` | `compileOscillator`, `compileOscillatorF64`, `passesBandwidthRule`, `dampedFrequencyTimesDt` | A1-R1, A1-R2 |
| `mode_bank.hpp`, `mode_bank.cpp` | `ModeBank`, `stepModes` kernel, derived quantities, energy | A1-R3, A1-R4, A1-R5 |
| `ramp.hpp`, `ramp.cpp` | `exceedsRampThreshold`, `planRamp`, `coefficientsAt`, `applyRampsAt` | A1-R6 |

## Coefficient compilation (A1-R1)

With Δt = 1/fs, x = σΔt, ω\_dΔt = Δt √((ω − σ)(ω + σ)) (the product form avoids cancellation near
σ ≈ ω), all in float64:

- σ̂Δt = tanh(x)
- ω̂²Δt² = 4 [sinh²(x/2) + sin²(ω\_dΔt/2)] / cosh(x)
- β = (1 − σ̂Δt)/(1 + σ̂Δt), κ = ω̂²Δt²/(1 + σ̂Δt), γ = Δt²/(1 + σ̂Δt)
- ω̂² = ω̂²Δt² · fs², σ̂ = σ̂Δt · fs (kept float64); β, κ, γ are then rounded to nearest float32.

**SI-0008 evaluation.** The formula printed in SPEC-OSC before SI-0008 was resolved,
2[−expm1(−σΔt) + 2e^(−σΔt) sin²(ω\_dΔt/2)] − 2 σ̂Δt e^(−σΔt) cos(ω\_dΔt), is algebraically equal but
its two largest terms cancel when σΔt is small. The implementation uses the form now in SPEC-OSC
(`// SPEC-OSC discrete scheme; evaluation per SI-0008`). Test `A1-R1 SPEC printed formula agrees
with the SI-0008 form` evaluates the old expression and shows the two agree within 5 × 10⁻¹² over
the A1-T1 grid (measured worst difference 1.9 × 10⁻¹², which is the old form's rounding error).

`compileOscillatorF64` returns the float64 values (σ̂Δt, ω̂²Δt², β, κ, γ, ω̂², σ̂) before rounding;
the A1-T1 test checks them at 10⁻¹⁴. Status: `InvalidArgument` for fs ≤ 0 or a non-finite or
negative ω, σ or fs; `Rejected` when the bandwidth rule rejects (σ ≥ ω, ω\_dΔt > 0.95π); `Ok`
otherwise. On any status other than `Ok` the out-parameter is unchanged (ADR-0006).

## Bandwidth rule (A1-R2)

`passesBandwidthRule(omega, sigma, fs, fMax)` returns `Accepted` or the reason for rejection,
checked in this order: `InvalidArgument`, `Overdamped` (σ ≥ ω), `AboveNyquistMargin`
(ω\_dΔt > 0.95π), `AboveFMax`. **f\_max interpretation:** "its frequency" in SPEC-OSC is the damped
frequency, so a mode is above f\_max when ω\_d/(2π) > f\_max (strictly greater; a mode exactly at
f\_max is kept). `compileOscillator` gives the same verdict as the function with f\_max = +∞.
`compileString` (A2) and later compilers call it with the instrument's f\_max.

Highest accepted frequency (σ = 0, last double below the 0.95π limit):

| fs | Highest accepted frequency |
| --- | --- |
| 44 100 Hz | 20 947.5 Hz (0.475 fs) |
| 48 000 Hz | 22 800 Hz |
| 96 000 Hz | 45 600 Hz |

## ModeBank and the scalar kernel (A1-R3)

`ModeBank` stores N modes in structure-of-arrays form: q, u, β, κ, γ as float32 and ω̂², σ̂ as
float64. Every array is 64-byte aligned and padded to a multiple of 16 elements
(`paddedSize()`); padding lanes have β = κ = γ = 0 and zero state and stay zero. `resize(n)`
allocates (non-real-time, zero-fills); `step(force, qPrev, uPrev)` is `noexcept` and allocation-free.

`stepModes<S, C>(q, u, beta, kappa, gamma, force, n, qPrevOut, uPrevOut)` is the kernel written
once as a template on the state type `S` (float in the engine, double in tests) and coefficient
type `C`. For each mode, in this exact order (no algebraic rearrangement):

    u_next = beta * u - kappa * q + gamma * f;    // SPEC-OSC difference form
    q      = q + u_next;

Only q and u are stored. `qPrevOut` / `uPrevOut` (nullable) receive qⁿ / uⁿ, which the derived
quantities and energy need. **A8** vectorises this kernel and must match it within 10⁻⁶ relative
(Numerics rule 4); the force is a per-mode span (per unit modal mass). **A2 and A3** build their
bank with `setMode(i, OscCoeffs)` and drive it with `step`.

## Derived quantities and energy (A1-R4, A1-R5)

Float64 functions of float32 states: `halfStepVelocity` = uⁿ⁺¹/Δt, `centredVelocity` =
(uⁿ⁺¹ + uⁿ)/(2Δt), `centredAcceleration` = (uⁿ⁺¹ − uⁿ)/Δt². `modeEnergy` is
H^(n+½) = ½(uⁿ⁺¹/Δt)² + ½ ω̂² qⁿ qⁿ⁺¹ per unit modal mass (callers multiply by ρA or the modal
mass); `energyBalance` is Δt[−2σ̂ (δ\_t· qⁿ)² + fⁿ δ\_t· qⁿ]. `bankEnergy` and `bankEnergyBalance`
sum over the modes in index order in float64.

## Coefficient ramps (A1-R6)

Rule (SPEC-OSC "Time-varying parameters"), on the physical parameters of the mode in force: a
change is ramped when |Δω| > 10⁻³ ω or |Δσ| > 10⁻³ σ. Any change of σ from or to 0 ramps. Smaller
changes switch to the target coefficients from the first sample of the sub-block.

`planRamp` records the coefficients in force, the target and the sub-block length S (16 or 32)
and decides `ramped`. `coefficientsAt(plan, s, β, κ, γ)` gives the coefficients for the update of
sample s = 0 … S − 1 of the sub-block: linear, c(s) = c₀ + (c₁ − c₀)(s + 1)/S, and exactly the
target at s = S − 1 and afterwards. `applyRampsAt` writes the coefficients of a span of plans into
a `ModeBank` for one sample; the state (q, u) is never touched. Everything is `noexcept` and
allocation-free (C4 applies bends and damper changes on the audio thread).

## Tests

| Test | File |
| --- | --- |
| A1-T1 reference table (`a1_t1_reference.json`, 64 × 41 grid, 2286 accepted pairs; generated by `make_a1_t1_reference.py` with mpmath 1.3.0, not run in CI) | `engine/tests/osc/test_coefficients.cpp` |
| A1-T2 bandwidth rejections | `engine/tests/osc/test_coefficients.cpp` |
| A1-T3 chirp phase | `engine/tests/osc/test_chirp.cpp` |
| A1-R3 to A1-R6 | `test_mode_bank.cpp`, `test_ramp.cpp`, `test_rt.cpp` (label `rt`) |
| Benchmark `pme_osc_bench` (64, 512, 4096 modes; "mode-samples"; label `perf`) | `engine/tests/osc/osc_bench.cpp` |

The A1-T1 grid tops out at 0.95π(1 − 10⁻⁹) so that the accept/reject verdict is never decided by
double rounding at the limit; the limit itself is tested by A1-T2.

**A1-T3 results** (fs = 48 kHz, σ = 0.5 s⁻¹, free oscillation, 1% ramp over one sub-block): relative
phase error accumulated over the ramp, with the ramp / with a step change at the sub-block start:

| f, S | with ramp | step change |
| --- | --- | --- |
| 1 kHz, 16 | 3.2 × 10⁻⁴ | 5.6 × 10⁻³ |
| 1 kHz, 32 | 1.4 × 10⁻⁴ | 5.3 × 10⁻³ |
| 4 kHz, 16 | 5.6 × 10⁻⁴ | 5.3 × 10⁻³ |
| 4 kHz, 32 | 3.1 × 10⁻⁴ | 5.1 × 10⁻³ |

The ramp meets the 0.1% criterion; the step change does not (about five times over), which is why
the ramp rule exists. Benchmark numbers depend on the machine; there is no baseline file until V2 and
decision D-4 define the reference machines.
