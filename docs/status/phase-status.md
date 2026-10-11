# Phase status

Maintained by the orchestrator (Opus). Status values: not started, ready, in progress, in review, complete, blocked, skipped. A phase is `ready` when every entry of its `depends_on` in [phases.yaml](../plan/phases.yaml) is complete, passed or answered.

## Phases

| Phase | Name | Track | Status | Branch / PR | Gate report | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| P0.1 | Repository and build skeleton | 0 | in progress | branch `ccr-5ef35d73-oqb6fd` |  | Briefs: docs/agents/briefs/P0.1-implementer.md, P0.1-code-reviewer.md. Planning outputs written (docs/adr/TEMPLATE.md, ADR-0001 to ADR-0003). ADR-0004 (no Windows tsan preset) Proposed, awaiting orchestrator approval. T1 evidenced by temporary .github/workflows/bootstrap-build.yml, removed by P0.2. |
| P0.2 | CI and quality gates | 0 | not started |  |  |  |
| P0.3 | Test and analysis framework | 0 | not started |  |  |  |
| P0.4 | Interface skeleton | 0 | not started |  |  |  |
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
