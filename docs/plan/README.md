# Planning documents

All planning documentation for the project. These files are written and maintained by Opus agents only (model policy in [../../CLAUDE.md](../../CLAUDE.md)).

| File | Key | Role |
| --- | --- | --- |
| [phasing-plan.md](phasing-plan.md) | — | Binding work breakdown: guidelines, phase cards, gates, agent briefs, decisions |
| [phases.yaml](phases.yaml) | — | Machine-readable mirror of the phasing plan; the orchestrator schedules from it |
| [phase-graph.md](phase-graph.md) | — | Mermaid dependency graph generated from phases.yaml |
| [engine-specification.md](engine-specification.md) | SPEC | Normative physics, numerical schemes, execution order and interfaces |
| [implementation-plan.md](implementation-plan.md) | PLAN | Background: architecture, stack, compute budget, validation, risks; roadmap superseded |
| [literature-survey.md](literature-survey.md) | — | Research survey of papers and software |
| [verification/](verification/) | — | Numerical checks run while SPEC was written |
| [tools/gen_phase_graph.py](tools/gen_phase_graph.py) | — | Validates phases.yaml and regenerates phase-graph.md and the status tracker |
| [assets/](assets/) | — | Rendered diagrams |

Precedence: SPEC governs engine behaviour; the phasing plan governs phases, outputs and tests; PLAN applies where neither says otherwise.
