# Brief: V1 Traceability and oracle suite — code-reviewer

Model: sonnet
Branch: review the V1 commits on the branch the orchestrator names in its launch message (normally `ccr-5ef35d73-oqb6fd` after the merge); do not create a branch, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

keeps every requirement, test and SPEC oracle linked and visible, so a gate check is a report rather than an investigation.

Your job: decide whether the V1 implementer's work meets the V1 card, the Software guidelines and docs/agents/briefs/V1-implementer.md (including the six P0.3 follow-ups), and return a verdict. Gate reports will quote this matrix, so a matrix that reports a gap as covered is the worst possible defect: try to make it lie.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Testing policy, CI); the phase card **V1 Traceability and oracle suite**; the **Gates** table; **Agent briefs and handoff**.
- SPEC and PLAN sections:
  - SPEC-ORC — docs/plan/engine-specification.md, "Verification oracles"
- docs/agents/briefs/V1-implementer.md (ownership table, CI design, follow-ups, V1-T2 policy).
- docs/status/phase-status.md (row P0.3: the follow-ups' origin); engine/tests/oracles.md; docs/dev/testing.md.
- `.claude/agents/code-reviewer.md` — your checklist.
- The implementer's handoff report, as given in the orchestrator's launch message.

## Inputs

- Diff under review: commits whose messages start `V1: ` (`git log --grep '^V1: ' --name-only`).
- CI evidence: given by the orchestrator after merging (pr.yml run with the `traceability` job and its artifact). Without a link, a test is `not run`.
- Frozen interfaces: none used.

## Scope

- May edit: `tools/tests/test_v1_*.py` and fixtures under `tools/trace/testdata/` — only to add missing or independent tests for V1-T1, V1-T2 or V1-R1 to R4, and tests for the follow-ups under `tools/tests/test_p0_3_*.py`. Commit with messages starting `V1: `. Do not push.
- Read only: everything else. You MUST NOT change implementation, workflows, build files or configuration; describe needed changes as review items.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. V1-R1 A script extracts requirement and test IDs from this plan, the code and test results, and builds a traceability matrix.
2. V1-R2 The matrix shows, per phase, requirements without tests and tests that fail or are missing.
3. V1-R3 Every ORC row is implemented and passing by the gate that needs it (ORC-01 to 13 and 16 to 19 by G2; ORC-14 and 15 by G6).
4. V1-R4 The matrix is published as a CI artifact on every pull request and nightly.

## Tests that must pass

- **V1-T1** a deliberately removed test shows up as a gap;
- **V1-T2** the matrix for the main branch has no gaps for completed phases.

## Review checklist for V1

**Scope and process**

1. V1 commits touch only V1's paths in the ownership table: no edit to ICR-0001 paths (`engine/include/pme/**`, `engine/tests/interfaces/**`, `tools/params/**`, `params.json`, docs/dev/interfaces.md, engine/tests/oracles.md), `pyproject.toml`, `uv.lock` or `cmake/dependencies.cmake`. Edits to test files of P0.1 to P0.3 are `Covers:` comment lines only (`git diff -U0` shows nothing else). pr.yml: the existing jobs gained only the WAV cross-language step; the `traceability` job sits in a `# ---- V1 … ----` block at the end. `required_checks.json` and CONTRIBUTING.md gained `traceability` (run `uv run pytest tools/tests/test_p0_2_r7.py`). Messages start `V1: `.

**Extraction and matrix (R1, R2)**

2. Every phase card in docs/plan/phasing-plan.md is parsed: compare the script's count of requirement and test IDs with `grep -oE '\b(P0\.[1-5]|[ABCPDEVH][0-9]+)-[RT][0-9]+\b' docs/plan/phasing-plan.md | sort -u | wc -l` and explain any difference. A malformed card makes the script fail, not skip.
3. Adversarial cases in scratch (copy the inputs): (a) delete one `TEST_CASE` → missing; (b) a JUnit failure → fail; (c) a test present only in Linux JUnit → not run on all systems; (d) a test named `A1-T10 …` must not count for `A1-T1` (prefix boundary); (e) a Python test `test_a1_t1_x` maps to `A1-T1`; (f) a `Covers:` comment on an unrelated line does not count; (g) a requirement with no test → uncovered. Each must show as described.
4. The HTML is self-contained (no network fetches) and readable without JavaScript; the JSON has `schema_version: 1`.

**Oracles (R3)**

5. All 19 ORC rows appear with registry status, CI status and inconsistencies; the gate summary lists missing ORC rows for G1, G2 and G6 matching the Gates table. The script never writes oracles.md.

**CI (R4) and V1-T2**

6. pr.yml `traceability` job: `if: always()`, downloads every `junit-*` artifact, uploads `traceability` (HTML and JSON), writes the summary, fails only on gaps of `complete` phases not in `tools/trace/known-gaps.json`. nightly.yml has the matching job that does not fail the run. Validate both workflows' YAML.
7. Run the script on the CI JUnit of the merged head (or locally produced JUnit): list the gaps of P0.1 to P0.4. Every allow-list entry has an ID, phase, reason and owner, and is a real gap (check the test it claims is missing really is). Every `Covers:` annotation the implementer added points at a test or step that actually checks that requirement — read each one. V1-T2 is met only when the allow-list is empty.

**P0.3 follow-ups**

8. Timeout: `ctest --preset linux-debug -N --show-only=json-v1` shows TIMEOUT 120 on the rtcheck tests and 300 on others.
9. The timing test compares minima with up to three attempts; run it 20 times under load (`stress` or a parallel build) and record failures.
10. `knee_s` comparison in C++ and Python with the 5% default; schema updated; +10% fails, +2% passes in both languages.
11. Docs state that oracle tests pass SPEC-ORC tolerances explicitly; the golden energy default is unchanged.
12. WAV cross-language test covers all five formats and two channel counts, both directions, and runs in the build jobs of all three systems via `PME_WAV_TOOL` (a skip fails the step).
13. The `pme_analysis` docstring no longer names SciPy.

**Quality**

14. `ruff check`, `ruff format --check`, `mypy` (strict), `check_spdx.py`, `pytest` all pass; changed C++ is clang-format and clang-tidy clean; ctest passes in `linux-debug` and `linux-rt-safety` with GCC and Clang. docs/dev/traceability.md, docs/dev/testing.md, docs/dev/signal-analysis.md and docs/dev/ci.md describe the changes.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

For V1: no engine/src code (RT-safety, coverage and baselines do not apply). There is no pull request; the orchestrator records your verdict.

## Output

- Verdict: **approve**, or **request changes** with each blocking item tied to a checklist number and a file and line.
- Non-blocking suggestions separately, marked as such.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `V1`, `role` `code-reviewer`, `model` `sonnet`; `requirements` V1-R1 to V1-R4 as verified; `tests` V1-T1, V1-T2 with the CI run you observed (`ci_run`) or `not run`; `outputs` listing tests you added; `pull_requests` empty. Start `notes` with `Verdict: approve` or `Verdict: request changes`, then blocking items, then each checklist number with its command and result.
