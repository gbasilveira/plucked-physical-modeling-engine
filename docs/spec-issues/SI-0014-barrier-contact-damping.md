# SI-0014 SPEC-PLK barrier contact damping "one dashpot along ∇V_B" is not normalised

Status: resolved (orchestrator default; flagged for owner review) · Raised by: P0.5, implementer and code-reviewer · Date: 2026-10-11
SPEC section: SPEC-PLK "Distributed barriers"; SPEC-NL "Discrete scheme"

## Failing test
None. Found while implementing P0.5-R5 (string released onto a barrier, energy non-increasing with contact damping). The reference could not implement the SPEC sentence as written and applied one dashpot per contact point instead (docs/dev/reference-solver.md, "Contacts"). Without contact damping (the ORC-15 setup) the two are identical, so no test fails today; P2-T1 and P2-R3 need a defined model.

## Evidence
- SPEC-PLK: "Contact damping lumps into one dashpot along ∇V\_B with R = μ\_B K\_B Σ\_j Δx\_j [η\_j]₊^α."
- SPEC-NL's dashpot term is −R c\_i (cᵀ δ\_t· z). Its dissipated power is R (cᵀ ż)², so R's scale depends on the length of c. With c = ∇V\_B (units of force, magnitude ∝ K\_B Σ Δx\_j [η\_j]^α) the product R c cᵀ has the wrong units and the damping grows with the square of the contact force; with c a unit vector the meaning of R is still unstated. Either way the coefficient is not defined.
- The Hunt–Crossley law the sentence comes from is local: each element of contact length Δx\_j has a dashpot Δx\_j K\_B μ\_B [η\_j]₊^α on its own penetration velocity. The reference implements that (R\_j along c\_j = −s\_b φ(x\_j)).
- A per-point dashpot set has rank J\_active; SPEC-NL allows at most 3 dashpot ranks per segment, so the engine needs a rank-1 lumped form (P2-R3 "Lumped contact damping along ∇V\_B").

## Proposed correction
SPEC-PLK "Distributed barriers": replace the lumping sentence with

1. The physical model: per-point dashpots R\_j = Δx\_j K\_B μ\_B [η\_j]₊^α on c\_j = ∂η\_j/∂z = −s\_b φ(x\_j); dissipated power Σ\_j R\_j (c\_jᵀ ż)².
2. The engine's lumped form (rank 1, as P2-R3 requires): direction ĉ = ∇V\_B / |∇V\_B| (Euclidean norm over the segment's coordinates) and coefficient R = Σ\_j R\_j (c\_jᵀ ĉ)², the Rayleigh quotient of the per-point damping matrix along ĉ; R = 0 when ∇V\_B = 0. Evaluated at step n like every dashpot, so it only removes energy. It equals the per-point model when one point is in contact; with several it damps only the velocity along ĉ.
3. The reference's per-point model is the yardstick for comparisons with contact damping.

## Resolution
Resolved 2026-10-11 by the orchestrator's default under the owner's proceed instruction, as proposed. This defines dissipation physics and its numerical lumping: **flagged for owner review**. SPEC-PLK updated in the `PLAN: file SI-0012 to SI-0015 …` commit. Affected phases: P2 (P2-R3 implements the lumped form; P2-T1 compares against the reference's per-point model, and a contact-damping comparison needs its own tolerance when several points are in contact), P4 (jawari, thread). P0.5 already conforms (per-point model).
