# SI-0023 ORC-11's 10⁻⁹ tolerance needs the forcing function evaluated in float64

Status: resolved (2026-10-12, orchestrator; test condition only, no change to engine behaviour) · Raised by: A7, test-author · Date: 2026-10-12
SPEC section: SPEC-ORC row "Longitudinal forcing" (ORC-11); SPEC-GEO "Longitudinal modes"

## Failing test
ORC-11 compares the pair-sum forcing f\_m^L against quadrature of −(EA − T)/(2ρA) ∫ y\_x² χ\_m′ dx within 10⁻⁹ relative. The engine stores modal states in float32; a pair sum over N modes evaluated in float32 carries relative round-off of order 10⁻⁷, so 10⁻⁹ is unreachable in the engine's float32 instantiation.

## Evidence
float32 unit round-off is 6 × 10⁻⁸; the pair sum accumulates O(N) products with cancellation between the δ\_{i+j,m} and δ\_{|i−j|,m} terms. The oracle checks the formula, not float32 accumulation, so the comparison must run in float64. The A7 implementer provides `S = double` instantiations of the forcing function (A7 per-sample code is templated on the state type, as A3 and A4 are).

## Proposed correction
SPEC-ORC row "Longitudinal forcing", tolerance cell: "10⁻⁹ relative, with the engine's forcing function evaluated in float64 (instantiated with double state; the float32 instantiation cannot reach 10⁻⁹)".

## Resolution
Applied to SPEC on 2026-10-12 as proposed. Affected phases: A7 (test-author: ORC-11 uses the double instantiation; the A7 implementer provides it). The engine/tests/oracles.md row ORC-11 should carry the same note in its criterion cell; an implementer updates it in the next A7 or V1 follow-up (that file is engine-side).
