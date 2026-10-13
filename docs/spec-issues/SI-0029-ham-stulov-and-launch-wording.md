# SI-0029 SPEC-HAM is silent on the Stulov force out of contact and per strike weight, and on what "contact starts on the next sample" means

Status: resolved (2026-10-12, planner ratifies A5's implementation; clarifications, flagged for owner review with the D-13 items) · **corrected 2026-10-13** (Stulov sign defect and non-negativity clamp; see Correction; rigid-wall figure corrected to the clamped 0.7187 the same day) · Raised by: A5, implementer · Date: 2026-10-12
SPEC section: SPEC-HAM "Velocity map", "Alternative: Stulov hereditary felt"

## Failing test
None. Two silences and one ambiguity found while implementing A5-R1 and A5-R3.

## Evidence
1. **Stulov out of contact.** SPEC gives F\_hered = −K ε h along c\_s with h a low-pass of [δ]₊^p, but not whether the force acts when δ\_s ≤ 0. A felt cannot pull the string, so A5 applies the hereditary force only while δ\_s > 0; h keeps evolving (it decays towards 0 while out of contact).
2. **Stulov and strike weight.** The elastic part carries w\_s (V\_H = Σ w\_s K\_s …); SPEC's hereditary force has no w\_s. A5 scales it by w\_s, so a string with w\_s = 0 (una corda) feels no hereditary force either.
3. **Energy (withdrawn 2026-10-13, see Correction).** At ν = 0.8, ε = 0.3, τ = 0.5 ms A5 measured an energy gain of 2.4 % of the hammer energy (one string) and 3.8 % (trichord) and this issue recorded it as consistent with "not provably passive". That gain was a sign defect in A5's code, not a property of the model.
4. **Launch.** "Launched at a gap of v\_H Δt … so contact starts on the next sample": A5 has δ = 0 at launch + 1 (touching) and δ = v\_H Δt > 0 at launch + 2 (first non-zero force). The alternative reading (non-zero force at launch + 1) would need a zero gap. The difference is one sample (about 20 µs) and A5's reading keeps the hammer outside the string at launch.

## Proposed correction
- SPEC-HAM Stulov paragraph: "The hereditary force of string s acts only while δ\_s > 0 and is scaled by its strike weight w\_s, like the elastic part; h keeps evolving while out of contact." Plus the measured energy gain.
- SPEC-HAM velocity map: "… so it touches the string (δ = 0) on the next sample and the contact force is non-zero from the sample after."
- ~~Open question for the A5 follow-up~~ (closed 2026-10-13 by the clamp below) (no SPEC change yet): whether the total felt force K(δ^p − εh) of a string can become negative near the end of contact with fitted parameters; if it can, the planner proposes limiting the hereditary part so the felt never pulls. A5 reports whether it occurs at ν = 1 and ε = 0.5.

## Resolution
Applied to SPEC on 2026-10-12. A5's implementation stands (docs/dev/hammer.md). Flagged for the owner's final review with the other numerics items. Affected phases: A5 (none), B5 (fits ε and τ with this definition).

## Correction (2026-10-13, after the A5 code review)

**What was wrong.** SPEC-HAM stated the force as K(δ^p − εh) but wrote the explicit term as "F\_hered = −Kεh along c\_s". The elastic generalized force is −∂V\_H/∂z = −w\_s K\_s δ\_s^p c\_s, so a hereditary term with the same sign along c\_s adds stiffness. A5 implemented that literal reading and produced K(δ^p + εh). The A5 code reviewer found it (blocking). Rigid-wall bounce, m\_H = 8 g, K = 4·10⁹, p = 2.5, ε = 0.3, τ = 0.5 ms, v = 3 m/s, rebound KE / incoming KE:

| Force law | Ratio |
| --- | --- |
| K(δ^p + εh) (A5 code, SPEC's per-coordinate text) | 1.358 |
| K δ^p (ε = 0) | 1.000 |
| K max(0, δ^p − εh) (SPEC law with the no-pull clamp; **governs**) | 0.7187 at 48 kHz (0.717 converged at 384 kHz) |
| K(δ^p − εh) unclamped (pulls during unloading; context only) | 0.598 at 48 kHz (0.604 converged) |

**Figure corrected 2026-10-13 (second correction).** The first version of this correction quoted 0.598 as the expected ratio. That is the unclamped law, which pulls the hammer back during unloading and so removes more energy than felt can. With the clamp that SPEC-HAM states, the ratio is 0.7187 at 48 kHz (A5 regression test, commit 4a1d133, asserts 0.7187 ± 1 %). Hereditary work over the contact divided by the initial hammer energy E0 at ε = 0.3: −3.24·10⁻² (one string), −4.20·10⁻² (three strings); negative, i.e. dissipated.

The 2.4 % / 3.8 % energy gains in item 3 therefore came from the sign defect. The record above is corrected; the figures MUST NOT be quoted as Stulov's non-passivity.

**Ruling (orchestrator, 2026-10-13).** The stated scalar law K(δ^p − εh) governs: the hereditary term softens the felt and dissipates. While in contact the total felt force is clamped at ≥ 0 (felt cannot pull).

**SPEC change (applied 2026-10-13, SPEC-HAM "Alternative: Stulov hereditary felt").** Felt force magnitude f\_s = w\_s K\_s max(0, δ\_s^p − εh\_s); explicit term F\_hered,s = +w\_s K\_s min(εh\_s, δ\_s^p) c\_s while δ\_s > 0, i.e. +w\_s K\_s min(εh, δ^p) on x\_H (towards the string) and −s\_E φ̄\_s,n w\_s K\_s min(εh, δ^p) on string mode n, opposite to the elastic force on every coordinate. h is per string (h\_s). Items 1, 2 and 4 of the first resolution stand. The A5 implementer and code-reviewer briefs are amended to match. This is a correction of the sign to the SPEC's own stated law plus a physical constraint, not new physics; it is listed for the owner's final numerics review with the other D-13 items.

**Is "not provably passive" still true?** Yes, as far as is known:
- For p = 1 the law is a standard linear solid (relaxation modulus K(1 − ε(1 − e^{−t/τ})), positive and non-increasing for 0 ≤ ε < 1), which is passive in continuous time.
- For p > 1 (the piano range, 2–4) it is a quasi-linear viscoelastic law in δ^p. We know of no general passivity proof for it. The power of the hereditary term is w K εh δ̇: it returns energy while δ grows and removes more while δ shrinks, because h lags δ^p; the rigid-wall case above dissipates 28 % with the clamp (40 % without), but that is one trajectory, not a proof (multiple contacts and re-contact with residual h are the cases a proof would have to cover).
- The clamp only limits the magnitude of the hereditary force, whatever the sign of its power, so it neither creates energy nor proves passivity.
- The discrete scheme adds no proof: the hereditary force is explicit against an SAV elastic part.

SPEC therefore keeps "not provably passive", and energy monitors MUST check it; a gain of hammer–string energy across a contact is now to be reported as a defect, not as expected behaviour.

**Affected phases.** A5: changes requested; the implementer fixes the sign and adds the clamp (done in commit 4a1d133, regression test 0.7187 ± 1 %), the test author wires A5-T2, ORC-09 and ORC-19 (the ORC-19 golden envelope, if recorded with the Stulov law, must be re-recorded). B5: its card (B5-R4) fits K, p and μ\_HC only; any fit of ε and τ (SPEC says they are fitted) MUST use the corrected sign and clamp; the B5 card carries a note.
