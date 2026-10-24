# SI-0009 ORC-02's float32 energy tolerance (10⁻⁵) is below float32 round-off

Status: resolved · Raised by: A1, test-author · Date: 2026-10-11
SPEC section: SPEC-ORC "Verification oracles", row "Oscillator energy" (ORC-02); SPEC-OSC "Discrete energy"

## Failing test
ORC-02, float32 case (A1 test-author brief): lossless mode (σ = 0, f = 0), 480 000 steps (10 s at 48 kHz), float32 coefficients and state, H^(n+½) accumulated in float64 with float64 ω̂². Pass was max over n of |H^(n+½) − H^(½)| / H^(½) ≤ 10⁻⁵.

The test-author ran this on a scratch `compileOscillator` written from SPEC-OSC (SI-0008 form), independent of the A1 implementation, with q⁰ = 1 and u⁰ = 0.5 √κ:

| Frequency | float32 drift | float64 drift |
| --- | --- | --- |
| 27.5 Hz | 2.365 × 10⁻⁵ | ≤ 2 × 10⁻¹³ |
| 4186 Hz | 5.658 × 10⁻⁵ | ≤ 2 × 10⁻¹³ |
| 21 600 Hz (ω\_dΔt = 0.9π) | 4.977 × 10⁻⁶ | ≤ 2 × 10⁻¹³ |

q⁰ = 0.7 and 0.9 gave 4 × 10⁻⁶ to 7.8 × 10⁻⁵. Replacing float64 ω̂² by float32 κ/Δt² in H gave the same drift, so the drift is round-off in the state update, not a mismatch between κ and ω̂². The float64 case passes 10⁻¹².

## Evidence
The planner reproduced all three float32 values to four digits and extended the experiment. Script: [docs/plan/verification/osc_energy_f32.py](../plan/verification/osc_energy_f32.py) (NumPy float32 arrays, every operation correctly rounded; `fma=True` emulates a fused u − κq). Notation: u = 2⁻²⁴ (float32 unit round-off), N = number of steps, u√N = 4.13 × 10⁻⁵ at N = 480 000.

**1. 10⁻⁵ is below the round-off floor.** Each step rounds qⁿ⁺¹ and uⁿ⁺¹ to float32, a relative error of about u in each, so H changes by about u per step relative. With uncorrelated errors H performs a random walk of size about u√N. Over 400 oscillators (frequencies log-uniform 20 Hz to 21.6 kHz, amplitudes 0.3 to 1):

| N | median of max drift | median / (u√N) | rms of final drift / (u√N) |
| --- | --- | --- | --- |
| 10³ | 1.6 × 10⁻⁶ | 0.86 | 0.92 |
| 10⁴ | 5.2 × 10⁻⁶ | 0.87 | 0.88 |
| 10⁵ | 1.7 × 10⁻⁵ | 0.88 | 0.81 |
| 4.8 × 10⁵ | 3.3 × 10⁻⁵ | 0.81 | 0.79 |
| 4.8 × 10⁶ | 5.4 × 10⁻⁵ | 0.41 | 0.47 |

The drift grows as √N (random walk), not as N, and then saturates: at a single frequency it stops growing (27.5 Hz: 2.4 × 10⁻⁵ from 3 × 10⁵ to 4.8 × 10⁶ steps; 21.6 kHz: 5.0 × 10⁻⁶ from 10⁴ steps on). It is bounded and non-secular. The median run over 10 s already exceeds 10⁻⁵ by 3.3×, so no float32 implementation of SPEC-OSC can pass the current row. A shorter run does not help: at N = 10⁴ the worst of 400 runs is 3.8 × 10⁻⁵.

**2. The energy is ill-conditioned near Nyquist.** For the lossless discrete solution qⁿ = A cos(n ω\_dΔt), H Δt² = ½A² sin²(ω\_dΔt), while its two terms are each about ½A² ω̂²Δt². Round-off in the terms is therefore amplified by ω̂²Δt²/sin²(ω\_dΔt) = 1/(1 − ω̂²Δt²/4) = 1/cos²(ω\_dΔt/2): 1.0 at 27.5 Hz, 1.08 at 4186 Hz, 41 at ω\_dΔt = 0.9π. Normalised by u√N/(1 − ω̂²Δt²/4), the drift over 2 982 runs at 1 491 random frequencies (both rounding modes) has median 0.7, 99th percentile 2.1, maximum 3.4 (frequencies within 1% of fs/6, fs/4 and fs/3 excluded, see item 4).

**3. The distribution has a heavy tail at low frequency.** Random initial phases and amplitudes (0.2 to 1), N = 480 000, with and without FMA, drift in units of u√N (the conditioning factor is 1.00 here except 1.08 at 4186 Hz):

| Frequency | median | 99th percentile | 99.9th percentile | max (runs) |
| --- | --- | --- | --- | --- |
| 20 Hz | 1.6 | 10.6 | 15.9 | 25.9 (2 400) |
| 27.5 Hz | 1.1 | 3.9 | 6.0 | 15.2 (10 400) |
| 41.2 Hz | 0.8 | 2.1 | 3.8 | 5.3 (2 400) |
| 110 Hz | 0.4 | 1.5 | 2.2 | 3.0 (2 400) |
| 4186 Hz | 0.9 | 2.3 | 2.9 | 3.2 (6 000) |

The engine's float32 kernel is compiled with and without FMA contraction on different compilers and CPUs, so the tolerance must cover this whole distribution, not the one trajectory a given test takes. Below about 25 Hz the tail exceeds the tolerance proposed here (20 Hz: 25.9), which is why the float32 row names its frequencies.

**4. Exact resonances drift secularly.** At fs/6, fs/4 and fs/3, κ = 1, 2 and 3 is exact in float32 and the exact map has period 6, 4 and 3, so the round-off pattern repeats and accumulates coherently. At fs/3 with q⁰ = 0.7, u⁰ = 0.5 √3 the drift is linear, about 3.8 × 10⁻⁸ per step (1.8 × 10⁻² after 480 000 steps, 0.17 after 4.8 × 10⁶). Detuned by 10⁻⁷ to 10⁻⁶ relative the drift is still 2 × 10⁻³ to 2 × 10⁻²; detuned by 10⁻⁴ it reaches 9.8 in the units of item 2; from 0.3% detuning on it stays inside the generic band (at most 3.9). Engine impact: none in practice. Damping bounds the error at about (3.8 × 10⁻⁸)/(2σΔt); a 16 kHz mode with T60 = 2 s gives 2.6 × 10⁻⁴ relative energy (0.001 dB). The ORC-02 test frequencies must keep away from these three points.

**5. A tolerance at 24 u√N / (1 − ω̂²Δt²/4) still catches defects.** Same script, lossless, 480 000 steps, q⁰ = 1, drift divided by the tolerance (9.9 × 10⁻⁴, 9.9 × 10⁻⁴, 1.07 × 10⁻³, 4.05 × 10⁻² at 27.5 Hz, 261.6 Hz, 4186 Hz, 0.9π):

| Kernel | 27.5 Hz | 261.6 Hz | 4186 Hz | 0.9π |
| --- | --- | --- | --- | --- |
| Correct, worst of 50 random initial conditions, no FMA / FMA | 0.38 / 0.21 | 0.07 / 0.08 | 0.08 / 0.09 | 0.001 / 0.01 |
| Correct, the test-author's cases (q⁰ = 1, u⁰ = 0.5 √κ) | 0.024 | 0.049 | 0.053 | 0.0001 |
| β one float32 ulp below 1 (spurious loss) | 36 | 44 | 42 | 0.03 |
| β one float32 ulp above 1 (spurious gain) | 63 | 58 | 49 | 1.5 |
| q advanced with uⁿ instead of uⁿ⁺¹ (not energy conserving) | 5 × 10⁵ | overflow | overflow | overflow |
| κ = ω²Δt² instead of ω̂²Δt² | 0.03 | 0.14 | 13 | unstable (10⁷⁴) |
| κ 10⁻³ relative too large | 1.0 | 1.0 | 0.5 | 1.0 |

A one-ulp error in β, the smallest non-conservative change a float32 kernel can make, fails by 36× to 63× at the three lower frequencies; a non-conservative update fails by 10⁵ or overflows. The float32 energy check does not detect small coefficient errors (κ off by 10⁻³ sits at the threshold); it never could, since a wrong κ is conserved in its own energy. Coefficient errors are covered elsewhere: ORC-01 bounds frequency to 0.01 cent (κ to about 1.2 × 10⁻⁵ relative at low frequency), A1-T1 bounds the float64 coefficients to 10⁻¹⁴, and the float64 ORC-02 case bounds κ against ω̂² to 10⁻¹². At ω\_dΔt = 0.9π the ill-conditioning of H leaves the float32 case able to detect only spurious gain and instability; the float64 case at the same frequency is unaffected.

## Proposed correction
In SPEC-ORC, row "Oscillator energy", replace

| Oscillator energy | Lossless mode, 10 s | H^(n+½) constant | 10⁻¹² relative in float64; 10⁻⁵ in float32 |

with

| Oscillator energy | Lossless mode, 10 s; float32 at 48 kHz (N = 480 000) for 27.5 Hz, 4186 Hz and ω\_dΔt = 0.9π, none within 1% of fs/6, fs/4 or fs/3 | H^(n+½) constant (float32: bounded round-off drift) | 10⁻¹² relative in float64; in float32 24 u√N / (1 − ω̂²Δt²/4), u = 2⁻²⁴: 9.9 × 10⁻⁴ at 27.5 Hz, 1.07 × 10⁻³ at 4186 Hz, 4.05 × 10⁻² at 0.9π (SI-0009) |

The constant 24 is 1.6× the worst normalised drift seen at 27.5 Hz in 10 400 runs (15.2), 4× its 99.9th percentile and 6× its 99th; the defect margins are in Evidence item 5. The relative drift is measured exactly as before: max over n of |H^(n+½) − H^(½)| / H^(½), H in float64 with float64 ω̂².

## Resolution
Resolved on 2026-10-11: accepted as proposed. Orchestrator default under the owner's proceed instruction, flagged for owner review. This is a test-tolerance change; the scheme, the coefficients, the energy definition, the float64 tolerance and every other oracle are unchanged.

- SPEC commit: "PLAN: SI-0009 ORC-02 float32 energy tolerance" (SPEC-ORC row replaced as proposed).
- docs/agents/briefs/A1-test-author.md: ORC-02 row and float32 pass criterion updated to match.
- Follow-up for the orchestrator to route to an implementer: the SPEC columns of row ORC-02 in engine/tests/oracles.md still show 10⁻⁵; they must be copied from SPEC (not planning documentation, so not changed here).
- If the owner prefers a tighter float32 check, the alternative is a secular-trend criterion (least-squares slope of the relative drift over the run times its length, bounded by a few u√N), which separates a one-ulp β error from round-off by a wider margin but needs a metric P0.3's `account_energy` does not provide.

Affected phases: A1 (ORC-02). Later energy oracles (ORC-09, ORC-15) run in float64 and are unaffected.
