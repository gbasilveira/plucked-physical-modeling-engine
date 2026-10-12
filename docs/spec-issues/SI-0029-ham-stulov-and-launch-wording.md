# SI-0029 SPEC-HAM is silent on the Stulov force out of contact and per strike weight, and on what "contact starts on the next sample" means

Status: resolved (2026-10-12, planner ratifies A5's implementation; clarifications, flagged for owner review with the D-13 items) · Raised by: A5, implementer · Date: 2026-10-12
SPEC section: SPEC-HAM "Velocity map", "Alternative: Stulov hereditary felt"

## Failing test
None. Two silences and one ambiguity found while implementing A5-R1 and A5-R3.

## Evidence
1. **Stulov out of contact.** SPEC gives F\_hered = −K ε h along c\_s with h a low-pass of [δ]₊^p, but not whether the force acts when δ\_s ≤ 0. A felt cannot pull the string, so A5 applies the hereditary force only while δ\_s > 0; h keeps evolving (it decays towards 0 while out of contact).
2. **Stulov and strike weight.** The elastic part carries w\_s (V\_H = Σ w\_s K\_s …); SPEC's hereditary force has no w\_s. A5 scales it by w\_s, so a string with w\_s = 0 (una corda) feels no hereditary force either.
3. **Energy.** At ν = 0.8, ε = 0.3, τ = 0.5 ms A5 measured an energy gain of 2.4 % of the hammer energy (one string) and 3.8 % (trichord), consistent with "not provably passive". Energy monitors report it.
4. **Launch.** "Launched at a gap of v\_H Δt … so contact starts on the next sample": A5 has δ = 0 at launch + 1 (touching) and δ = v\_H Δt > 0 at launch + 2 (first non-zero force). The alternative reading (non-zero force at launch + 1) would need a zero gap. The difference is one sample (about 20 µs) and A5's reading keeps the hammer outside the string at launch.

## Proposed correction
- SPEC-HAM Stulov paragraph: "The hereditary force of string s acts only while δ\_s > 0 and is scaled by its strike weight w\_s, like the elastic part; h keeps evolving while out of contact." Plus the measured energy gain.
- SPEC-HAM velocity map: "… so it touches the string (δ = 0) on the next sample and the contact force is non-zero from the sample after."
- Open question for the A5 follow-up (no SPEC change yet): whether the total felt force K(δ^p − εh) of a string can become negative near the end of contact with fitted parameters; if it can, the planner proposes limiting the hereditary part so the felt never pulls. A5 reports whether it occurs at ν = 1 and ε = 0.5.

## Resolution
Applied to SPEC on 2026-10-12. A5's implementation stands (docs/dev/hammer.md). Flagged for the owner's final review with the other numerics items. Affected phases: A5 (none), B5 (fits ε and τ with this definition).
