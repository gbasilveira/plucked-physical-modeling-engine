# SI-0028 The hammer contact distance ignores the bridge's lifting displacement

Status: open (physics/numerics; owner decision; planner recommendation below) · Raised by: A5, test-author and implementer · Date: 2026-10-12
SPEC section: SPEC-HAM "Contact with each struck string"; SPEC-BRG "Moving support", "Bridge force"; SPEC-ORC "Hammer–string energy"

## Failing test
ORC-09's reported (unbounded) movable-bridge case: a trichord on a lossless mass–spring bridge (0.3 kg, 300 Hz) deviates from energy conservation by 2.4 × 10⁻⁵ relative, scaling as 1/m with the bridge mass. On a rigid bridge the deviation is about 10⁻¹⁴.

## Evidence
SPEC-HAM defines δ\_s = x\_H − s\_E Σ φ̄\_s,n q\_s,n: the window-averaged **modal** displacement only. On a movable bridge the string's displacement is Σ qₙφₙ + w\_b ψ(x) (SPEC-BRG lifting), so the true penetration also contains −s\_E w\_b ψ̄(x\_c) (ψ̄ the window average of the lifting function, about β\_c for a speaking segment). With δ independent of w\_b, the contact does no work on the bridge and the bridge does none on the contact, so the energy of the coupled system is not exactly balanced; the error is of order (lifting displacement)/(modal displacement) at the strike point, which falls as 1/m. The A5 implementer confirms SPEC is explicit and implemented it as written, so the deviation is SPEC's, not A5's. Same root cause as SI-0025 (a nonlinear term that ignores the lifting part of the string's displacement).

Including the term is not free in the explicit coupling order: V\_H then depends on w\_b, and its gradient adds a contact share to the bridge force; the bridge advances (step 3) before the segment's nonlinear solve (step 6), so an exactly energy-consistent form needs the contact force at step n in F\_b (a one-sample-lagged reaction) or the implicit bridge scheme, where the bridge unknown is solved together with the segment's affine response.

## Proposed correction (planner recommendation)
1. Keep SPEC-HAM as written (modal-only δ) as the default: it is what A5 implements, it is cheapest, and the measured defect is 2.4 × 10⁻⁵ of the energy at 0.3 kg, far below audibility. Document the 1/m scaling in SPEC-HAM.
2. Under D-13, plan the alternative as an engine setting `numerics.contactLifting` = Auto, ModalOnly, Full (Realtime → ModalOnly, HighQuality → Full), where Full uses δ\_s with the lifting term and adds the contact's reaction to the bridge force, implemented first for the implicit bridge scheme (where it can be made exactly energy-consistent). It needs: a planner derivation of the discrete form (energy identity), an amendment to ICR-0002 or the next ICR (one enum), and an A5/A9 follow-up. The same setting should cover barriers and other contacts on movable bridges (SPEC-PLK) and SI-0025's tension term if the owner prefers that to be optional.
3. ORC-09 meanwhile: rigid bridge bounded at 10⁻¹⁰ (SI-0027); movable bridge reported.

## Resolution
Pending the owner. Until then SPEC stands, A5 merges as implemented, and the movable-bridge deviation is an open risk recorded in phase-status.
