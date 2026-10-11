# SI-0012 SPEC-BRG Im Y has no stated time convention

Status: resolved · Raised by: P0.5, implementer and code-reviewer · Date: 2026-10-11
SPEC section: SPEC-BRG "Small-admittance results"; notation "Sign conventions"

## Failing test
None. Found while implementing and reviewing P0.5-R3 ("a bridge mass shifts the partials as the exact end-mass equation `cot(kL) = M k / rhoA` says, and SPEC-BRG's `-2 f0 Z0 Im Y` agrees with that shift within 5%"). The test passes with Y = 1/(iωm). It would fail with the opposite sign of Im Y, and SPEC does not say which sign is meant. A4-T2 ("a mass-like bridge shifts partials as Δω ≈ −2 f₀ Z₀ Im Y within 2%") depends on the same choice.

## Evidence
- SPEC-BRG gives Δω\_n ≈ −2 f₀ Z₀ Im Y(ω\_n) without stating whether phasors rotate as e^(+iωt) or e^(−iωt). The two conventions give Y\_mass = 1/(iωm) or −1/(iωm), opposite signs of Im Y.
- The exact end-mass equation cot(kL) = Mk/ρA has its roots just above kL = nπ: a mass-loaded end (no spring) raises the partials (Gough's mass-like bridge). Under e^(+iωt), Im Y\_mass = −1/(ωm) < 0, so −2 f₀ Z₀ Im Y > 0: a rise. The formula is correct under e^(+iωt). P0.5-R3 confirms it within 5%.
- SPEC-BODY's radiation integral already uses e^(−iΩ|R−x|/c₀) for an outgoing wave, which is also the e^(+iωt) convention, so stating it makes the two sections consistent.

## Proposed correction
Text only, no formula change:

1. Notation, "Sign conventions": add "Complex amplitudes, admittances and radiation gains use the time convention e^(+iωt): x(t) = Re[X e^(iωt)], d/dt ↦ iω, an outgoing wave is e^(i(ωt − kr)), a mass m has Y = 1/(iωm) and a spring k has Y = iω/k."
2. SPEC-BRG "Small-admittance results": add "Y is the bridge velocity per unit force under the e^(+iωt) convention. A mass-like bridge (Im Y < 0) raises the partials and a spring-like bridge (Im Y > 0) lowers them; with e^(−iωt) the sign flips."

## Resolution
Resolved 2026-10-11 by the orchestrator under the owner's proceed instruction, as proposed. Clarifying text; no physics or numerics change, so no human reviewer is needed. SPEC updated in the `PLAN: file SI-0012 to SI-0015 …` commit. Affected phases: A4 (A4-T2 uses Y = 1/(iωm) for a mass), B3 (mobilities follow the same convention). P0.5 already conforms.
