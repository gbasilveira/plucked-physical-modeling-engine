# SI-0021 ORC-06 "every kept mode within 0.1%" is not met by the SPEC-BRG partitioned scheme above ωΔt ≈ 0.1

Status: resolved (oracle criterion 2026-10-12; scheme: owner decision 2026-10-12, both schemes selectable; numerics of the implicit scheme pending owner review) · Raised by: A4, test-author · Date: 2026-10-12
SPEC section: SPEC-ORC row "Bridge damping" (ORC-06); SPEC-BRG introduction ("reproduces wave theory's bridge damping to 1 part in 10⁴"), "Bridge force", "Discrete coupling order"

## Failing test
ORC-06: flexible string on a pure conductance G, every kept mode, σ = −f₀ ln[(1 − Z₀G)/(1 + Z₀G)], tolerance 0.1% (run 10⁻⁴). A4's implementation of SPEC-BRG as written (explicit order, a\_n = ρA ω̂ₙ² g\_n with the discrete ω̂ₙ²), 48 kHz, Z₀G = 0.01, relative decay-rate error σ/σ\_SPEC − 1:

| Mode | 1 | 3 | 6 | 8 | 16 | 32 | 64 | 128 | 200 (of 207) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Error | 5.5 × 10⁻⁶ | −1.3 × 10⁻⁴ | −5.8 × 10⁻⁴ | −1.05 × 10⁻³ | −4.3 × 10⁻³ | −1.7 × 10⁻² | −6.7 × 10⁻² | −0.25 | −0.50 |

Z₀G = 0.05 is similar (worst −0.96). The error is within 10⁻³ only for ωΔt ≲ 0.1 (below about 760 Hz at 48 kHz). ORC-07 and the ωΔt ≤ 0.1 ORC-06 cases pass; the "every kept mode" cases are held from merge.

## Evidence
- Two independent computations by the test-author agree to 4 × 10⁻⁶ absolute: the exact eigenvalues of the one-step coupled linear map built from the SPEC formulas, and decay fits of the float32 time-domain run. The error is a property of the discrete scheme, not of A4's code.
- It scales as (ωΔt)² (×4 per octave from mode 8 to 128), the signature of the second-order time discretisation of the explicit partitioned coupling, not of round-off.
- Replacing ω̂ₙ² by the continuous ωₙ² in a\_n (which SPEC-BRG forbids, because it breaks the discrete energy balance) only reduces the error (3.6 × 10⁻⁴ at mode 30) and still fails above mode 46 (Z₀G = 0.01) or 29 (Z₀G = 0.05).
- SPEC-BRG's claim "reproduces wave theory's bridge damping to 1 part in 10⁴" was evidently checked on low modes only.

## Proposed correction
1. **Oracle (resolved):** SPEC-ORC row "Bridge damping", tolerance cell:
   "Modes with ω\_nΔt ≤ 0.1: 0.1% against the formula (run: 10⁻⁴). Every kept mode: the measured decay rate equals the decay rate of the exact eigenvalues of the scheme's one-step coupled map within 10⁻⁴ relative, and the deviation from the formula is reported per mode, not bounded (SI-0021)."
   This keeps an exact check on every mode (the implementation matches the SPEC scheme) and the physical check where the scheme is accurate.
2. **SPEC-BRG text:** replace "reproduces wave theory's bridge damping to 1 part in 10⁴" by "reproduces wave theory's bridge damping to 1 part in 10⁴ for modes with ωΔt ≲ 0.05 and within 0.1% for ωΔt ≤ 0.1; above that the explicit order under-estimates the bridge damping, by a relative error growing as (ωΔt)² (about 1.7% at ωΔt = 0.4 and up to 50% near the bandwidth limit at Z₀G = 0.01; SI-0021)."
3. **Scheme question (open, owner):** high string partials (above roughly 1 kHz at 48 kHz) lose too little energy to the bridge — up to half the bridge damping near the bandwidth limit. Bridge damping is a minor part of the total decay of high partials in a piano (internal losses b₃ω² dominate there), so the audible effect is probably small, but it is a systematic numerical error. Options: (A) accept and document (proposed default; V4 listening tests and E2 fitting absorb it, since fitted losses compensate); (B) make the bridge step implicit (one scalar solve per bridge point and axis per sample), which is also Option C of SI-0018 and would address both issues; (C) a per-mode correction of the bridge coupling coefficient fitted to the exact one-step map (keeps the explicit order; needs a proof that the discrete energy balance survives). Changing the scheme is a numerics decision for the owner; measure (B)'s cost in A8/V2 before deciding.

## Resolution
Items 1 and 2 resolved 2026-10-12 by the orchestrator's default, as proposed; SPEC updated in the `PLAN: file SI-0021 …` commit. They change a test criterion and a documentation claim, not engine behaviour; flagged for owner review with item 3. Item 3 stays open for the owner (recommended default: A now, with B evaluated together with SI-0018 before G1). Affected phases: A4 (ORC-06 test-author: add the exact-map comparison for every mode, keep the 0.1% check for ωΔt ≤ 0.1, report per-mode deviations; update the engine/tests/oracles.md row), B5 (fitted intrinsic losses absorb the deficit; B5-T2 uses ORC-06), V4.

## Owner decision on item 3 (2026-10-12)

**Implement both schemes, selectable (decision D-13).** Explicit stays the Realtime default (fastest). An implicit scheme is the HighQuality default and the fallback for unstable bridge points (SI-0018).

**Planner's check before planning the work (eigenvalues of the exact one-step map, flexible string, 48 kHz, all modes below 20 kHz):**

| Scheme | Worst mode, Z₀G = 0.01 | Worst mode, Z₀G = 0.05 |
| --- | --- | --- |
| Explicit (SPEC order) | −0.42 | −0.32 |
| Option B as written here: implicit (trapezoidal) bridge update, coupling g\_n unchanged | −0.45 | −0.46 |
| Matched coupling g̃\_n = g\_n ω\_n/ω̂\_n with the explicit order | +0.10 | +0.66 |
| **Implicit bridge update + matched coupling g̃\_n** | **1.7 × 10⁻⁴** | **4.3 × 10⁻³** (10⁻³ up to ω\_nΔt ≈ 2.4) |

The implicit update alone does **not** fix the high-mode deficit. The error comes from the discrete second difference δ\_tt w seen by each mode, not from the bridge update. Combined with the matched coupling it does: every kept mode is within 0.1% at Z₀G = 0.01. Against the continuous-time coupled modal system, a stiff string (B = 10⁻³) shows the same errors. The same scheme, with the bridge modes taking the averaged force (Fⁿ⁺¹ + 2Fⁿ + Fⁿ⁻¹)/4, also removes SI-0018's light-bridge instability.

**Acceptance (SPEC-ORC row "Bridge damping", implicit scheme):** every kept mode within 0.1% of the formula at Z₀G = 0.01; at Z₀G = 0.05 within 0.1% for ω\_nΔt ≤ 2.4 and within 0.5% for every kept mode. The explicit-scheme criterion above stays.

**Owner review needed (numerics):** the matched coupling and the averaged bridge force are the planner's proposal. They are normative in SPEC-BRG "Bridge scheme" but flagged for the owner's review of the numerics before A9 merges. A9 proves the energy balance and runs the tests. If a test fails, A9 stops and files a spec issue.

Implemented by phase **A9**. Cost: one extra dot product (aᵀb) per segment and axis per sample, plus one solve of size 1 or 2 per bridge point; A9 measures it.
