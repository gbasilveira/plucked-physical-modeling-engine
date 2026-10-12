# Brief: H1 task H1-agpl (relicense to AGPL-3.0-or-later) — code-reviewer

Status: scheduled; review once the H1-agpl implementer's commits are merged.

Model: sonnet
Branch: review on the branch the orchestrator names; do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

makes the project legally publishable: licences, third-party notices, data provenance, trademarks and a prior-art review.

Your job: verify that the H1-agpl change (docs/agents/briefs/H1-agpl-implementer.md) applies AGPL-3.0-or-later exactly and changes nothing else.

## Read first

docs/adr/ADR-licence.md; the implementer brief; docs/plan/decisions/D-1-licence-analysis.md sections 4 and 8; `.claude/agents/code-reviewer.md`.

## Inputs

Commits starting `H1: ` (`git log --grep '^H1: '`); CI evidence from the orchestrator.

## Scope

May edit nothing except new tests `tools/tests/test_h1_review_*.py` (names start with the ID, for example `test_h1_t2_review_…`). Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/.

## Requirements

1. H1-R1 Licence decision recorded as an ADR per decision D-1; LICENSE files match it.
2. H1-R2 SPDX headers on every source file, checked in CI.

## Tests that must pass

H1-T2 no source file lacks an SPDX header (with `AGPL-3.0-or-later`).

## Review checklist

1. LICENSE is byte-identical to https://www.gnu.org/licenses/agpl-3.0.txt (download and compare SHA-256 yourself).
2. Every changed source file differs from its parent only in the SPDX line (script the check over the whole diff; list exceptions).
3. Vendored and third-party files are untouched and still pass with their own licence text.
4. `tools/check_spdx.py` requires the exact identifier; a planted MIT, AGPL-3.0-only or GPL-3.0-or-later header on a project file fails; its tests cover this.
5. `pyproject.toml`, CMake/packaging and plug-in metadata (LV2 `doap:license`, descriptors, installers) carry AGPL-3.0-or-later; no "MIT" licence statement about our own code remains (`git grep -n -i "\bMIT\b"` reviewed line by line; third-party mentions are fine).
6. README and docs/user state the licence, the network-use (section 13) duty and the Mac App Store consequence.
7. The dependency table in the report matches `pyproject.toml`, `uv.lock` direct dependencies and `cmake/dependencies.cmake`.

## Output and report

Verdict **approve** or **request changes** (items tied to checklist numbers, file and line). Handoff report per docs/agents/report.schema.json, `phase` `H1`, `role` `code-reviewer`, `model` `sonnet`; `notes` start with the verdict.
