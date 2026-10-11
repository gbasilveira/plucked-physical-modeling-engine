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

## Index

| ID | Title | Status | Interfaces | Spec issues |
| --- | --- | --- | --- | --- |
| [ICR-0001](ICR-0001-interfaces-v2.md) | interfaces-v2: instrument retirement, plucked exciters, tuning keys, body geometry, registry clean-up | approved | v1 → v2 | SI-0003 to SI-0007; SI-0001 and SI-0002 follow-ups |
