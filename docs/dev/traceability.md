# Traceability matrix (V1)

SPEC sections served: SPEC-ORC (every oracle row is tracked from registry to CI result). The tool
implements no engine behaviour. Code: `tools/trace/` (Python 3.12, standard library only; package
`trace_matrix`, entry point `build_matrix.py`). Tests: `tools/tests/test_v1_*.py`, fixtures in
`tools/trace/testdata/fixture/`.

The matrix links every requirement (`A2-R1`), test (`A2-T1`) and oracle (`ORC-01`) of the phasing
plan to the tests that carry its ID and to their results, so that a gate check is a report and not
an investigation (orchestration rule 7).

## What it reads

| Input | Default | What is taken from it |
| --- | --- | --- |
| Phasing plan | `docs/plan/phasing-plan.md` | Per phase card (`### <ID> <name>`): each requirement `<ID>-R<n>` with its text, each test `<ID>-T<n>` with its criterion, each ORC row the Tests line names; the Gates table (required phases, ORC rows per gate). A malformed card (missing field, numbering out of sequence, unparsable Tests item) stops the run with an error naming the card. |
| Status | `docs/status/phase-status.md` | Phase status (`complete`, `in progress`, ...) and gate status (`passed`, `not passed`). |
| Oracle registry | `engine/tests/oracles.md` | ID, owning phase, gate, registry status, test location. Never edited by the script. |
| Test sources | `engine/ tools/ reference/ compiler/ plugin/ bindings/` | C++ `TEST_CASE("...")`, CMake `add_test(NAME "...")`, Python `def test_...` (under a `tests/` directory) with docstring; `Covers:` annotations. Directories named `build`, `testdata`, `.venv`, `_deps` ... are skipped. |
| Workflows | `.github/workflows/*.yml` | `# Covers:` comments above steps and jobs. |
| Results | `--junit <dir>` | JUnit XML from `ctest --output-junit` and `pytest --junitxml`, one sub-directory per workflow artifact (`junit-build-linux-gcc/`, ...). |
| Job conclusions | `--job-results <json>` | `toJSON(needs)` of the workflow, to verify annotated CI steps. |
| Allow-list | `--known-gaps tools/trace/known-gaps.json` | Gaps accepted for now (see below). |

### How IDs are found

- **C++ and ctest names** begin with the ID and a space (P0.3-R1 rule, docs/dev/testing.md).
- **Python tests**: the ID the docstring begins with, else the one the lower-cased function name
  encodes (`test_p0_3_r1_x` is `P0.3-R1`, `test_orc_01_x` is `ORC-01`). pytest parameters
  (`[fmt-1]`) are stripped when results are matched to sources, so a parametrised test is one test.
- **`Covers:` annotations** add IDs to a test or workflow step that does not carry them by name.
  Write the comment on the line(s) directly above the `TEST_CASE(`, `def test_`, `add_test(` or
  the workflow `- name:` line (a contiguous comment block; Python decorators between the comment and
  the `def` are fine), or above a workflow job key to cover the whole job:

  ```cpp
  // Covers: P0.2-R4, P0.2-R5
  TEST_CASE("P0.2-T1 allocation inside ScopedRealtimeThread aborts") { ... }
  ```

  ```yaml
      # Covers: P0.2-R4
      - name: clang-format
  ```

  Only annotate a requirement that the test or step genuinely verifies: the annotation is a claim a
  reviewer checks, and the matrix cannot.
- **Results** are mapped back to IDs through the source (so `Covers:` applies to results too) or,
  for a test known only from results, by its name. Tests without any ID are listed under "tests
  without an ID" in the JSON and ignored.
- The **system** of a result comes from the artifact directory name (`windows`, `macos`, `linux`
  anywhere in it), else from the file name, else `--default-system` (linux; the performance, fuzz
  and soak jobs run there).

## States and gaps

State of an ID (requirement, test or oracle):

| State | Meaning |
| --- | --- |
| `pass` | At least one test (or annotated step) carries the ID, no carrying result failed in any job, and a passing result exists on every required system. A step annotation counts through its job's conclusion; without job results it is shown as *unverified* and counted as covered. |
| `fail` | A carrying test or step failed (the jobs are named). |
| `missing` | Nothing carries the ID (shown as `UNCOVERED` for a requirement). |
| `not_run_all_systems` | Something carries the ID but no passing result exists on some required system (skipped results do not count). |

A **gap** is: an uncovered requirement; a failing, missing or not-everywhere-run test ID (including
the ORC rows a card's Tests line names); an ORC row required by a passed gate that is not passing; or
a registry row that disagrees with CI (registry `passing` but CI not passing; CI passing but the
registry says `not implemented`; an ORC ctest test without the `oracle` label). Kinds in the JSON:
`requirement_uncovered|failing|not_run_all_systems`, `test_missing|failing|not_run_all_systems`,
`oracle_missing|failing|not_run_all_systems`, `oracle_gate`, `oracle_inconsistent`.

A gap is **enforced** when its phase has status `complete` (or, for `oracle_gate`, when the gate
has passed) and it is not on the allow-list. Gaps of phases that are not complete are listed, not
enforced. The Gates table gives the ORC rows per gate; the gate summary lists the rows still
missing for G1, G2 and G6 whatever the gate state.

## Command line

```bash
python3 tools/trace/build_matrix.py --junit <dir> --out <dir> \
    [--job-results <json> [--job-results-workflow pr.yml]] \
    [--known-gaps tools/trace/known-gaps.json] [--fail-on-gaps] \
    [--systems linux,macos,windows] [--default-system linux] \
    [--root .] [--plan ...] [--status ...] [--oracles ...] [--source-dir DIR ...] [--workflows DIR]
```

Writes `traceability.html` (self-contained: no script, no external asset, readable without
JavaScript), `traceability.json` (`schema_version` 1) and `summary.md` (the table for
`$GITHUB_STEP_SUMMARY`) into `--out`, and prints the summary. Exit codes: 0; 1 with
`--fail-on-gaps` when an enforced gap exists; 2 for an unreadable input or malformed plan.

**Local run.** One machine cannot produce results for three systems; pass `--systems linux`:

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug
cmake --preset linux-rt-safety && cmake --build --preset linux-rt-safety
mkdir -p /tmp/junit/junit-build-linux/
ctest --preset linux-debug -LE "perf|fuzz|soak" --output-junit /tmp/junit/junit-build-linux/ctest-debug.xml
ctest --preset linux-rt-safety -LE "perf|fuzz|soak" --output-junit /tmp/junit/junit-build-linux/ctest-rt.xml
mkdir -p /tmp/junit/junit-python-linux/ && uv run pytest --junitxml /tmp/junit/junit-python-linux/pytest.xml
python3 tools/trace/build_matrix.py --junit /tmp/junit --out /tmp/trace --systems linux \
    --known-gaps tools/trace/known-gaps.json
```

Without `--job-results`, annotated CI steps are unverified (covered, flagged).

## The `traceability` jobs

- **`pr.yml`, job `traceability`** (a required check): `needs` the jobs that upload `junit-*`
  artifacts, runs `if: always()`, downloads every `junit-*` artifact of the run, runs the script
  with the run's `toJSON(needs)`, writes the per-phase summary to the job summary, uploads
  `traceability.html` and `traceability.json` as the artifact `traceability` (14 days), and
  **fails when a `complete` phase has a gap that is not allow-listed**. When a phase adds a pr.yml job
  that uploads `junit-*` artifacts (`plugin`, `reference-python`), add the job to `needs`.
- **`nightly.yml`, job `traceability`**: the same over the nightly run's artifacts (30 days); it
  reports gaps but never fails the run.

## The allow-list

`tools/trace/known-gaps.json`:

```json
{ "schema_version": 1,
  "known_gaps": [ { "id": "P0.4-T2", "phase": "P0.4", "reason": "why", "owner": "who closes it" } ] }
```

An entry (optional `kind` narrows it) hides one enforced gap while it is routed to its owner. It is
for gaps that need a new test or a change outside V1's paths; a gap that is only a missing link is
closed with a `Covers:` annotation instead. The list is **empty** when every complete phase is
traced (V1-T2). Entries that match no gap are reported as stale and should be removed.

## How a gate report uses the matrix

The planner's gate report (docs/gates/G&lt;n&gt;.md) attaches the `traceability` artifact of the
run on the gate commit: the gate table shows the required phases not yet complete, the gaps in
them and, for G1, G2 and G6, the ORC rows still missing; a gate criterion of the form "V1 matrix has
no gaps for ..." is read off the per-phase rows.

## Requirements covered

| ID | Where |
| --- | --- |
| V1-R1 | `tools/trace/trace_matrix/{plan,sources,results}.py`; `tools/tests/test_v1_r1.py` |
| V1-R2 | `tools/trace/trace_matrix/{matrix,render}.py`; `tools/tests/test_v1_t1.py` |
| V1-R3 | `tools/trace/trace_matrix/matrix.py` (oracle rows, gate summary); `tools/tests/test_v1_r3.py` |
| V1-R4 | `traceability` jobs of `pr.yml` and `nightly.yml`; `tools/tests/test_v1_r4.py` |
| V1-T1, V1-T2 | `tools/tests/test_v1_t1.py`; `tools/tests/test_v1_t2.py` and the `traceability` job |
