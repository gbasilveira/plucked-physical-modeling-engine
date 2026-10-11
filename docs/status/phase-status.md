# Phase status

Maintained by the orchestrator (Opus). Status values: not started, ready, in progress, in review, complete, blocked, skipped. A phase is `ready` when every entry of its `depends_on` in [phases.yaml](../plan/phases.yaml) is complete, passed or answered.

## Phases

| Phase | Name | Track | Status | Branch / PR | Gate report | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| P0.1 | Repository and build skeleton | 0 | complete | branch `ccr-5ef35d73-oqb6fd`; no PR (single session branch) |  | Head 312a62f. Code reviewer approved, no blocking findings. CI: bootstrap-build run https://github.com/gbasilveira/plucked-physical-modeling-engine/actions/runs/38140277814 green on all 21 jobs. ADR-0004 (no Windows tsan preset) Accepted by the orchestrator as the recommended default; owner may reverse. Carried to P0.2: delete bootstrap-build.yml, exact SPDX match, drop the phases.yaml exemption, clang-cl entry, Linux-Clang sanitizers. Carried to P0.3: ctest `--no-tests=error`. Carried to P0.4: buildable `pme_engine`. |
| P0.2 | CI and quality gates | 0 | complete | branch `ccr-5ef35d73-oqb6fd`; no PR (single session branch) |  | Head 2daee85. Code reviewer approved, no blocking items. CI: pr.yml run https://github.com/gbasilveira/plucked-physical-modeling-engine/actions/runs/38144357509 green on all 12 jobs at 2daee85. P0.2-T2: run 38144566762 failed only at clang-format on the deliberately injected file (gate demonstrated). P0.2-T3: nightly run 38144943308 and release run 38144945130 at HEAD. Open risks: rtcheck intercepts only operator new and blockingCall on macOS/Windows (malloc/free and lock interposition Linux-only); setup-uv and apt clang-tidy-18 not pinned to exact versions. |
| P0.3 | Test and analysis framework | 0 | ready |  |  | Depends on P0.1 (complete). Briefs: docs/agents/briefs/P0.3-implementer.md, P0.3-code-reviewer.md; no test-author (card tests no ORC rows). ADR-0005 (nlohmann/json). |
| P0.4 | Interface skeleton | 0 | complete | branch `ccr-5ef35d73-oqb6fd`; no PR (single session branch) |  | Head 2daee85. Code reviewer approved, no blocking items. CI: pr.yml run 38144357509 green on all 12 jobs at 2daee85 (covers P0.4-T2 after P0.2). P0.4-R6: interfaces-v1 = 2daee85, tag push pending owner (tag created locally; this session may push only its branch). Interfaces are frozen at 2daee85; changes go through docs/icr/. Open risks: the P0.4-R3(a) allocation-count case is omitted under TSan builds (operator new not replaced under the TSan runtime); rtcheck coverage on macOS/Windows limited to operator new and blockingCall. SPEC gaps found in review: to be filed under docs/spec-issues/. |
| P0.5 | Reference solver | 0 | not started |  |  |  |
| A1 | Modal oscillator | A | not started |  |  |  |
| A2 | Stiff string compiler | A | not started |  |  |  |
| A3 | SAV framework and low-rank solve | A | not started |  |  |  |
| A4 | Bridge coupling | A | not started |  |  |  |
| A5 | Hammer and felt | A | not started |  |  |  |
| A6 | Dampers and pedals | A | not started |  |  |  |
| A7 | Geometric nonlinearity and longitudinal waves | A | not started |  |  |  |
| A8 | SIMD kernels | A | not started |  |  |  |
| B1 | Instrument description schema and loader | B | not started |  |  |  |
| B2 | Plate and body FEM compiler | B | not started |  |  |  |
| B3 | Radiation, mobility and local bridge builder | B | not started |  |  |  |
| B4 | Modal data import | B | not started |  |  |  |
| B5 | Parameter fitting toolkit | B | not started |  |  |  |
| C1 | Note group runtime | C | not started |  |  |  |
| C2 | Global body engine | C | not started |  |  |  |
| C3 | Sympathetic path and guard | C | not started |  |  |  |
| C4 | Event and parameter system | C | not started |  |  |  |
| C5 | Scheduler and threading | C | not started |  |  |  |
| C6 | Sleep, wake and state management | C | not started |  |  |  |
| C7 | Reference mode | C | not started |  |  |  |
| C8 | Offline render CLI and Python bindings | C | not started |  |  |  |
| P1 | Plucked exciters | P | not started |  |  |  |
| P2 | Distributed barriers | P | not started |  |  |  |
| P3 | Frets and stopping finger | P | not started |  |  |  |
| P4 | Jawari, thread and sawari | P | not started |  |  |  |
| P5 | Bending and time-varying tension | P | not started |  |  |  |
| P6 | Plucked-instrument integration | P | not started |  |  |  |
| D1 | CLAP plug-in shell | D | not started |  |  |  |
| D2 | VST3, AUv2 and standalone | D | not started |  |  |  |
| D3 | LV2 plug-in | D | not started |  |  |  |
| D4 | GUI | D | not started |  |  |  |
| D5 | Instrument and preset management | D | not started |  |  |  |
| D6 | Real-engine integration | D | not started |  |  |  |
| E1 | Concert grand piano | E | not started |  |  |  |
| E2 | Piano measurement and fitting | E | not started |  |  |  |
| E3 | Upright piano | E | not started |  |  |  |
| E4 | Harpsichord | E | not started |  |  |  |
| E5 | Guitars | E | not started |  |  |  |
| E6 | Tanpura | E | not started |  |  |  |
| E7 | Sitar | E | not started |  |  |  |
| E8 | Kantele, shamisen and tanbur | E | not started |  |  |  |
| V1 | Traceability and oracle suite | V | not started |  |  |  |
| V2 | Performance and real-time validation | V | not started |  |  |  |
| V3 | Sympathetic approximation validation | V | not started |  |  |  |
| V4 | Listening and playability tests | V | not started |  |  |  |
| V5 | Robustness - fuzzing and soak | V | not started |  |  |  |
| V6 | Host compatibility matrix | V | not started |  |  |  |
| H1 | Licensing, provenance and compliance | H | not started |  |  |  |
| H2 | Packaging and signing | H | not started |  |  |  |
| H3 | Documentation and website | H | not started |  |  |  |
| H4 | Release pipeline | H | not started |  |  |  |
| H5 | Public release 1.0 (pianos) | H | not started |  |  |  |
| H6 | Plucked-instrument releases (1.x) | H | not started |  |  |  |

## Gates

| Gate | Name | Status | Report |
| --- | --- | --- | --- |
| G0 | Foundations | not passed |  |
| G1 | Core numerics | not passed |  |
| G2 | Piano engine | not passed |  |
| G3 | Plug-in alpha | not passed |  |
| G4 | Piano content | not passed |  |
| G5 | Release candidate | not passed |  |
| G6 | Plucked numerics | not passed |  |
| G7 | Plucked instrument (one gate per instrument) | not passed |  |
