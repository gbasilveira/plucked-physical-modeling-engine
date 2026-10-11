# Contributing

## Workflow

- Trunk-based development. Every change reaches `main` through a pull request.
- Commit messages and pull-request titles start with the phase ID, for example
  `A2: compile wound-string mass and stiffness`, or with `PLAN:` for planning-only changes.
- Phase branches are named `phase/<ID>-<slug>`.
- Requirement and test IDs appear in test names, for example `TEST_CASE("A2-T1 wound string mass")`.
- Every source file carries an SPDX header (`SPDX-License-Identifier: MIT`); `tools/check_spdx.py`
  enforces it.
- Real-time rules, numerics rules and code style are in the Software guidelines of
  [docs/plan/phasing-plan.md](docs/plan/phasing-plan.md).

## Model policy

Planning documentation is written by Opus agents and code by Sonnet agents; see
[CLAUDE.md](CLAUDE.md). Planning documentation is everything under `docs/plan/`, `docs/adr/`,
`docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/` and `docs/agents/`, plus the
narrative and conclusions of the decision reports named in phase cards. Code, tests, CI, build
scripts, tools, module notes and user documentation are written by Sonnet agents.

## Problems with interfaces or the specification

- Interface changes: [docs/icr/README.md](docs/icr/README.md).
- Specification defects: [docs/spec-issues/README.md](docs/spec-issues/README.md).

## Build and test

See [docs/dev/build.md](docs/dev/build.md).

## Required checks

Placeholder: P0.2-R7 fills this section with the checks that protect `main`.
