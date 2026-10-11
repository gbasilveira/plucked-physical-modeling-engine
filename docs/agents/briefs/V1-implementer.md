# Brief: V1 Traceability and oracle suite — implementer

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/V1-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

keeps every requirement, test and SPEC oracle linked and visible, so a gate check is a report rather than an investigation.

You also own the **P0.3 follow-ups** (section "Carried follow-ups from P0.3" below): they sit in the test framework that V1 reads results from.

## Read first

- docs/plan/phasing-plan.md: **How to use this plan** (phase card format, orchestration rules 2 and 7), **Software guidelines** (Testing policy, CI), the phase card **V1 Traceability and oracle suite**, the **Gates** table (which ORC rows each gate needs) and **Agent briefs and handoff** (definition of done, handoff report fields).
- SPEC and PLAN sections:
  - SPEC-ORC — docs/plan/engine-specification.md, "Verification oracles"
- engine/tests/oracles.md (the oracle registry), docs/dev/testing.md (naming rule, labels, registry statuses), docs/dev/signal-analysis.md, docs/dev/ci.md, .github/workflows/pr.yml and nightly.yml, tools/ci/ (`junit_summary.py`, `required_checks.json`), tools/tests/test_p0_2_r7.py (every pr.yml job must be a required check and listed in CONTRIBUTING.md).
- docs/status/phase-status.md — which phases are `complete` (read only).
- docs/agents/report.schema.json — the handoff report format.

## Inputs

- Completed dependencies and their outputs: **P0.3** complete (head 15a1a43; pr.yml run https://github.com/gbasilveira/plucked-physical-modeling-engine/actions/runs/38147326045 and nightly 38147728728 green): test naming rule and `test_p0_3_r1_test_names_carry_ids`, `pme_add_doctest` (one CTest test per `TEST_CASE`), JUnit output from `ctest --output-junit` and `pytest --junitxml` uploaded as `junit-*` artifacts by the build, python and coverage jobs, the oracle registry. G0 passed (docs/gates/G0.md).
- Frozen interfaces: none that you use.
- Completed phases today: P0.1, P0.2, P0.3, P0.4 (V1-T2 must hold for them).

## Concurrent work: P0.5, V1, A1 and D1 run at the same time

Four phases are implemented in separate git worktrees; the orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. Edits must not overlap. This table is identical in every brief of the four phases.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/osc/**`, `engine/tests/osc/**`, `docs/dev/osc.md` | A1 implementer | read only |
| In `engine/tests/interfaces/test_stubs.cpp`: the `SUBCASE("compileOscillator")` block only. In `docs/dev/interfaces.md`: the word `osc` in the first sentence of "Stubs (P0.4-R3)" only | A1 implementer | read only |
| `engine/tests/oracles/**` (new); in `engine/tests/oracles.md`: the status and test-location cells of rows ORC-01 and ORC-02 only | A1 test-author | read only |
| `reference/**`, `tools/reference-cli/**`, `tools/tests/test_p0_5_*.py`, `docs/dev/reference-solver.md` | P0.5 | read only |
| `pyproject.toml`, `uv.lock` | P0.5 | read only |
| `tools/trace/**`, `tools/tests/test_v1_*.py`, `docs/dev/traceability.md`, `.github/workflows/nightly.yml`, `tools/ci/**` except `required_checks.json` | V1 | read only |
| P0.3 follow-ups: `cmake/pme_testing.cmake`, `engine/tests/framework/**`, `engine/tests/rtcheck/CMakeLists.txt`, `tools/analysis/**`, `tools/bench/**`, `tools/tests/test_p0_3_*.py`, `docs/dev/testing.md`, `docs/dev/signal-analysis.md` | V1 | read only |
| `# Covers:` / `// Covers:` comment lines added to test files of completed phases P0.1 to P0.3 (comments only) | V1 | read only |
| `plugin/**` (`plugin/CMakeLists.txt`, `plugin/clap/`, `plugin/common/`), `docs/dev/clap-shell.md` | D1 | read only |
| `cmake/dependencies.cmake`, `docs/dev/build.md` | D1 | read only |
| `.github/workflows/pr.yml` | V1 owns every existing job; V1, D1 and P0.5 each **append one new job in a delimited block at the end of the file** (`traceability`, `plugin`, `reference-python`) | nobody else edits existing jobs |
| `tools/ci/required_checks.json` | each phase that adds a pr.yml job appends its own matrix-expanded job names at the end of the array | — |
| `CONTRIBUTING.md` | each phase that adds a pr.yml job appends its own rows at the end of the required-checks table | — |
| `docs/dev/ci.md` | V1; D1 and P0.5 each append one delimited subsection at the end of the file describing their job | — |
| Root `CMakeLists.txt` | nobody above the current last line; A1 implementer, A1 test-author, P0.5 and D1 each **append one delimited block at the end** | — |
| `CMakePresets.json`, `cmake/pme_module.cmake`, `cmake/compiler_options.cmake`, `cmake/sanitizers.cmake`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.github/workflows/release.yml`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody | read only |
| `engine/include/pme/**`, the other `engine/src/*/` stubs, `engine/tests/interfaces/**` (except the A1 row above), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2; changes only by ICR) | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

When two phases append at the same place (end of the root `CMakeLists.txt`, end of pr.yml, end of a JSON array or table), the orchestrator keeps both blocks when merging. Keep your blocks self-contained so that order does not matter.

ICR-0001 (interfaces-v2) may still be in review while you work: never edit `engine/include/pme/**`, `engine/tests/interfaces/**`, `tools/params/**`, `params.json`, `docs/dev/interfaces.md` or `engine/tests/oracles.md`, not even comments.

## Scope

- May edit (create or change): the paths the ownership table gives **V1**: `tools/trace/**`, `tools/tests/test_v1_*.py`, `.github/workflows/nightly.yml`, `tools/ci/**` (except that `required_checks.json` takes only your own appended entry), every existing job of `.github/workflows/pr.yml` plus your appended `traceability` job, `docs/dev/ci.md`, your row appended to the CONTRIBUTING.md required-checks table, and the P0.3 follow-up paths; plus comment-only `Covers:` lines in test files of P0.1 to P0.3.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. V1-R1 A script extracts requirement and test IDs from this plan, the code and test results, and builds a traceability matrix.
2. V1-R2 The matrix shows, per phase, requirements without tests and tests that fail or are missing.
3. V1-R3 Every ORC row is implemented and passing by the gate that needs it (ORC-01 to 13 and 16 to 19 by G2; ORC-14 and 15 by G6).
4. V1-R4 The matrix is published as a CI artifact on every pull request and nightly.

### How to meet each requirement

**V1-R1 — extraction.** Python 3.12, standard library only (no new dependency; `pyproject.toml` is P0.5's), ruff and mypy strict clean, under `tools/trace/` (package `trace_matrix` or similar; avoid the stdlib name `trace`). Entry point `python3 tools/trace/build_matrix.py --junit <dir> [--job-results <json>] --out <dir>` (exact flags are yours; document them).

- **Plan IDs** from docs/plan/phasing-plan.md: per phase card (`### <ID> <name>`), every requirement ID `<ID>-R<n>` with its text, every test ID `<ID>-T<n>` with its criterion, and every ORC ID the card's Tests line names. Gate rows give the ORC IDs each gate needs (use the Gates table and `gates.*.oracles` in docs/plan/phases.yaml if you parse YAML without a dependency; otherwise the table only). Fail loudly on a malformed card instead of skipping it.
- **Code IDs**: every test name in tracked C++ test sources (`TEST_CASE("…")`) and Python tests (`def test_…` plus docstring) under `engine/`, `tools/`, `reference/`, `compiler/`, `plugin/`, using the P0.3-R1 rule (`^((P0\.[1-5]|[ABCPDEVH][0-9]+)-[RT][0-9]+|ORC-[0-9]{2}) `; Python names lower-cased). Also `Covers:` annotations (below).
- **Results**: JUnit XML from ctest and pytest (all `junit-*` artifacts of the run), mapped back to IDs by name prefix. A test ID **passes** when at least one test carries it and every test carrying it passed in every job where it ran, and it ran on all three systems (Windows, macOS, Linux; derive the system from the artifact or job name). It **fails** if any carrying test failed; it is **missing** if no test carries it; it is **not run on all systems** otherwise (report as a gap).
- **Covers annotations**: a requirement is covered by a test whose name begins with the requirement ID, or by a test or CI step annotated `Covers: <ID>[, <ID>…]` in a comment on the line above the `TEST_CASE`/`def test_` or on a workflow step (`# Covers: P0.2-R4` above `- name:`). CI-step coverage takes its result from the job conclusion (pass `toJSON(needs)` from the workflow to the script). Document the convention in docs/dev/testing.md (yours).

**V1-R2 — matrix.** Output `traceability.html` (self-contained, no external assets, readable without JavaScript) and `traceability.json` (machine-readable, `schema_version: 1`) with, per phase: each requirement (covered by which tests or steps, or **uncovered**), each test ID (pass, fail, missing, not run on all systems, with the job names), and each ORC row the card names. A summary table per phase and per gate counts gaps. A "gap" is: an uncovered requirement, a failing or missing test ID, a test ID not run on all three systems, or an ORC row required by a passed gate that is not passing. Phase status (`complete`, `in progress`, …) is read from docs/status/phase-status.md.

**V1-R3 — oracles.** Read engine/tests/oracles.md (ID, owning phase, gate, status, test location). For each ORC row show: registry status, whether tests named `ORC-xx` exist and pass on all three systems, and inconsistencies (registry says `passing` but CI does not; CI passes but registry says `not implemented`). The gate summary lists, for G1, G2 and G6, the ORC rows still missing. The script never edits oracles.md.

**V1-R4 — CI.**

- **pr.yml:** append one job at the end of the file, in a delimited block:

  ```yaml
  # ---- V1: traceability ----
    traceability:
      name: traceability
      needs: [lint, build, python, coverage, deps]
      if: always()
      …
  # ---- end V1 ----
  ```

  It downloads every `junit-*` artifact of the run, runs the script, uploads `traceability.html` and `traceability.json` as the artifact `traceability` (retention as P0.2-R6 set for reports), writes the per-phase summary to `$GITHUB_STEP_SUMMARY`, and **fails when a phase whose status is `complete` has a gap** that is not in the allow-list `tools/trace/known-gaps.json` (each entry: ID, phase, reason, owner). Add `traceability` to `tools/ci/required_checks.json` (append at the end of the array) and a row to the CONTRIBUTING.md required-checks table (append after the last row), or `test_p0_2_r7` fails.
- D1 and P0.5 append their own jobs (`plugin (linux|macos|windows)`, `reference-python (linux|macos|windows)`) concurrently. You cannot list them in `needs` (they do not exist in your base, and an unknown `needs` entry makes the workflow invalid). The orchestrator adds `plugin` and `reference-python` to your `needs` when it merges those phases; say so in a comment inside your block.
- **nightly.yml:** a final job that does the same over the nightly run's `junit-*` artifacts and uploads `traceability` (it reports gaps but does not fail the nightly run).
- docs/dev/ci.md: describe both jobs, the artifact, the allow-list and how a gate report uses the matrix.

## Carried follow-ups from P0.3 (you own them)

The P0.3 reviewer approved P0.3 with these non-blocking items (docs/status/phase-status.md, row P0.3). Fix each, with a test where it is code. Commit them separately from the V1 work, each message starting `V1: P0.3 follow-up — `.

1. **Default ctest timeout.** `pme_add_doctest` (cmake/pme_testing.cmake) sets no TIMEOUT, so the rtcheck death tests lost the 120 s limit they had before they moved onto the helper. Add an optional `TIMEOUT <seconds>` argument with a default of 300 s applied to every discovered test (the discovery script writes `TIMEOUT` into `set_tests_properties`), and pass `TIMEOUT 120` in engine/tests/rtcheck/CMakeLists.txt for both rtcheck executables. Show `ctest -N --show-only=json-v1` lists the timeouts. Document it in docs/dev/testing.md. Do not edit engine/tests/interfaces/ (it inherits the default).
2. **Flaky timing test.** `TEST_CASE("P0.3-T3 harness measures a slower function as slower")` (tools/bench/tests/bench_tests.cpp) compares medians of 10 repetitions and can flake on a loaded runner. Compare `min_ns` (the minimum is the most stable estimator for "slower") and, if the first comparison fails, rerun both up to two more times before failing; keep the 1.5× threshold for a 3× workload. Keep the test name.
3. **Golden comparator ignores `knee_s`.** In the C++ comparator (tools/analysis/src/golden.cpp) and the Python mirror (tools/analysis/pme_analysis/golden.py), compare `second_decay.knee_s` when both files have a second decay, with a new default tolerance `knee_relative` = 5% (the planner's choice; no SPEC-ORC row sets it; the knee of a two-slope fit is poorly conditioned), overridable per golden file and per partial like the others. Add it to `golden-features.schema.json` (`schema_version` stays 1: the field is optional with a default) and to docs/dev/signal-analysis.md. Tests: a +10% knee change fails naming the partial and `knee_s`; +2% passes; C++ and Python agree.
4. **Energy tolerance default.** The golden default `energy_absolute = 1e-9` is looser than ORC-02 (10⁻¹² relative in float64, 10⁻⁵ in float32) and ORC-09 (10⁻¹⁰ relative). Do not change the default (it applies to golden renders, not oracles); state in docs/dev/signal-analysis.md and docs/dev/testing.md that oracle tests must pass the SPEC-ORC tolerance explicitly to `account_energy` or the comparator and never rely on golden defaults. (The A1 test-author brief already requires this for ORC-02; A5's brief will for ORC-09.)
5. **WAV cross-language test.** No committed test proves a WAV written by the C++ library reads identically in Python and vice versa. Add a small C++ tool in tools/analysis (for example `pme_wav_roundtrip write|read <file> …`) and `tools/tests/test_p0_3_r5_cross_language.py` that, for 16-, 24-, 32-bit integer and 32-, 64-bit float with 1 and 3 channels, writes in C++ and reads in Python (sample-exact for float formats; exact integer codes for integer formats) and the reverse. The tool path comes from the environment variable `PME_WAV_TOOL`; the test is skipped without it. In pr.yml's existing `build` job, add one step after "P0.3-T3 bench smoke" that sets `PME_WAV_TOOL` to the built tool and runs this test with `-rs`, so it runs on all three systems; a skip there must fail the step.
6. **Docstring.** tools/analysis/pme_analysis/__init__.py says "NumPy and SciPy only"; the package imports NumPy only. Fix the docstring (and docs/dev/signal-analysis.md if it says the same).

## Tests that must pass

- **V1-T1** a deliberately removed test shows up as a gap;
- **V1-T2** the matrix for the main branch has no gaps for completed phases.

### How each test is built

**V1-T1** (`tools/tests/test_v1_t1.py`, unit): build the matrix from fixture inputs (a small plan excerpt, a few test sources and JUnit files under `tools/trace/testdata/`), then remove one test (from the sources and from the JUnit) and rebuild: the matrix reports that test ID as missing and the phase as having one gap. Also: a failing JUnit case shows as fail; a test that ran only on Linux shows as not run on all systems; an uncovered requirement shows as a gap; an ORC row required by a passed gate and not passing shows as a gap.

**V1-T2**: the `traceability` job on the merged branch (this repository has no commits on `main` yet; `ccr-5ef35d73-oqb6fd` stands in for it, see docs/gates/G0.md) shows no gaps for P0.1, P0.2, P0.3 and P0.4, with `tools/trace/known-gaps.json` **empty**. Before you finish, run the script locally on JUnit you produce (`ctest --preset linux-debug --output-junit …`, `uv run pytest --junitxml …`) and list every gap of the completed phases. Close gaps that are only missing links by adding `Covers:` comments where an existing test or CI step genuinely verifies the requirement (comment-only edits, test files of P0.1 to P0.3 and the workflows; never in ICR-0001 paths). For a gap that needs a new test or a change outside your paths (for example a P0.4 requirement whose test sits in engine/tests/interfaces/), add an allow-list entry with the owning phase and reason, and list it in your report: V1-T2 is then `not met` until the orchestrator routes it. Never mark a requirement covered by a test that does not check it.

Locally you cannot reproduce "all three systems"; run with a flag that treats the local system as sufficient and say so in `notes`.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to V1:

- Module note: docs/dev/traceability.md (new, yours) — SPEC-ORC is the section served; the ID extraction rules, gap definitions, `Covers:` convention, allow-list policy, CLI and artifacts.
- No engine/src code: RT-safety, coverage and performance baselines do not apply. Your C++ WAV tool and the changed bench test must pass in `linux-debug` and `linux-rt-safety`.
- V1 is continuous (phases.yaml `continuous: true`): this brief delivers the tool, CI publication and the P0.3 follow-ups; later phases' IDs appear in the matrix without further V1 work.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `V1: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths.
- Run before you finish: `uv run ruff check`, `uv run ruff format --check`, `uv run mypy`, `uv run python tools/check_spdx.py`, `uv run pytest`; `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on changed C++; `ctest --preset linux-debug` and `linux-rt-safety`; a YAML lint of both workflows (`python3 -c "import yaml…"` via `uv run --with pyyaml`, or `actionlint` if available).
- If something cannot be done as written, stop that item, do not weaken a check to make it pass, and describe it in the report.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run`, set `status` to `partial`, and put the local evidence in `notes`: commands, the local matrix summary for P0.1 to P0.4 (gap count per phase, each allow-listed gap), and the follow-up results.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `V1`, `role` `implementer`, `model` `sonnet`; `requirements` V1-R1 to V1-R4; `tests` V1-T1, V1-T2; every created or changed path in `outputs`; `pull_requests` empty; the six follow-ups with their commits in `notes`; remaining gaps in `open_risks`.
