# Phase status

Maintained by the orchestrator (Opus). Status values: not started, ready, in progress, in review, complete, blocked, skipped. A phase is `ready` when every entry of its `depends_on` in [phases.yaml](../plan/phases.yaml) is complete, passed or answered.

## Phases

| Phase | Name | Track | Status | Branch / PR | Gate report | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| P0.1 | Repository and build skeleton | 0 | complete | branch `ccr-5ef35d73-oqb6fd`; no PR (single session branch) |  | Head 312a62f. Code reviewer approved, no blocking findings. CI: bootstrap-build run https://github.com/gbasilveira/plucked-physical-modeling-engine/actions/runs/38140277814 green on all 21 jobs. ADR-0004 (no Windows tsan preset) Accepted by the orchestrator as the recommended default; owner may reverse. Carried to P0.2: delete bootstrap-build.yml, exact SPDX match, drop the phases.yaml exemption, clang-cl entry, Linux-Clang sanitizers. Carried to P0.3: ctest `--no-tests=error`. Carried to P0.4: buildable `pme_engine`. |
| P0.2 | CI and quality gates | 0 | complete | branch `ccr-5ef35d73-oqb6fd`; no PR (single session branch) |  | Head 2daee85. Code reviewer approved, no blocking items. CI: pr.yml run https://github.com/gbasilveira/plucked-physical-modeling-engine/actions/runs/38144357509 green on all 12 jobs at 2daee85. P0.2-T2: run 38144566762 failed only at clang-format on the deliberately injected file (gate demonstrated). P0.2-T3: nightly run 38144943308 and release run 38144945130 at HEAD. Open risks: rtcheck intercepts only operator new and blockingCall on macOS/Windows (malloc/free and lock interposition Linux-only); setup-uv and apt clang-tidy-18 not pinned to exact versions. |
| P0.3 | Test and analysis framework | 0 | complete | branch `ccr-5ef35d73-oqb6fd`; no PR (single session branch) |  | Head 15a1a43. Code reviewer approved, no blocking items; reviewer added `tools/tests/test_p0_3_review_t1.py` (5f052c0, independent T1 signals: 12 stiff partials, 1.2 Hz pair). CI: pr.yml run https://github.com/gbasilveira/plucked-physical-modeling-engine/actions/runs/38147326045 green on all 12 jobs at 15a1a43; nightly run 38147728728 green on all 12 jobs at 15a1a43. ADR-0005 (nlohmann/json v3.12.0). Deviations accepted by the orchestrator: (1) `CMakePresets.json` restructured beyond the briefed `execution` blocks (noTestsAction and outputOnFailure moved into the hidden `os-*` test presets, per-preset copies removed); (2) module note is `docs/dev/signal-analysis.md`, not `docs/dev/analysis.md` (P0.2, P0.3, P0.4 briefs amended). Open follow-ups (owner V1, see docs/agents/briefs/V1-implementer.md, unless noted): `pme_add_doctest` has no default TIMEOUT, so the rtcheck death tests lost their 120 s timeout; `P0.3-T3 harness measures a slower function as slower` may flake on loaded runners (compare minima or retry); the golden comparator ignores `knee_s`; the golden energy tolerance default 1e-9 is looser than ORC-02 (1e-12 float64, 1e-5 float32) and ORC-09 (1e-10): oracle tests must pass explicit tolerances (A1 test-author for ORC-02; A5 for ORC-09); no committed C++/Python WAV cross-language test; the `pme_analysis` package docstring says SciPy (it uses NumPy only). |
| P0.4 | Interface skeleton | 0 | complete | branch `ccr-5ef35d73-oqb6fd`; no PR (single session branch) |  | Head 2daee85. Code reviewer approved, no blocking items. CI: pr.yml run 38144357509 green on all 12 jobs at 2daee85 (covers P0.4-T2 after P0.2). P0.4-R6: interfaces-v1 = 2daee85, tag push pending owner (tag created locally; this session may push only its branch). Interfaces are frozen at 2daee85; changes go through docs/icr/. Open risks: the P0.4-R3(a) allocation-count case is omitted under TSan builds (operator new not replaced under the TSan runtime); rtcheck coverage on macOS/Windows limited to operator new and blockingCall. SPEC gaps found in review filed as SI-0001 to SI-0007 (docs/spec-issues/): SI-0001, SI-0002 resolved in SPEC; SI-0003 to SI-0007 accepted by the orchestrator on 2026-10-11 and routed through ICR-0001 (interfaces-v2, see Interface change requests below). |
| P0.5 | Reference solver | 0 | ready |  |  | Depends on P0.3 (complete). Briefs: docs/agents/briefs/P0.5-implementer.md, P0.5-code-reviewer.md; no test-author (tests compare with analytic values, no ORC rows). Start from a commit containing the ICR-0001 application (description structs at interfaces-v2). Python bindings need D1's first commit (nanobind gate in cmake/dependencies.cmake): merge that before P0.5's `reference-python` job. Reference accepts b₂ = 0 only (no local FD form for b₂√ω); later comparisons use b₂ = 0. |
| A1 | Modal oscillator | A | ready |  |  | Depends on G0 (passed). Briefs: docs/agents/briefs/A1-implementer.md, A1-test-author.md (ORC-01, ORC-02), A1-code-reviewer.md. Start from a commit containing the ICR-0001 application (interfaces-v2). SI-0008 (SPEC-OSC ω̂²Δt² evaluation) resolved 2026-10-11: accepted by the orchestrator as default under the owner's proceed instruction, flagged for owner review; SPEC-OSC now prints the sech form; no longer blocks A1 merge. Runs concurrently with P0.5, V1, D1 under the shared ownership table in the briefs. |
| A2 | Stiff string compiler | A | not started |  |  |  |
| A3 | SAV framework and low-rank solve | A | not started |  |  |  |
| A4 | Bridge coupling | A | not started |  |  |  |
| A5 | Hammer and felt | A | not started |  |  |  |
| A6 | Dampers and pedals | A | not started |  |  |  |
| A7 | Geometric nonlinearity and longitudinal waves | A | not started |  |  |  |
| A8 | SIMD kernels | A | not started |  |  |  |
| B1 | Instrument description schema and loader | B | not started |  |  | Waits for ICR-0001 to be applied and interfaces-v2 tagged (B1-R1 mirrors the v2 description structs); also depends on G0. |
| B2 | Plate and body FEM compiler | B | not started |  |  |  |
| B3 | Radiation, mobility and local bridge builder | B | not started |  |  |  |
| B4 | Modal data import | B | not started |  |  |  |
| B5 | Parameter fitting toolkit | B | not started |  |  |  |
| C1 | Note group runtime | C | not started |  |  | Do not brief before ICR-0001 is applied and interfaces-v2 tagged (SI-0006 instrument lifetime, SI-0004 tuning ownership). Owns ORC-16. |
| C2 | Global body engine | C | not started |  |  |  |
| C3 | Sympathetic path and guard | C | not started |  |  |  |
| C4 | Event and parameter system | C | not started |  |  | Do not brief before ICR-0001 is applied and interfaces-v2 tagged (SI-0006, SI-0005 Pluck semantics, SI-0007 registry). |
| C5 | Scheduler and threading | C | not started |  |  |  |
| C6 | Sleep, wake and state management | C | not started |  |  | Do not brief before ICR-0001 is applied and interfaces-v2 tagged (SI-0006). |
| C7 | Reference mode | C | not started |  |  |  |
| C8 | Offline render CLI and Python bindings | C | not started |  |  |  |
| P1 | Plucked exciters | P | not started |  |  |  |
| P2 | Distributed barriers | P | not started |  |  |  |
| P3 | Frets and stopping finger | P | not started |  |  |  |
| P4 | Jawari, thread and sawari | P | not started |  |  |  |
| P5 | Bending and time-varying tension | P | not started |  |  |  |
| P6 | Plucked-instrument integration | P | not started |  |  |  |
| D1 | CLAP plug-in shell | D | ready |  |  | Depends on G0 (passed). Briefs: docs/agents/briefs/D1-implementer.md, D1-code-reviewer.md; no test-author. Start from a commit containing the ICR-0001 application (interfaces-v2). D1's first commit (cmake/dependencies.cmake: PME_BUILD_PLUGINS, PME_BUILD_PYTHON_BINDINGS) should be merged early; P0.5's Python job needs it. D1-R6 engine routing deferred to D6 by ADR-0007 (Accepted 2026-10-11 by the orchestrator as default under the owner's proceed instruction; owner may reverse; D6 card notes the inherited clause). Plug-in identifiers provisional until D-3. |
| D2 | VST3, AUv2 and standalone | D | not started |  |  | Waits for D1 and decision D-3 (product name and plug-in identifiers; open). |
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
| V1 | Traceability and oracle suite | V | ready |  |  | Depends on P0.3 (complete); continuous. Briefs: docs/agents/briefs/V1-implementer.md, V1-code-reviewer.md; no test-author (no ORC rows). Also owns the six P0.3 follow-ups (ctest TIMEOUT default, timing-test flake, knee_s comparison, energy-tolerance documentation, WAV cross-language test, docstring). May start before ICR-0001 is merged (touches none of its paths). When merging D1 and P0.5, the orchestrator adds `plugin` and `reference-python` to the `needs` of V1's `traceability` job. |
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

## Interface change requests

| ICR | Interfaces | Status | Briefs | Applied at | Tag | Waiting phases |
| --- | --- | --- | --- | --- | --- | --- |
| [ICR-0001](../icr/ICR-0001-interfaces-v2.md) | interfaces-v1 (2daee85) → interfaces-v2 | approved by the orchestrator 2026-10-11 (owner's proceed instruction); SPEC updated in c5cec38; not yet applied | docs/agents/briefs/ICR-0001-implementer.md, ICR-0001-code-reviewer.md | pending | interfaces-v2: pending (orchestrator tags the reviewed head; owner pushes; SHA recorded here and in the ICR) | B1, C1, C4, C6 (also P1, B2, B3, C8, D1, D6 start on v2). Apply after the in-flight CI fix in CMakeLists.txt, engine/tests/** and docs/dev/** lands. |

Flagged for owner review (orchestrator defaults under the proceed instruction, listed in ICR-0001 "Decision"): plucked-exciter strength map (γ\_p = 1), finger structure, initial-displacement semantics, NoteOn launch of plucked exciters, event-time φ̄ evaluation (SI-0005); plate geometry inputs and f\_split = 2 kHz provisional (SI-0003); MIDI key numbering and f₁ computed by compileInstrument (SI-0004).

## Gates

| Gate | Name | Status | Report |
| --- | --- | --- | --- |
| G0 | Foundations | passed (2026-10-11, at 15a1a43 on `ccr-5ef35d73-oqb6fd`; tag push and merge to `main` pending the owner) | [G0.md](../gates/G0.md) |
| G1 | Core numerics | not passed |  |
| G2 | Piano engine | not passed |  |
| G3 | Plug-in alpha | not passed |  |
| G4 | Piano content | not passed |  |
| G5 | Release candidate | not passed |  |
| G6 | Plucked numerics | not passed |  |
| G7 | Plucked instrument (one gate per instrument) | not passed |  |
