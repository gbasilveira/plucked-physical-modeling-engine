# Brief: <phase ID> <phase name> — <planner | implementer | code-reviewer | test-author>

Model: <opus for planner; sonnet for implementer, code-reviewer and test-author>
Branch: phase/<ID>-<slug>
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective
<the phase card's description, verbatim>

## Read first
- docs/plan/phasing-plan.md: Software guidelines; the phase card <ID>
- SPEC and PLAN sections: <keys from the card's References, each with its document and heading>

## Inputs
- Completed dependencies and their outputs: <phase IDs, paths, tags, pull requests>
- Frozen interfaces: interfaces-v<n>

## Scope
- May edit: <paths from the card's Outputs (implementer) or planning_outputs (planner)>
- Read only: everything else
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements
<the card's requirement IDs and text, verbatim>

## Tests that must pass
<the card's test IDs and criteria, plus any ORC IDs named>

## Definition of done
- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

## Report
Return the handoff report as JSON matching docs/agents/report.schema.json.
