# ADR-NNNN: <decision in a few words>

- **Status:** Proposed | Accepted | Superseded by ADR-NNNN | Deprecated
- **Date:** YYYY-MM-DD
- **Deciders:** <who decided: the human owner, or the planner (Opus) under a rule of the phasing plan; name the rule>
- **Phase:** <phase ID that raised the decision>
- **References:** <SPEC / PLAN keys, phasing-plan sections, phase cards, issues>

## Decision

<One or two sentences: what is decided. Lead with it.>

## Context

<The problem and the constraints that force a choice. Quote the binding text (SPEC, phasing plan, PLAN) where it constrains the options. State facts with numbers and units; say where each fact comes from.>

## Options considered

| Option | For | Against |
| --- | --- | --- |
| <chosen option> | | |
| <alternative> | | |

## Consequences

- <What later phases must do, or may no longer do, because of this decision. Name the phases.>
- <Licence, build, performance or numerics effects.>
- <Risks this decision accepts, and how they are watched.>

## Verification

<How the repository shows the decision is followed: a test ID, a CI check, a review checklist item.>

## Revisiting

<What evidence would reopen the decision, and which phase would produce it. A replacement decision is a new ADR that supersedes this one; this file is then only updated in its Status line.>

<!--
Rules (docs/adr/README.md and the Software guidelines in docs/plan/phasing-plan.md):
- File name ADR-NNNN-<slug>.md; numbers are never reused.
- ADRs are planning documentation, written by the planner (Opus).
- An ADR records a decision not fixed by SPEC or PLAN. It cannot change SPEC; a SPEC defect is a spec issue (docs/spec-issues/).
- An ADR that defers a phase-card requirement or test must name the requirement or test ID, and needs the orchestrator's approval (the human owner's for anything in the Decisions table) before its Status becomes Accepted.
-->
