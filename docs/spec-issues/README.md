# Spec issues (SI)

Agents never deviate from [../plan/engine-specification.md](../plan/engine-specification.md) silently. When a test cannot pass because SPEC is wrong or ambiguous:

1. The agent stops work on the affected requirement and describes the defect in its handoff report, with the failing test and evidence.
2. The `planner` (Opus) files `SI-NNNN-<slug>.md` from the template below.
3. The orchestrator resolves it. Any change to physics or numerics goes to a human reviewer before SPEC is changed.
4. The planner updates SPEC, links the change here, closes the issue, and the agent resumes.

## Template

```markdown
# SI-NNNN <title>

Status: open | accepted | rejected | resolved · Raised by: <phase ID, role> · Date: <YYYY-MM-DD>
SPEC section: <key and heading>

## Failing test
Test or oracle ID, CI run link, observed versus expected values.

## Evidence
Derivation, reference-solver result or literature that shows the defect.

## Proposed correction
The exact SPEC text and formula change.

## Resolution
Decision, human reviewer (for physics or numerics), SPEC commit, affected phases.
```
