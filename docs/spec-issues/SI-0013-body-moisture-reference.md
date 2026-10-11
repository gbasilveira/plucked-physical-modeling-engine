# SI-0013 SPEC-BODY moisture coefficients have no reference moisture content

Status: resolved (orchestrator default; flagged for owner review) · Raised by: P0.5, implementer and code-reviewer · Date: 2026-10-11
SPEC section: SPEC-BODY "Plate model (offline)"; SPEC-API `PlateParams`

## Failing test
None yet. B2-T5 ("a moisture change shifts frequencies in the direction and by the amount the coefficients predict, within 1%") cannot be specified without a defined law.

## Evidence
- SPEC-BODY: "Moisture content MC changes E\_L, E\_R, G\_LR and density through linear coefficients supplied with the wood data." It does not say whether the listed constants are values at MC = 0 (law E = E₀ + dE/dMC · MC) or at some reference MC (law E = E\_ref + dE/dMC · (MC − MC\_ref)), and names no reference value.
- `engine/include/pme/body.hpp` (`PlateParams`) documents `youngsL`, `youngsR`, `shearLR`, `density` as "at reference moisture" but has no field for that reference.
- The P0.5 reference applies `value = reference + coefficient * moisture`, reading `moisture` as the change from the reference (reference/README.md "Limits", reference/include/pme/ref/plate.hpp). The B1 examples set `moisture: 0.12` with slopes 0. With non-zero slopes, the two readings differ by dE/dMC × 0.12; for spruce (dE\_L/dMC of order −10¹⁰ Pa per unit MC, E\_L ≈ 11 GPa) that is 10–20 % of E\_L, 5–10 % in frequency.
- Tabulated tonewood constants (e.g. the USDA Wood Handbook) are given at about 12 % MC, so an absolute law at MC = 0 would extrapolate far outside the data.

## Proposed correction
1. SPEC-BODY: the listed E\_L, E\_R, G\_LR and ρ are values at MC\_ref, and E\_L(MC) = E\_L + dE\_L/dMC · (MC − MC\_ref), likewise for E\_R, G\_LR and ρ. MC is a fraction (0.12 = 12 %). MC\_ref defaults to 0.12.
2. Next ICR (compatible, new member with a default): `double moistureRef{0.12};` in `PlateParams`, with the matching B1 schema field. Until it is applied, compileBody (B2) uses the constant 0.12.
3. The P0.5 reference keeps its delta reading; a comparison with non-zero slopes passes `moisture = MC − MC_ref` to the reference. The reference README already states this; no code change.

## Resolution
Resolved 2026-10-11 by the orchestrator's default under the owner's proceed instruction, as proposed. The law (relative to a reference MC) and the default MC\_ref = 0.12 are physical choices: **flagged for owner review**. SPEC-BODY updated in the `PLAN: file SI-0012 to SI-0015 …` commit; `PlateParams::moistureRef` added to the next-ICR list (docs/icr/README.md). Affected phases: B2 (B2-R2, B2-T5 use the relative law with MC\_ref = 0.12), B1 (schema field with the next ICR), E1 to E8 (wood data entered at MC\_ref).
