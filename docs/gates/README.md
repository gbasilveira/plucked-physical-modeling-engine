# Gate reports

One file per gate check, `G<n>.md` (for G7, `G7-<instrument>.md`), written by the `planner` (Opus) when every phase the gate requires is complete. Gate definitions and exit criteria are in the Gates section of [../plan/phasing-plan.md](../plan/phasing-plan.md) and in `gates` of [../plan/phases.yaml](../plan/phases.yaml).

A gate report states only what CI artifacts, the traceability matrix (V1) and handoff reports show. It never asserts a result that has no artifact behind it.

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

## Open issues
Release-blocking issues and their owners.

## Failed criteria and reopened phases
| Criterion | Owning phase | Action |
| --- | --- | --- |
```
