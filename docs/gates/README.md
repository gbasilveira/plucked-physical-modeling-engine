# Gate reports

One file per gate check, `G<n>.md` (for G7, `G7-<instrument>.md`), written by the `planner` (Opus) when every phase the gate requires is complete. Gate definitions and exit criteria are in the Gates section of [../plan/phasing-plan.md](../plan/phasing-plan.md) and in `gates` of [../plan/phases.yaml](../plan/phases.yaml).

A gate report states only what CI artifacts, the traceability matrix (V1) and handoff reports show. It never asserts a result that has no artifact behind it.

**Plain-text documentation (decision D-11, owner, 2026-10-12).** The owner approves G5 and every G7 on condition that the documentation is detailed and textual, whatever its size. Every gate report, not only G5 and G7, therefore contains complete plain-text explanations of:

- what was built;
- every decision taken (by the owner, or by default and flagged);
- every deviation from SPEC or the plan, with its reason;
- every known limitation.

Tables and links support the text but never replace it. A reader must be able to follow each item without opening other files. Release notes follow the same rule (H4-R7).

## Template

```markdown
# G<n> <gate name> — gate report

Date: <YYYY-MM-DD> · Main-branch commit: <sha> · Result: passed | failed
Human approval (G5, G7 only): <name, date> | not required

## Required phases
| Phase | Status | Pull requests |
| --- | --- | --- |

## Exit criteria
| Criterion (verbatim from the Gates table) | Evidence (CI run, artifact, report) | Met |
| --- | --- | --- |

## Traceability
Link to the V1 matrix artifact for this commit; list any gaps.

## What was built
Plain text, per phase: what exists now and what it does.

## Decisions
Every decision that shaped this gate's work: owner answers, defaults in effect, resolved spec issues and ICRs, each explained in sentences.

## Deviations
Every deviation from SPEC or the plan (accepted spec issues, deferred requirements, widened tolerances), with its reason and who accepted it.

## Known limitations
Everything that does not work, works only partly, or was measured but not bounded (for example per-mode deviations reported but not limited).

## Open issues
Release-blocking issues and their owners.

## Failed criteria and reopened phases
| Criterion | Owning phase | Action |
| --- | --- | --- |
```
