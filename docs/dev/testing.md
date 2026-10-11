# Testing framework (P0.3)

SPEC sections served: SPEC-ORC (the verification oracles; this phase registers them but tests none).
Background: PLAN-VAL. The framework implements no engine behaviour.

This note describes the conventions every test follows, how to add and run a test, how oracle tests
update the registry, the golden-feature and benchmark workflows, and the isolation rule that keeps
test code out of the engine. The signal-analysis library is described in
[analysis.md](analysis.md).

## Naming

Every test name begins with a requirement, test or oracle ID.

- **C++ (doctest).** The `TEST_CASE` name begins with the ID, a space, then a description:

  ```cpp
  TEST_CASE("P0.2-T1 allocation inside ScopedRealtimeThread aborts") { ... }
  TEST_CASE("ORC-01 oscillator accuracy at 27.5 Hz") { ... }
  ```

  The ID matches `^((P0\.[1-5]|[ABCPDEVH][0-9]+)-[RT][0-9]+|ORC-[0-9]{2}) `. A name must not contain
  a comma, an asterisk, a question mark, a backslash or a double quote: doctest filters treat the
  first three specially and each test case becomes its own CTest test.
- **Python (pytest).** The function is `test_<id>_<what>` with the ID lower-cased and `.` and `-`
  replaced by `_` (`test_p0_2_t2_misformatted_file_fails`, `test_orc_01_oscillator_accuracy`), and its
  docstring begins with the ID (`"""P0.2-T2: a misformatted file fails."""`).
- **Enforcement.** `tools/tests/test_p0_3_r1.py::test_p0_3_r1_test_names_carry_ids` scans every C++
  test source under `engine/`, `tools/` and `reference/` and every Python test under
  `compiler/tests/` and `tools/tests/` (tracked files plus untracked files that are not ignored) and
  lists each name that breaks the convention.

## Labels

Every registered CTest test carries at least one label:

| Label | Meaning |
| --- | --- |
| `unit` | One function or class. |
| `oracle` | A SPEC-ORC row (named `ORC-xx`). |
| `integration` | An offline render of a defined scenario. |
| `golden` | Golden-feature comparison of a render. |
| `rt` | Must also pass under the `rt-safety` preset (audio-thread code). |
| `perf` | Benchmark; nightly, and on pull requests that touch `engine/src`. |
| `fuzz` | Event, parameter and state fuzzing; nightly. |
| `soak` | Long sustain soak; nightly and before releases. |

The pull-request workflow runs `ctest -LE "perf|fuzz|soak"`; the nightly workflow runs those three
labels in their own jobs. `pme_add_doctest` rejects a label outside this list at configure time.

## Where tests live

- Engine module tests: `engine/tests/<module>/` (for example `engine/tests/rtcheck/`).
- Oracle tests: `engine/tests/oracles/` (or next to the module they exercise), named `ORC-xx`.
- Tests of the analysis library and benchmark harness: `tools/analysis/tests/`, `tools/bench/tests/`.
- Python tests: `compiler/tests/` for the design compiler, `tools/tests/` for tools and analysis.
- The test framework itself: `engine/tests/framework/` (target `pme_test_framework`).

## Adding a C++ test

1. Write the test sources. They include `<pme/test/tolerance.hpp>` (or `<doctest/doctest.h>`) and
   contain only `TEST_CASE`s: the framework library supplies `main`.
2. Register them in the `CMakeLists.txt` next to them:

   ```cmake
   pme_add_doctest(pme_osc_tests
     SOURCES osc_tests.cpp
     LIBRARIES pme_osc
     LABELS unit)            # add RT for audio-thread code that must pass under rt-safety
   ```

   `pme_add_doctest(<target> SOURCES … [LIBRARIES …] LABELS … [RT])` builds the executable with
   `pme_build_options`, `COMPILE_WARNING_AS_ERROR ON`, `doctest::doctest` and `pme_test_framework`,
   then registers **each `TEST_CASE` as its own CTest test** by running the executable with
   `--list-test-cases` after the build. `LABELS` is required; `RT` adds the `rt` label.
3. A directory that adds tests is added to the build by its parent `CMakeLists.txt`; engine module
   test directories are the owning phase's responsibility.

### Tolerance helpers

`pme/test/tolerance.hpp` measures differences in the units SPEC-ORC uses and prints the actual
value, the expected value, the measured error and the tolerance when a check fails. Write the macros
as statements inside braces (they expand to a braced block).

| Macro | Error measured |
| --- | --- |
| `PME_CHECK_REL(actual, expected, tol)` | `abs(actual - expected) / abs(expected)` |
| `PME_CHECK_CENTS(actual, expected, tol)` | `abs(1200 log2(actual / expected))` |
| `PME_CHECK_DB(actual, expected, tol)` | `abs(20 log10(actual / expected))`, for amplitudes |
| `PME_CHECK_DB_VALUE(actual_db, expected_db, tol)` | `abs(actual_db - expected_db)`, for values already in dB |
| `PME_CHECK_SAMPLES(actual_s, expected_s, fs, tol)` | `abs(actual_s - expected_s) * fs`, durations compared in samples |

A NaN never satisfies a tolerance.

## Running tests

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --preset linux-debug                      # everything (--no-tests=error is set in the preset)
ctest --preset linux-debug -L unit              # one label
ctest --preset linux-debug -LE "perf|fuzz|soak" # what the pull-request workflow runs
ctest --preset linux-debug -R "P0.3-T2"         # one test or ID, by regular expression
./build/linux-debug/tools/analysis/pme_analysis_tests -tc="P0.3-T2*"   # one case, doctest filter
uv run pytest tools/tests/test_p0_3_t2.py       # Python tests
```

The three `os-*` test presets in `CMakePresets.json` set `execution.noTestsAction` to `error` and
print output on failure, so a filter that matches nothing fails instead of passing silently.

## Oracle registry

`engine/tests/oracles.md` has one row per SPEC-ORC row (`ORC-01` to `ORC-19`) with the SPEC setup,
expected value and tolerance (verbatim), the owning phase, the gate that requires it, a status and
the test location. Statuses: `not implemented`, `implemented` (the test exists but is not yet
passing or required), `passing` (registered with the `oracle` label and green in CI on all three
systems). The phase that implements an oracle edits its row (status and test location) in the same
pull request as the test and never edits the SPEC columns.

## Golden features

Golden references are JSON feature files, not audio. Format, tolerances and the comparison tools are
in [analysis.md](analysis.md). A golden-feature test measures the features of an offline render with
`pme_analysis`, then compares them with the committed reference using `pme_golden_compare` or the
library function `compare_golden`, and carries the label `golden`.

## Benchmarks

`tools/bench` provides the harness (`pme_bench`, alias `pme::bench`). A benchmark executable
registers functions with `PME_BENCHMARK(name, "unit", work_per_call) { … }` and its `main` returns
`pme::bench::run_main(argc, argv)`. The CMake helper registers it with the `perf` label:

```cmake
pme_add_benchmark(pme_osc_bench SOURCES osc_bench.cpp LIBRARIES pme_osc)
```

Command line: `--repetitions N` (default 11), `--warmup N` (default 3), `--min-time-ms X` (each
timed repetition runs enough calls to last at least this long, default 1 ms), `--filter TEXT`,
`--out FILE` (`-` for standard output), `--list`. The JSON (`schema_version` 1) holds `machine`
(OS, architecture, CPU model read without privileges, logical cores), `build` (compiler, build type,
preset), `settings` and `results`; each result has `name`, `unit`, `work_per_call`, `repetitions`,
`inner_iterations`, `median_ns`, `min_ns`, `iqr_ns` (per call, nanoseconds) and `throughput_per_s`.

**Regression rule.** `python3 tools/bench/compare.py <baseline.json> <current.json>` exits 1 and names
each benchmark whose current median exceeds the baseline median by more than 5% (current / baseline
strictly greater than 1.05), exits 0 otherwise, and exits 2 for unreadable or malformed input.
Benchmarks present in only one file are reported, not failed.

**Baselines** live in `tools/bench/baselines/<machine-id>.json`; none exist yet. They are produced on
a reference machine and compared only with runs on the same machine class (V2 and decision D-4 define
the reference machines): the comparison prints both files' CPU model and warns when they differ.

## Isolation from the engine

The analysis library (WAV and JSON), the test framework and the benchmark harness are test and tool
code. `cmake/pme_testing.cmake` checks, after all targets exist, that none of the eleven engine
module targets nor `pme_engine` reaches `pme_analysis`, `pme_test_framework`, `pme_bench` or
`nlohmann_json` through its link closure, and stops configuration with an "Engine isolation violated"
error naming the offender otherwise (P0.3-R5, ADR-0005).

## Requirements covered

| ID | Where |
| --- | --- |
| P0.3-R1 | `cmake/pme_testing.cmake`, `engine/tests/framework/`, `tools/tests/test_p0_3_r1.py` |
| P0.3-R2 | `engine/tests/oracles.md` |
| P0.3-R3, R4, R5 | `tools/analysis/` ([analysis.md](analysis.md)) |
| P0.3-R6 | `tools/bench/`, `cmake/pme_testing.cmake` (`pme_add_benchmark`) |
