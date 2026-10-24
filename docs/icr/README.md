# Interface change requests (ICR)

Interfaces created in P0.4 and tagged `interfaces-v<n>` are frozen. Any change follows the protocol in the Agent briefs and handoff section of [../plan/phasing-plan.md](../plan/phasing-plan.md):

1. The need is raised (by an implementer in its handoff report, or directly by the planner). The `planner` (Opus) writes `ICR-NNNN-<slug>.md` from the template below.
2. The orchestrator approves or rejects it and records the decision in the file.
3. If approved, one interface agent (an `implementer`, Sonnet) applies the change to headers and stubs and bumps the interfaces tag; the planner updates SPEC-API if the change alters it.
4. Every affected in-flight phase is notified and rebases before continuing.

## Template

```markdown
# ICR-NNNN <title>

Status: proposed | approved | rejected | applied · Raised by: <phase ID, role> · Date: <YYYY-MM-DD>

## Problem
What cannot be done with the current interface, and which requirement or test it blocks.

## Proposed change
The header diff (declarations only) and the SPEC-API text change, if any.

## Phases affected
| Phase | Impact | Must rebase |
| --- | --- | --- |

## Decision
Approved or rejected by the orchestrator, with the reason. Interfaces tag after applying: interfaces-v<n+1>.
```

## Test names in an ICR

P0.3-R1 requires every test name to begin with a requirement or test ID of a phase card. An ICR ID is not one. When an ICR adds or renames tests, its "Tests that must change" table names them with the ID of the phase that owns the interface, followed by the ICR ID, for example `P0.4-T3 ICR-0001 retired id 1010 is not registered` (interfaces from P0.4, as applied in 300b995) or `C4-R2 ICR-0003 …` (an interface owned by a later phase). Never start a test name with `ICR-`.

## Carried to the next ICR

**2026-10-12:** every item below is now part of [ICR-0002](ICR-0002-interfaces-v3.md) (approved 2026-10-12). The list stays for history.

Non-blocking items from the ICR-0001 review (see [ICR-0001 "Application"](ICR-0001-interfaces-v2.md#application)); the next ICR includes them as documentation-only items:

- Stale "interfaces-v1" comments in `engine/include/pme/nonlinear.hpp` (lines 7, 16) and `engine/include/pme/pme.hpp` (line 5).
- Redundant `pedal.soft` (1003) description.
- `docs/dev/interfaces.md` lists non-audio functions under "Real-time functions".
- `engine/include/pme/nonlinear.hpp` says the SAV types are declared privately by A5; A3 declares them, in `engine/src/nonlinear/include/pme/nonlinear/sav.hpp` (A5 adds the hammer potential). Comment only.
- SI-0010 (resolved 2026-10-11): add `double muteFeltSigma{20.0}` (σ\_felt, s⁻¹) to `Defaults` (compatible: new member with a default).
- A4 (2026-10-11, candidate): `SegmentBridgeTerms` holds one `a`; the horizontal polarisation needs a\_n,h = ρA ω̂²\_h,n g\_n from `oscH`. A4 provides it through `pme::bridge::horizontalForceCoefficients` (engine/src/bridge/include/pme/bridge/terms.hpp); a member `std::vector<float> aH` in `SegmentBridgeTerms`, filled by `compileBridgeTerms`, would make it part of the contract (compatible). Confirmed by the A4 implementer handoff 2026-10-12.
- SI-0013 (resolved 2026-10-11): add `double moistureRef{0.12}` (MC\_ref, dimensionless fraction) to `PlateParams` (compatible: new member with a default), with the matching B1 schema field; until then compileBody uses MC\_ref = 0.12.
- B2 handoff (2026-10-12, proposed by the B2 implementer): (1) the SI-0013 `PlateParams::moistureRef` member above, confirmed as needed by B2-T5; (2) an optional static-prestress field in the instrument description (B2-R4: crown and downbearing). B2 takes prestress as a `compiler/pme_compiler/body` API argument only, so an instrument file cannot carry it. Proposal: an optional `PlateParams` member (for example `prestress`, in-plane membrane stress resultants N\_x, N\_y, N\_xy in N/m, default zero = none) with the matching B1 schema field; compatible (new member with a default). Exact form to be settled in the ICR; physics review needed (it changes body modes).

## Index

| ID | Title | Status | Interfaces | Spec issues |
| --- | --- | --- | --- | --- |
| [ICR-0001](ICR-0001-interfaces-v2.md) | interfaces-v2: instrument retirement, plucked exciters, tuning keys, body geometry, registry clean-up | applied (300b995; tag push pending owner) | v1 → v2 | SI-0003 to SI-0007; SI-0001 and SI-0002 follow-ups |
| [ICR-0002](ICR-0002-interfaces-v3.md) | interfaces-v3: engine quality and numerical options (D-13), bridge-switch diagnostic, flexible shank, plate prestress and moistureRef, carried items | approved 2026-10-12 (orchestrator, under D-13; owner's final review pending); not yet applied; amendment `StringModes::omegaH` proposed 2026-10-13, pending orchestrator approval | v2 → v3 | SI-0010, SI-0013, SI-0018, SI-0020, SI-0021, SI-0032 |
