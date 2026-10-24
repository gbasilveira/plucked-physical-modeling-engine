# Brief: A5 Hammer and felt — implementer

Status: **ready** (A2, A3, A4 complete). Start from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A5-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the piano exciter: velocity map, multi-string felt contact, dissipation, release, repetition and una corda.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it, including the new "Quality settings and numerical options (decision D-13)"); the phase card **A5 Hammer and felt**; for context the cards **A3**, **A4**, **A6** (your inputs), **A9** (the second bridge scheme), **C1** (composes you into the note-group loop) and **B5** (fits your felt parameters).
- SPEC and PLAN sections:
  - SPEC-HAM — docs/plan/engine-specification.md, "Hammer and action" (all of it, including the new "Flexible shank" paragraph)
  - SPEC-NL — "Nonlinear framework" (all of it)
  - SPEC-BRG — "Discrete coupling order", "Unison coupling", "Bridge scheme" (so your composition does not block the implicit scheme)
  - SPEC-EXEC — "Per-sample loop of one note group" (steps 1, 2, 5 and 6, and 3′ to 5′)
  - SPEC-DMP — "Damper model" (rank-one variant, for the hammer–damper dashpot budget)
  - SPEC-CONV — "Quality and numerical options", sign conventions (s\_E)
  - SPEC-STR — "Shape vectors" (raised-cosine windows φ̄)
- engine/include/pme/exciters.hpp (`HammerParams`, `UnaCordaParams`, `FeltLaw`), engine/include/pme/instrument.hpp (`NoteGroupDesc`), engine/include/pme/nonlinear.hpp. These are frozen.
- Internal headers you build on: `engine/src/nonlinear/include/pme/nonlinear/sav.hpp` (`Potential`, `Dashpot`, `SavSegment`, `savStep`, `savEnergy`, `savDissipation`, `kMaxDashpots` = 3); `engine/src/bridge/include/pme/bridge/{terms,model,bridge_point,modal_segment}.hpp`; `engine/src/string/include/pme/string/string_model.hpp` (`windowedShapes`, `applyMute`); `engine/src/dampers/include/pme/dampers/*.hpp`; `engine/src/osc/include/pme/osc/{oscillator,mode_bank}.hpp`.
- Module notes: docs/dev/nonlinear.md, docs/dev/bridge.md (the internal API, the energy of the partitioned scheme, the SI-0019 rotation), docs/dev/dampers.md, docs/dev/string.md, docs/dev/osc.md, docs/dev/testing.md, docs/dev/rtcheck.md, docs/dev/signal-analysis.md.
- Spec issues: SI-0011 (ψ sign), SI-0012 (time convention), SI-0019 (rotation), SI-0021 and SI-0018 (bridge schemes). ICR-0002 (proposed: `ShankParams`). ADR-0006 (status returns).
- docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs:
  - **A2** (merged b80a7d4): `pme_string`, `compileString`, `windowedShapes`, `applyMute`.
  - **A3** (merged 352bbfe; ORC-08 rigid wall adb9479): `pme_nonlinear`, `SavSegment`, `savStep`.
  - **A4** (complete 2026-10-12; review e44bd4f, oracles aea8a68, registry b3edcb3): `pme_bridge`, `BridgePoint<S>`, `ModalSegment<S>`, `bridgeForce`, `horizontalForceCoefficients`.
  - **A6** (merged 96bcf97): `pme_dampers` (diagonal and rank-one damper, lift logic).
  - **A1**: `pme_osc`.
  - **P0.5** (103badf): used by the A5 test-author only.
- Frozen interfaces: **interfaces-v2 = 300b995**. If `interfacesVersionMajor` is not 2, stop and report. ICR-0002 (interfaces-v3) is proposed. When it is applied you rebase and switch your internal shank type to the public `ShankParams`.
- Current state of your module: `engine/src/exciters/exciters_placeholder.cpp`. P1 later adds plucked exciters to the same module, so name your files `hammer*` and your headers `engine/src/exciters/include/pme/exciters/hammer*.hpp`.

## Concurrent work: A5, A7, A8 and A9 run at the same time

A5, A7 and A8 (each with its test-author where briefed) run in separate git worktrees, and A9 (implicit bridge step, D-13), each with its test-author where briefed. The orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. B2 and D5 follow-ups (in review) and an interface agent for ICR-0002 (once approved) may also be committing. Edits must not overlap. **This table is identical in every A5, A7, A8 and A9 brief.**

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/exciters/**` (replaces `exciters_placeholder.cpp`; internal headers in `engine/src/exciters/include/pme/exciters/`), `engine/tests/exciters/**`, `docs/dev/hammer.md` | A5 implementer | read only |
| `engine/tests/oracles/exciters/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-09 and ORC-19, and an appended note in the test-location cell of ORC-08 | A5 test-author | read only |
| New files only: `engine/src/string/longitudinal*` and `engine/src/string/include/pme/string/longitudinal*`, `engine/src/nonlinear/tension*` and `engine/src/nonlinear/include/pme/nonlinear/tension*`; `engine/tests/geometric/**`; `docs/dev/geometric.md` | A7 implementer | read only |
| `engine/tests/oracles/geometric/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-11 and ORC-12 | A7 test-author | read only |
| `engine/src/simd/**` (replaces `simd_placeholder.cpp`; internal headers in `engine/src/simd/include/pme/simd/`), `engine/tests/simd/**`, `docs/dev/simd.md`, one new file `tools/bench/baselines/<machine-id>-a8.json` | A8 implementer | read only. **No Highway include outside `engine/src/simd/`** (ADR-0001). A8 adds kernels only and wires nothing into osc, string, bridge or nonlinear (C1 composes them). Its kernels are generic primitives (dot products, mode update, rank-one update) that serve **both** bridge schemes; A8 MUST NOT encode the explicit coupling order (for example a fused "bridge force, then advance with δ\_tt w" kernel) in a way that the implicit scheme cannot reuse. |
| `engine/src/bridge/**`, `engine/tests/bridge/**`, `engine/tests/oracles/bridge/**`, `docs/dev/bridge.md` | A9 implementer (and A9 test-author for `engine/tests/oracles/bridge/test_a9_*` and new files there; the existing A4 oracle files stay as they are) | read only: call the A4 internal headers, never edit them. **Only A9 edits engine/src/bridge while these phases run.** A5 and A7 use `BridgePoint`, `ModalSegment`, `bridgeForce` and the hooks as they are. |
| Existing files of `engine/src/string/**` (`string_model.*`), `engine/src/nonlinear/**` (`sav.*`), `engine/src/osc/**`, `engine/src/dampers/**`, `reference/**`, `tools/reference-cli/**`, `engine/tests/{osc,string,nonlinear,dampers,oracles/osc,oracles/string,oracles/nonlinear,oracles/dampers}/**` and their module notes | A1, A2, A3, A6, P0.5 (complete; follow-ups only when the orchestrator routes them) | read only; call their internal headers |
| `engine/include/pme/**`, `engine/tests/interfaces/**`, `tools/params/**`, `params.json`, the other `engine/src/*/` stubs and placeholders (`body`, `runtime` except the B1 loader, `barriers`, `sched`) | nobody (frozen at interfaces-v2 = 300b995; ICR-0002 is proposed and will be applied by an interface agent) | read only |
| `compiler/**`, `plugin/**`, `engine/src/runtime/loader/**` | B2, D5 follow-ups, B1 (complete) | read only |
| `.github/workflows/**`, `tools/ci/**`, `tools/trace/**`, `tools/tests/**`, `CONTRIBUTING.md`, `docs/dev/ci.md`, `CMakePresets.json`, `cmake/**` (including `cmake/dependencies.cmake`), `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody in these phases | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

**Shared files: minimal, delimited, append-only edits, merged in the order A5, A7, A8, A9.** Later merges rebase onto earlier ones. Keep every block self-contained, so the order does not change behaviour.

| Shared file | A5 | A7 | A8 | A9 |
| --- | --- | --- | --- | --- |
| Root `CMakeLists.txt` | one block `# ---- A5: hammer and felt ----` … `# ---- end A5 ----` appended after the last line | one block `# ---- A7: geometric nonlinearity ----` … `# ---- end A7 ----` appended | one block `# ---- A8: SIMD kernels ----` … `# ---- end A8 ----` appended | none (tests go into engine/tests/bridge/, already built by A4's block) |
| `engine/tests/oracles/CMakeLists.txt` | test-author appends `add_subdirectory(exciters)` | test-author appends `add_subdirectory(geometric)` | none | none (the A9 test-author appends its `test_a9_*` registrations to `engine/tests/oracles/bridge/CMakeLists.txt`) |
| `docs/dev/interfaces.md`, the sentence under "Stubs (P0.4-R3)" | move `exciters` out of the placeholder list, add "`exciters`: hammer by A5 (plucked exciters P1)" | none | move `simd` out of the placeholder list, add "`simd`: implemented by A8" | none |
| `engine/tests/oracles.md` | rows ORC-08 (location note), ORC-09, ORC-19 only | rows ORC-11, ORC-12 only | none | ORC-06: an appended note in the test-location cell (A9 test-author) |

**Rules for every agent in these phases.**

- **Test names** start with a requirement, test or oracle ID followed by a space (C++ doctest) or `test_<id>_` (pytest), and contain none of `,` `*` `?` `\` `"` `;`. Check with `uv run pytest tools/tests/test_p0_3_r1.py`. **Every requirement ID and every test ID of your card needs at least one test whose name starts with that ID** (the traceability job, docs/dev/traceability.md, fails a complete phase that lacks one).
- Stage files explicitly (`git add <path>`); never `git add -A` or `-u`.
- **Disk space is limited.** Build one preset at a time. Before configuring another preset, delete the previous build directory (`rm -rf build/<preset>`). Never keep more than two build trees. Delete scratch programs and their builds from your scratchpad when you are done. The reference-comparison tests need an optimised preset (`linux-release` or `linux-rt-safety`), so build it last, by itself.
- **Engine settings (D-13).** When SPEC offers alternatives, implement each behind an option (SPEC-CONV "Quality and numerical options"). Until ICR-0002 is applied, options are internal enums whose names and values match ICR-0002. Never pick one alternative silently.
- If something cannot be done as written (a tolerance not reached, an instability, SPEC unclear), stop that item. Do not loosen a tolerance or change the scheme. Describe it in the report with measured values; the planner files a spec issue.

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A5: hammer and felt ----
target_include_directories(pme_exciters PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/exciters/include")
target_link_libraries(pme_exciters PUBLIC pme_bridge pme_nonlinear pme_string pme_osc)
add_subdirectory(engine/tests/exciters)
# ---- end A5 ----
```

## Scope

- May edit (create or change): the paths the ownership table gives the **A5 implementer**, plus your root `CMakeLists.txt` block and your `docs/dev/interfaces.md` words.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A5-R1 Velocity map and launch at a gap of v\_HΔt, with no added latency.
2. A5-R2 `HammerPotential` over all struck strings with windows and approach sign s\_E.
3. A5-R3 Hunt–Crossley dashpots per struck string; Stulov hereditary felt as an option.
4. A5-R4 Release and check at d\_check; repetition while in flight.
5. A5-R5 Una corda map: strike weights, felt-zone stiffness and exponent; upright soft pedal as velocity scaling.
6. A5-R6 Flexible shank as one extra oscillator (SPEC-HAM "Flexible shank"), selected per instrument by `HammerParams::shank`; absent = rigid hammer (D-13; amended 2026-10-12 from MAY).
7. A5-R7 Diagnostics: contact duration and peak force per strike.

### How to meet each requirement

Namespace `pme::exciters`. Every formula cites `// SPEC-HAM <paragraph>` or `// SPEC-NL <paragraph>`. Compile-time arithmetic is float64. Per-sample code is templated on the state type `S` (float in the engine, double in tests), as A3 and A4 do. Configuration (allocating, non-audio) is separate from the per-sample path (`noexcept`, no allocation, lock, exception or I/O). Status returns per ADR-0006.

**Coordinates (SPEC-NL "Coordinates").** A struck note is one SAV segment: the vertical-polarisation modes of every struck string (string 0, then 1, then 2, each in mode order), followed by the hammer coordinate x\_H (mass m\_H, free: β = 1, κ = 0, γ = Δt²), followed by the shank's internal mode when the shank is enabled. Use A3's `SavSegment<S>` as is: `setCoordinate` for modes, `setLumped` for the hammer. All struck strings share one ψ (SPEC-NL: potentials add). The horizontal polarisations are linear and are advanced as in A4's `ModalSegment`. A string that is not struck (una corda's leaving string at s = 1) still belongs to the note and the bridge point.

**A5-R1.**

- `hammerVelocity(nu, const HammerParams&)` returns v\_H = v\_min + (v\_max − v\_min) ν^γ\_v (SPEC-HAM "Velocity map"), for ν ∈ (0, 1]. ν ≤ 0 or non-finite gives `InvalidArgument`.
- `launch` places the hammer at gap v\_HΔt from the window-averaged string position, along s\_E, moving at v\_H. Contact therefore starts on the next sample: no action latency.
- The upright soft pedal scales v\_H by (1 − s ρ\_up) (A5-R5).

**A5-R2.** `HammerPotential<S> : nl::Potential<S>`:

- δ\_s = x\_H − s\_E Σ\_n φ̄\_s,n q\_s,n;
- V\_H = Σ\_s w\_s K\_s/(p\_s + 1) [δ\_s]₊^(p\_s+1);
- gradient: ∂V/∂x\_H = Σ\_s w\_s K\_s [δ\_s]₊^p\_s, and ∂V/∂q\_s,n = −s\_E φ̄\_s,n w\_s K\_s [δ\_s]₊^p\_s.

Rules:

- φ̄ are A2's raised-cosine window shape vectors of width b\_H at x\_c = β\_c L (`windowedShapes`, float32 storage, float64 arithmetic in the potential).
- `value` returns exactly 0 when every δ\_s ≤ 0, so A3's skip rule applies between strikes.
- When the shank is on, x\_H is the fixed linear combination of the two shank coordinates (A5-R6), and the contact gradient has two hammer entries.
- Provide a test-only variant with no string modes and a fixed target ("rigid felt contact", SPEC-HAM "Oracle"), for A5-T1 and the test-author's ORC-08 case.

**A5-R3.**

- **Hunt–Crossley.** One `nl::Dashpot` per struck string along c\_s = ∂δ\_s/∂z (+1 on x\_H, −s\_E φ̄\_s,n on that string's modes), with R\_sⁿ = w\_s K\_s μ\_HC [δ\_sⁿ]₊^p\_s. For a single struck string, c is parallel to g: set `alongGradient`, so r = 1. A trichord gives r = 1 + 3 = 4, A3's maximum.
- **Stulov** (`FeltLaw::Stulov`):
  - h\_sⁿ = a h\_sⁿ⁻¹ + (1 − a)[δ\_sⁿ]₊^p, a = e^(−Δt/τ);
  - explicit force F\_hered,sⁿ = **+** w\_s K\_s min(ε h\_sⁿ, [δ\_sⁿ]₊^p) c\_s while δ\_s > 0 (0 otherwise), passed through `savStep`'s `explicitForce` (N per coordinate). It opposes the elastic force: +w\_s K\_s min(εh, δ^p) on x\_H (towards the string), −s\_E φ̄\_s,n w\_s K\_s min(εh, δ^p) on string mode n. Total felt force K(δ^p − εh), clamped at ≥ 0 (amended 2026-10-13, SI-0029 correction: the earlier text "−K ε hⁿ along c\_s" had the elastic force's sign and produced K(δ^p + εh));
  - the elastic part stays in V\_H;
  - regression check: rigid wall, m\_H = 8 g, K = 4·10⁹, p = 2.5, ε = 0.3, τ = 0.5 ms, v = 3 m/s gives rebound KE / incoming = 0.7187 at 48 kHz with the clamp (0.717 converged at 384 kHz; assert ± 1 %); 1.000 at ε = 0; 1.358 with the wrong sign; 0.598 without the clamp (context only: that law pulls on unloading) (figure corrected 2026-10-13, SI-0029).

  SPEC says this variant is not provably passive and energy monitors MUST check it. Provide the hereditary force's work per step in float64, so a test can close the balance E^(n+1) − Eⁿ + dissipation = work.
- **Dashpot budget with dampers.** SPEC-NL allows at most 3 dashpots per segment (A3 `kMaxDashpots`). While a trichord is in contact, its three felt dashpots use the whole budget, so a rank-one damper (A6) on the same segment cannot be added. Provide `std::size_t hammerDashpotCount()` and document in docs/dev/hammer.md that the composition (C1) must then use the damper's diagonal mode for that segment, as SPEC-NL requires. While the key is down the damper is lifted (R = 0, no column), so the case arises only at very fast re-strikes. Test the budget arithmetic; do not edit A6.

**A5-R4.**

- **Release and check.** After rebound the hammer leaves the coordinate set when it is farther than d\_check from the string (largest δ\_s < −d\_check) and moving away. "Leaves" means: it is frozen (u = 0, held position) and excluded from the potential, so the segment's free update (skip rule) is exact for the strings.
- **Double strikes** need no special code.
- **Repetition.** A new launch while the hammer is in flight replaces its velocity with the new v\_H. If it is within d\_check of the string it continues from its current position (no jump in x\_H). Otherwise it restarts from the launch gap. The strings keep their state.

**A5-R5.**

- Smoothstep S(s) between s₀ and s₁ (SPEC-HAM; define S as the cubic 3t² − 2t³ of t = clamp((s − s₀)/(s₁ − s₀), 0, 1) and state it in docs/dev/hammer.md).
- Weights from the SPEC-HAM table, using `UnaCordaParams::leavingString`: −1 for a single string, which then has w = 1.
- K\_s(s) = K\_s[1 − s(1 − κ\_uc)], p\_s(s) = p\_s + s Δp\_uc.
- The upright soft pedal scales v\_H by (1 − s ρ\_up) and leaves the strike unchanged.
- The shift s is a continuous parameter: the weights and felt values are recomputed per sample while s ramps.

**A5-R6.**

- Internal `struct ShankParams { double mass; double frequency; double sigma; }` mirroring ICR-0002, with `std::optional` on your hammer configuration; absent means a rigid hammer.
- Head m\_H and shank m\_S are joined by a spring and dashpot. Transform to normal coordinates: a free rigid-body coordinate (total mass) and one internal mode with Ω\_S = 2π f\_S and σ\_S (compiled with `compileOscillatorF64`; mass = the reduced mass).
- x\_H is the fixed linear combination of the two, so the contact direction has two hammer entries.
- At launch both masses move at v\_H with zero spring deflection.
- Rigid and shank runs share all other code.

**A5-R7.** Per strike, record:

- the contact duration: first to last sample with any δ\_s > 0, sub-sample interpolated at the zero crossings of max\_s δ\_s;
- the peak contact force: the SAV force actually applied, |∂V/∂x\_H|-equivalent g\_H (ψ^(n+½) + ψ^(n−½))/2, in N;
- a strike counter.

Expose them as plain counters and values that the runtime (C1) reads and clears on the audio thread, like A3's `SavCounters`.

**Reference composition (for tests, A5-T2 helper and C1).** Provide an internal class, for example `StruckNote<S>`, that runs one note per sample in the SPEC-EXEC order:

1. launch events (step 1);
2. each string's F\_b from qⁿ and wⁿ, using `bridgeForce` with A4's vertical and horizontal coefficients over that string's slice of the SAV state (step 2);
3. `BridgePoint::addForce` / `advance` (steps 3 and 4);
4. explicit modal forces −ρA g\_n δ\_tt wⁿ per string mode (in N per coordinate, because `savStep` divides by the mass) plus the Stulov force (step 5);
5. `savStep` (step 6);
6. the horizontal polarisations as in `ModalSegment` (step 7).

A C1-like composition is all A5 needs. Keep the bridge force and the bridge term separate inputs (do not fold δ\_tt w into the potential or the solve), so that A9's implicit scheme (SPEC-EXEC steps 3′ to 5′: the low-rank solve applied to a second right-hand side) can be added later without restructuring your code.

**Precision.**

- ψ, V, the gradient, the Hunt–Crossley R and every reduction are float64 (A3 does this inside `savStep`).
- φ̄ is stored float32.
- The engine instantiation is `S = float`; energy tests use `S = double`.

## Tests that must pass

- **ORC-09** (Hammer–string energy), **ORC-19** (Una corda aftersound): written by the A5 test-author (docs/agents/briefs/A5-test-author.md), not by you. Make them possible: `HammerPotential`, the dashpots, the launch, una corda and `StruckNote` must be reachable through `pme_exciters`'s internal headers. Do not write tests named `ORC-09` or `ORC-19`.
- **A5-T1** contact time scales as v^((1−p)/(1+p)) within 2% against a rigid wall;
- **A5-T2** against P0.5 with elastic felt, peak force and contact duration within 2% — written by the A5 test-author, not by you;
- **A5-T3** una corda weights and felt-zone values match SPEC-HAM tables;
- **A5-T4** re-launching a hammer in flight keeps its position continuous.

### How each test is built

All doctest in `engine/tests/exciters/`, one `TEST_CASE` per case, labels `unit` (or `integration` for long runs).

**A5-T1.**

- Setup: `HammerPotential` against a fixed target (no strings), elastic (μ\_HC = 0), m = 8 g, K = 4.5 × 10⁹ N/m^p.
- Cases: p = 2.5 and p = 3.5; v = 0.5, 1, 2, 4 and 6 m/s; 48 kHz, plus one case at 96 kHz; double states.
- Contact time: interpolate the zero crossings of δ at contact start and end.
- Pass:
  - |t\_c(v)/t\_c(1 m/s) ÷ v^((1−p)/(1+p)) − 1| ≤ 0.02 for every v;
  - additionally, the absolute t\_c against the SPEC-HAM closed form within max(1%, 1 sample) at v = 3 m/s, p = 2.5 (the ORC-08 numbers);
  - rebound speed within 0.1% of −v.
- Report every ratio.

**A5-T3.** For trichord, bichord and single string, and s ∈ {0, 0.3, 0.45, 0.6, 0.9, 1}:

- w\_s, K\_s(s) and p\_s(s) equal the SPEC-HAM formulas within 10⁻¹² relative (κ\_uc = 0.6, Δp\_uc = 0 and a non-zero Δp\_uc = 0.2);
- `leavingString` = 0 and 2 for a trichord, −1 for a single string;
- the upright soft pedal: v\_H scaled by (1 − s ρ\_up) and weights unchanged.

**A5-T4.** Launch a hammer at a string (single string, small σ), let it rebound. Then:

- inside d\_check, issue a second launch. Pass: x\_H at the sample of the re-launch equals x\_H one sample earlier plus the old velocity times Δt (no jump; compare the position sequence, not just one value), and the velocity is the new v\_H;
- outside d\_check: x\_H restarts at the launch gap;
- also during contact (re-strike while touching): no jump, energy accounted.

### Other tests (unit label unless stated)

- `A5-R1 …`:
  - the velocity map at ν = 0.01, 0.5, 1 (exact formula);
  - contact begins on the sample after launch for every v\_H;
  - invalid ν returns `InvalidArgument`.
- `A5-R2 …`:
  - V\_H and its gradient against finite differences of V\_H in float64 (relative 10⁻⁶) for one and three strings, with s\_E = +1 and −1;
  - `value` is exactly 0 out of contact;
  - window and strike position match `windowedShapes`.
- `A5-R3 …`:
  - Hunt–Crossley energy is non-increasing (float64, E^(n+1) − Eⁿ + dissipation = 0 within 10⁻¹² relative per step) for one and three strings;
  - r = 1 for one string (`alongGradient`) and r = 4 for a trichord;
  - the dashpot-budget arithmetic;
  - Stulov: the balance including the hereditary work closes within 10⁻¹² per step in float64, and the measured total energy change over a strike is reported.
- `A5-R4 …`: release after rebound (frozen hammer, strings bit-identical to a free run afterwards); a bass double strike with a slow heavy hammer produces two contact intervals without special code.
- `A5-R6 …`:
  - the shank normal-mode transform reproduces the two-mass equations (free response frequency f\_S within 10⁻¹⁰ in float64);
  - with a very stiff shank (f\_S → 20 kHz) contact time and peak force approach the rigid hammer's within 1%;
  - absent shank is bit-identical to the rigid code path.
- `A5-R7 …`: duration and peak force equal values computed independently from the recorded force trace.
- RT-safety (label `rt`): a trichord strike with a mass–spring–damper bridge point under `pme::rtcheck::ScopedRealtimeThread`, float32, allocates nothing.
- Benchmark `engine/tests/exciters/hammer_bench.cpp` (`pme_add_benchmark`, label `perf`): a trichord of 3 × 60 modes plus hammer, r = 4, 64 samples per call, and the skip path out of contact. No baseline file (D-4, V2); report the numbers.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A5:

- docs/dev/hammer.md names SPEC-HAM and SPEC-NL sections, and documents:
  - the coordinate layout;
  - the potential, gradient and dashpots;
  - Stulov's energy accounting;
  - the dashpot budget with dampers;
  - release, repetition, una corda and the smoothstep definition;
  - the shank transform;
  - the diagnostics;
  - `StruckNote` and how C1 and A9 extend it;
  - measured results.
- Coverage: `engine/src/exciters` is in the 90% engine/src line-coverage gate.
- Performance baseline: the benchmark must exist; the stored baseline waits for V2 and D-4.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A5: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Run before you finish:
  - `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files;
  - `ctest --preset linux-debug` with GCC and with Clang (one build tree at a time);
  - the rt-safety preset's tests;
  - `uv run pytest tools/tests/test_p0_3_r1.py`.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`:

- commands, compilers and presets;
- the A5-T1 ratios and absolute t\_c;
- the A5-T4 continuity values;
- the Stulov energy figures;
- the dashpot-budget note;
- benchmark numbers.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A5`, `role` `implementer`, `model` `sonnet`;
- `requirements` A5-R1 to A5-R7;
- `tests` A5-T1, A5-T3, A5-T4 (ORC-09, ORC-19 and A5-T2 are the test-author's);
- every created or changed path in `outputs`;
- `pull_requests` empty;
- `icrs` ICR-0002 (shank);
- `spec_issues` any new defect;
- `open_risks` as found.
