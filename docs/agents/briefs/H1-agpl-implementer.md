# Brief: H1 Licensing, provenance and compliance, task H1-agpl (relicense to AGPL-3.0-or-later) — implementer

Status: **scheduled, not ready.** Start only when the orchestrator confirms that A5, A7, A8 and A9 have merged (the same window in which ICR-0002 is applied). A repository-wide header change conflicts with every in-flight worktree, so it must not start earlier. Coordinate with the ICR-0002 interface agent: whichever merges second rebases; never hand-merge a file without rerunning `tools/check_spdx.py`.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

makes the project legally publishable: licences, third-party notices, data provenance, trademarks and a prior-art review.

This task applies the owner's licence decision (D-1, 2026-10-12: AGPL-3.0-or-later; docs/adr/ADR-licence.md) to the repository. It covers H1-R1 and H1-R2; the rest of H1 stays open.

## Read first

- docs/adr/ADR-licence.md; docs/plan/decisions/D-1-licence-analysis.md (sections 4 and 8); docs/plan/phasing-plan.md, Software guidelines "Licensing" and the card **H1**.
- `tools/check_spdx.py` and its tests; `pyproject.toml`; `cmake/dependencies.cmake`; `plugin/common/ids.hpp` and the plug-in descriptors (CLAP, LV2 `manifest.ttl` / `.ttl` files, any VST3/AU metadata); README.md; docs/user/; CONTRIBUTING.md; packaging/.

## Inputs

- Owner decision D-1 (licence): AGPL-3.0-or-later. D-3: brand Sympathon (product name open; identifiers are **not** part of this task).
- The repository at the merge point after A5, A7, A8, A9.

## Scope

- May edit: LICENSE; the SPDX line of every tracked project file (only that line; nothing else in those files); `tools/check_spdx.py` and its tests (`tools/tests/test_p0_1_t3*.py` or wherever they live; keep their names); `pyproject.toml` (`license` field only, plus classifiers if present); CMake or packaging metadata that states a licence; plug-in licence strings (LV2 `doap:license`, any "MIT" string in plug-in descriptors); README.md (licence section); docs/user/** (licence and network-use note); CONTRIBUTING.md (licence statement only; H1-R8's DCO is separate); a new `THIRD_PARTY_NOTICES` skeleton only if it does not exist (full H1-R4 is separate).
- Never edit: vendored or third-party files (they keep their own licences; `VENDORED` in `check_spdx.py`), CLAUDE.md, `.claude/`, docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/ (planner and orchestrator files; the planner updates the planning documents' licence statements separately).

## Requirements

1. H1-R1 Licence decision recorded as an ADR per decision D-1; LICENSE files match it.
2. H1-R2 SPDX headers on every source file, checked in CI.

### What to do

1. **LICENSE:** replace with the GNU AGPL version 3 text, verbatim and complete, from https://www.gnu.org/licenses/agpl-3.0.txt (download it; do not retype). Add no copyright line inside the licence text; put "Copyright (C) 2026 Guilherme Silveira and contributors. Licensed under the GNU Affero General Public License, version 3 or (at your option) any later version." in README.md. State in the report the SHA-256 of the downloaded file and of LICENSE (equal).
2. **Headers:** in every tracked file that `tools/check_spdx.py` requires to carry our header, change `SPDX-License-Identifier: MIT` to `SPDX-License-Identifier: AGPL-3.0-or-later` with a script (commit the script's command in the commit message, not the script). Do not touch vendored files (`cmake/CPM.cmake` and any others listed in `VENDORED`), third-party sources, generated files or test data.
3. **Checker:** `IDENTIFIER = "SPDX-License-Identifier: AGPL-3.0-or-later"`; the exact-match rule stays (so `AGPL-3.0-only`, `MIT` or `GPL-3.0-or-later` on a project file fail); vendored files keep their own expected text. Update its docstring and tests (a file with the old MIT header now fails). Also update the `.pmepreset` exemption comment only if D5's follow-up has already changed the extension; otherwise leave it.
4. **Metadata:** `pyproject.toml` `license = "AGPL-3.0-or-later"` (SPDX expression; add `license-files = ["LICENSE"]` if the build backend supports it), classifiers if present; any CMake `project()` or CPack licence field; LV2 `doap:license <https://spdx.org/licenses/AGPL-3.0-or-later>`; any plug-in descriptor or installer string that says MIT. List every file changed.
5. **Documentation:** README.md licence section (AGPL-3.0-or-later; third-party components keep their licences, see THIRD\_PARTY\_NOTICES; the Mac App Store is not a distribution channel); docs/user/ (a short "Licence" page or section). **Network use (AGPL section 13):** state that anyone who modifies the engine, compiler or renderer and lets users interact with it over a network (for example a web service that renders audio) must offer those users the corresponding source of the modified version.
6. **Dependency check:** list every dependency in `pyproject.toml`, `uv.lock` (direct dependencies) and `cmake/dependencies.cmake` with its licence, and mark any that is not in the table of docs/plan/decisions/D-1-licence-analysis.md section 4. Do not add or remove dependencies. Report anything incompatible (none is expected).

## Tests that must pass

- **H1-T2** no source file lacks an SPDX header — with the new identifier (`uv run python tools/check_spdx.py` exits 0; its tests pass).
- Every existing CI job stays green (the change is comment-only in sources).

## Definition of done

- [ ] H1-R1 and H1-R2 met as above; other H1 requirements untouched (H1 stays in progress).
- [ ] `tools/check_spdx.py` passes on the whole tree; a planted MIT header fails.
- [ ] No change other than the SPDX line in any source file (verify with `git diff --stat` and a diff filter that shows only SPDX lines changed).
- [ ] Handoff report submitted and consistent with CI results.

## Working rules

- Commit messages start `H1: ` and end with the attribution lines your session provides. Do not push, tag or open a pull request. Stage files by name or by the explicit list your script produced; never `git add -A`.
- Disk: no build needed except to run the test suites the CI jobs run; one build tree at a time.

## Report

Handoff report as JSON matching docs/agents/report.schema.json: `phase` `H1`, `role` `implementer`, `model` `sonnet`; `requirements` H1-R1, H1-R2 (others absent); `tests` H1-T2 (`not run` without CI, local evidence in `notes`); `outputs`: every changed path (or the count and the command for the header pass, plus the list of non-header files); `notes`: LICENSE SHA-256, the dependency table, any file the script skipped and why.
