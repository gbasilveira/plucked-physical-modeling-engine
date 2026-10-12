# SI-0027 ORC-09 "total energy constant over the whole run" contradicts the MUST reset at separation

Status: resolved (2026-10-12, orchestrator; oracle criterion, no change to engine behaviour) · Raised by: A5, test-author · Date: 2026-10-12
SPEC section: SPEC-ORC row "Hammer–string energy" (ORC-09); SPEC-NL "Drift control" rule 1

## Failing test
ORC-09: lossless string and felt in float64, total energy including ½ψ² constant within 10⁻¹⁰ relative. Over a run that includes rebound, the A5 test-author's SPEC-derived rig measures a loss of 1.5 × 10⁻⁶ E₀ (one string) and 8.3 × 10⁻⁶ E₀ (trichord), each at a ψ reset.

## Evidence
SPEC-NL drift control rule 1 (MUST) sets ψ ← sign(ψ)·√(2ε₀) when Vⁿ = Vⁿ⁻¹ = 0; SPEC says it "can only remove energy". The removed energy is ½ψ² − ε₀ at the reset. Up to the first reset the energy is constant to about 10⁻¹⁴; adding back the energy each reset removed restores constancy over the whole run to the same level. The oracle, as written, asks for something the MUST rule forbids.

## Proposed correction
SPEC-ORC row "Hammer–string energy", criterion: "Total energy including ½ψ² constant up to the first ψ reset; from then on, total energy plus the cumulative energy the resets removed is constant, and every reset removes energy (never adds)." Tolerance unchanged (10⁻¹⁰ relative). The setup names a rigid bridge; the movable-bridge case is reported, not bounded, until SI-0028 is decided.

## Resolution
Applied to SPEC on 2026-10-12 as proposed. Affected phases: A5 (test-author: ORC-09 checks as above; the engine/tests/oracles.md criterion cell should be updated to match in the next A5 or V1 follow-up). A5's `resetEnergyRemoved()` provides the cumulative removed energy; persistent-contact relaxation (drift rule 2) is not counted in it and does not occur in ORC-09's short contact (noted for C1).
