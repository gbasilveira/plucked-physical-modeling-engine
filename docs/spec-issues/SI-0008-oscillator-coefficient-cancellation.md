# SI-0008 SPEC-OSC's "cancellation-free" ω̂²Δt² formula loses up to three digits at small ωΔt

Status: resolved · Raised by: planner, while briefing A1 · Date: 2026-10-11
SPEC section: SPEC-OSC "Discrete scheme"; phase card A1 (A1-R1, A1-T1)

## Failing test
A1-T1 (not yet written): "coefficients agree with a 50-digit reference within 10⁻¹⁴ relative for ωΔt from 10⁻⁴ to 0.95π and σΔt from 0 to 0.1". The planner evaluated the SPEC-OSC formula in float64 against a 60-digit mpmath reference over that domain (300 × 151 grid of ωΔt and σΔt, modes rejected by the bandwidth rule skipped, ω\_d computed as √((ω − σ)(ω + σ))):

| Region (σ/ω at most) | Worst relative error of ω̂²Δt² (SPEC formula) | At (ωΔt, σΔt) |
| --- | --- | --- |
| 0.01 | 2.9 × 10⁻¹⁴ | (1.2 × 10⁻⁴, 8.1 × 10⁻⁷) |
| 0.1 | 5.0 × 10⁻¹³ | (1.6 × 10⁻⁴, 1.6 × 10⁻⁵) |
| 1 (whole domain) | 4.2 × 10⁻¹² | near ωΔt = 10⁻⁴, σ ≈ 0.75 ω |

The formula as written cannot meet A1-T1's 10⁻¹⁴ in the card's domain. The error is largest for low, heavily damped modes (low body modes, low string partials at 96 kHz).

## Evidence
SPEC-OSC gives

ω̂²Δt² = 2[−expm1(−σΔt) + 2e^(−σΔt) sin²(ω\_dΔt/2)] − 2 σ̂Δt e^(−σΔt) cos(ω\_dΔt), with σ̂Δt = tanh(σΔt).

The first and last terms are each about 2σΔt while their difference is of order σ²Δt², so for small σΔt the subtraction cancels about log₁₀(1/σΔt) digits. It removes the cancellation of 2 − 2cos but not this one.

An exact rewrite has no subtraction. With x = σΔt, y = ω\_dΔt and t = tanh x: (1 + t)e^(−x) = 2e^(−x)/(1 + e^(−2x)) = 1/cosh x, so

ω̂²Δt² = 2 − 2(1 + t)e^(−x) cos y = 2(1 − sech x) + 2 sech x (1 − cos y) = **4 [sinh²(x/2) + sin²(y/2)] / cosh x**.

Every term is positive. Over the same grid this form's worst relative error was 7.0 × 10⁻¹⁶. β, κ and γ computed from it as (1 − t)/(1 + t), ω̂²Δt²/(1 + t) and Δt²/(1 + t) stayed within 7.6 × 10⁻¹⁶. (β also equals e^(−2x) exactly.) Planner's check script: mpmath 60 digits, `uv run --no-project --with mpmath --with numpy`; the A1 implementer reproduces it as the A1-T1 reference table.

The discrete scheme, its poles e^((−σ ± iω\_d)Δt), the difference form and every tolerance are unchanged. Only the float64 evaluation of one compile-time quantity changes, to an algebraically identical expression.

## Proposed correction
In SPEC-OSC "Discrete scheme", replace the sentence "The compiler MUST evaluate ω̂²Δt² without cancellation, in float64:" and the formula after it with:

> The compiler MUST evaluate ω̂²Δt² without cancellation, in float64, using the equivalent form (since (1 + σ̂Δt)e^(−σΔt) = sech(σΔt)):
>
> ω̂²Δt² = 4 [sinh²(σΔt/2) + sin²(ω\_dΔt/2)] / cosh(σΔt), with ω\_dΔt = Δt √((ω − σ)(ω + σ)).
>
> The form 2[−expm1(−σΔt) + 2e^(−σΔt) sin²(ω\_dΔt/2)] − 2σ̂Δt e^(−σΔt) cos(ω\_dΔt) is algebraically equal but loses up to three digits when σΔt is small (SI-0008).

## Resolution
Resolved on 2026-10-11: accepted as proposed. Orchestrator default under owner's proceed instruction — flagged for owner review, numerics text change only, scheme and poles unchanged.

- SPEC-OSC "Discrete scheme": the ω̂²Δt² evaluation sentence and formula are replaced by the proposed text above. The scheme, its poles, the difference form and every tolerance are unchanged.
- A1-T1 keeps its 10⁻¹⁴ tolerance. The A1 brief already uses this form; the unit test comparing it with the old printed form stays as documentation.
- SPEC commit: "PLAN: resolve SI-0008".
- If the owner reverses this, SPEC-OSC reverts and A1-T1's tolerance must be relaxed instead (see the analysis above).

Affected phases: A1 (A1-R1, A1-T1). Every later phase uses `compileOscillator` and is unaffected.
