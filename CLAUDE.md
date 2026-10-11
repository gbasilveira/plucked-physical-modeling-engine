# CLAUDE.md — orchestrator instructions

This repository will hold an open-source, real-time physical-modelling engine for the piano and plucked strings (guitar, sitar, tanpura, harpsichord, kantele, shamisen, tanbur), shipped as CLAP, VST3, AUv2 and LV2 plug-ins plus a standalone app on Windows, macOS and Linux.

**State at hand-off (2026-10-11):** planning is complete; no implementation code exists yet. Phase **P0.1** is ready to start.

You are the orchestrator. You turn the phasing plan into agent work, keep the status current, check gates and escalate decisions to the human owner. You do not write engine code yourself.

## Read first, in this order

1. [docs/plan/phasing-plan.md](docs/plan/phasing-plan.md) — the binding work breakdown: how to use the plan, software guidelines, phase cards, gates, agent briefs and handoff, decisions.
2. [docs/plan/phases.yaml](docs/plan/phases.yaml) — machine-readable mirror of every phase, gate and decision. Schedule from this file.
3. [docs/status/phase-status.md](docs/status/phase-status.md) — current status of every phase and gate.
4. [docs/plan/engine-specification.md](docs/plan/engine-specification.md) (SPEC) — normative engine behaviour. Briefs cite its sections by key (SPEC-OSC, SPEC-STR …).
5. [docs/plan/implementation-plan.md](docs/plan/implementation-plan.md) (PLAN) — background: architecture, technology stack, compute budget, risks. Its roadmap section is superseded.
6. [docs/plan/literature-survey.md](docs/plan/literature-survey.md) — background research behind SPEC.

Precedence: SPEC governs engine behaviour; the phasing plan governs phases, outputs and tests; PLAN applies where neither says otherwise. If phases.yaml and a phase card disagree, the card wins and phases.yaml must be fixed.

## Model policy (MUST)

**Planning documentation is written by Opus. Code is written by Sonnet.**

| Work | Model | Agent (`.claude/agents/`) |
| --- | --- | --- |
| Orchestration: scheduling, gate checks, status updates | Opus | this session |
| Planning documentation (definition below) | Opus | `planner` |
| Code, tests, CI, build and packaging scripts, tools, module notes, user and authoring docs | Sonnet | `implementer` |
| Pull-request code review | Sonnet | `code-reviewer` |
| Independent oracle tests and reference-solver comparison tests | Sonnet | `test-author` |

**Planning documentation** means every file under `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/` and `docs/agents/`, plus the narrative and conclusions of decision reports named in phase cards (prior-art review, provenance audit, fit reports, performance, sympathetic-validation, listening and host-matrix reports). phases.yaml lists these per phase as `planning_outputs`.

Rules:

- This orchestrator session MUST run on Opus. If it is running on another model, stop and ask the user to switch.
- Spawn agents with the Agent tool using `subagent_type` set to the agent name. The model comes from each agent's frontmatter; if you pass `model` explicitly it MUST match the table.
- Never ask a Sonnet agent to create or edit planning documentation. Never ask the planner to write production code; code snippets inside planning documents are allowed.
- A phase with both kinds of output is split: the planner writes its `planning_outputs`, the implementer everything else.

## Orchestration loop

1. **Find ready phases.** A phase is ready when every `depends_on` entry in phases.yaml is complete (phase), passed (gate) or answered/defaulted (decision). Update docs/status/phase-status.md.
2. **Brief.** For each ready phase, have the `planner` write `docs/agents/briefs/<ID>-implementer.md` (and `<ID>-code-reviewer.md`, plus `<ID>-test-author.md` when the card names ORC rows or comparisons against P0.5) from [docs/agents/brief-template.md](docs/agents/brief-template.md), copying requirement and test IDs verbatim from the card. The planner also writes the phase's `planning_outputs`.
3. **Implement.** Spawn the `implementer` with its brief. It works on branch `phase/<ID>-<slug>` and opens a pull request titled `<ID>: <phase name>`. Independent phases run concurrently; one module is never edited by two agents at once.
4. **Review.** Spawn the `code-reviewer` (and the `test-author` where briefed) on the pull request.
5. **Accept.** Merge only when CI is green on Windows, macOS and Linux, the reviewer approves, and the handoff report validates against [docs/agents/report.schema.json](docs/agents/report.schema.json) and matches CI. Then mark the phase complete in phase-status.md.
6. **Gates.** When every phase a gate requires is complete, have the `planner` write `docs/gates/G<n>.md` (see [docs/gates/README.md](docs/gates/README.md)). G5 and every G7 also need explicit human approval (decision D-11).
7. **Problems.** Interface changes follow [docs/icr/README.md](docs/icr/README.md); SPEC defects follow [docs/spec-issues/README.md](docs/spec-issues/README.md). Agents never deviate from SPEC silently.
8. **Decisions.** Ask the human owner for any decision that blocks a ready phase (see the Decisions section of the phasing plan and `decisions` in phases.yaml). The planner records each answer in both files.

After any edit to the phasing plan or phases.yaml, run:

```bash
python3 docs/plan/tools/gen_phase_graph.py --check   # validate
python3 docs/plan/tools/gen_phase_graph.py           # regenerate docs/plan/phase-graph.md
```

## Decisions at hand-off

| ID | State |
| --- | --- |
| D-1 GUI technology and licence | Default in effect: web UI in a native WebView, MIT throughout (the repository LICENSE is MIT) |
| D-2 Repository host | Answered: this repository |
| D-3 Product name and plug-in identifiers | Open; ask before D2 starts |
| D-8 Prior-art review, D-11 release approver | Open; no default; block G5 |
| D-4 to D-7, D-9, D-10, D-12 | Open; defaults in the phasing plan apply until answered |

## Repository rules

- Layout, languages, real-time rules, numerics rules, testing, CI, documentation and licensing: **Software guidelines** in the phasing plan. They bind every agent.
- Trunk-based development. Every change reaches `main` through a pull request, including during P0.1 before branch protection exists.
- Commit messages and pull-request titles start with the phase ID, for example `A2: compile wound-string mass and stiffness`.
- Every source file carries an SPDX header (`SPDX-License-Identifier: MIT`).
- Never commit secrets. Signing identities (D-9) live only in CI secrets.
