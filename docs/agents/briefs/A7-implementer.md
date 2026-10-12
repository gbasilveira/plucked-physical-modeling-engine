# Brief: A7 Geometric nonlinearity and longitudinal waves — implementer

Status: **ready** (A3, A4 complete). Start from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A7-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

tension modulation, longitudinal modes and their transmission to the bridge.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (including "Quality settings and numerical options (decision D-13)"); the phase card **A7 Geometric nonlinearity and longitudinal waves**; for context the cards **A3**, **A4** (your inputs), **A5** (shares the SAV segment), **A10** (the exact tier behind your selector) and **C1** (wires you in, C1-R4).
- SPEC and PLAN sections:
  - SPEC-GEO — docs/plan/engine-specification.md, "Geometric nonlinearity and longitudinal waves" (all of it, including the SI-0015 energy paragraph and the high-quality tier)
  - SPEC-NL — "Nonlinear framework" (SAV, skip and drift rules)
  - SPEC-BRG — "Bridge force", "Discrete coupling order"; docs/dev/bridge.md "Hooks (A4-R5)"
  - SPEC-EXEC — per-sample loop, steps 2, 6 and 8
  - SPEC-OSC — "Difference form", "Discrete energy", "Bandwidth"; SPEC-STR — modes, losses
  - SPEC-CONV — "Quality and numerical options"; SPEC-INST defaults table (M\_L = 8)
- engine/include/pme/instrument.hpp (`SegmentDesc::tensionModulation`, `longitudinalModes`, `kappaL`, `kappaT`), engine/include/pme/string.hpp (`StringParams`, `StringModes`). These are frozen.
- Internal headers: `sav.hpp` (A3), `string_model.hpp` (A2), the A4 bridge headers, the A1 oscillator headers. docs/dev/nonlinear.md, docs/dev/string.md, docs/dev/bridge.md, docs/dev/osc.md, docs/dev/testing.md, docs/dev/signal-analysis.md, docs/dev/rtcheck.md.
- Spec issues: SI-0015 (one-way energy), SI-0011 (ψ sign). ICR-0002 (proposed: `GeometricTier`). ADR-0006.
- docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs:
  - **A3** (merged 352bbfe): `pme_nonlinear`, `SavSegment`, `savStep`. A3-T2 already ran a tension-modulation-like quartic potential; see docs/dev/nonlinear.md.
  - **A4** (complete 2026-10-12): `pme_bridge`, `BridgePoint::addVerticalForce` (the κ\_L F\_L + κ\_T ΔT hook).
  - Through them: **A2** (`compileString`, `StringModes`) and **A1** (`compileOscillatorF64`).
  - **P0.5** (103badf): used by the A7 test-author.
- Frozen interfaces: **interfaces-v2 = 300b995**. If `interfacesVersionMajor` is not 2, stop and report.
- **No description fields exist for the longitudinal losses** (SPEC-GEO: "their losses use their own b₁, b₃"). Use internal parameters `b1L`, `b3L` with default 0, set by tests. They are added to the next ICR (ICR-0002 amendment, `SegmentDesc::longitudinalB1`, `longitudinalB3`, default 0).

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

Your files are **new files only** inside two modules other phases own:

- `engine/src/string/longitudinal*.cpp` (compiled into `pme_string` by the module glob) with headers `engine/src/string/include/pme/string/longitudinal*.hpp`;
- `engine/src/nonlinear/tension*.cpp` (in `pme_nonlinear`) with headers `engine/src/nonlinear/include/pme/nonlinear/tension*.hpp`.

Do not change `string_model.*` or `sav.*`. Write `TensionModulationPotential` so that `pme_nonlinear` gains **no** dependency on `pme_string`: it takes k\_n² (float64 span) and (EA − T)/(2L) as plain numbers.

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A7: geometric nonlinearity ----
add_subdirectory(engine/tests/geometric)
# ---- end A7 ----
```

## Scope

- May edit (create or change): the paths the ownership table gives the **A7 implementer**, plus your root `CMakeLists.txt` block.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A7-R1 `TensionModulationPotential` over both polarisations, sharing the segment's ψ.
2. A7-R2 Longitudinal mode compilation (fixed ends, core EA, own losses).
3. A7-R3 Pair-sum forcing for M\_L ≤ 16; MAY add the cosine-transform path for larger M\_L.
4. A7-R4 Longitudinal bridge force F\_L and the κ\_L, κ\_T contributions to the vertical bridge force.
5. A7-R5 Per-segment enable flags.
6. A7-R6 Geometric tier selector (`numerics.geometricTier`, D-13): the default tier is selectable per segment, and the selector reports `NotImplemented` for the exact tier until A10 provides it (amended 2026-10-12 from MAY; the exact tier itself is phase A10).

### How to meet each requirement

Every formula cites `// SPEC-GEO <paragraph>`. EA is the **core's** axial stiffness E\_c π d\_c²/4 (SPEC-GEO). ρA is the full linear mass (core plus winding, A2). EA − T ≤ 0 is `InvalidArgument`. Per-sample code is templated on `S` as A3 and A4 do; compile-time arithmetic is float64.

**A7-R1.**

- `TensionModulationPotential<S> : nl::Potential<S>` over the segment's vertical **and** horizontal modes:
  - S = Σ k\_n² (q\_v,n² + q\_h,n²);
  - V\_KC = (EA − T) S²/(8L);
  - gradient ΔT k\_n² q\_·,n, with ΔT = (EA − T) S/(2L).
- With tension modulation on, the segment's SAV holds both polarisations (vertical modes, then horizontal modes, then any lumped coordinates).
- It shares ψ with any hammer potential (A5) on the same segment: potentials add, rank stays 1.
- Because V\_KC > 0 whenever the string sounds, the skip rule does not apply and A3's drift relaxation does. Document this.
- Expose ΔT (for κ\_T) and S (for ORC-12).

**A7-R2.** `compileLongitudinal(const StringModes&, const StringParams&, int ML, double b1L, double b3L, double fs, double fMax, LongitudinalModes& out)`:

- χ\_m = √(2/L) sin(K\_m x), K\_m = mπ/L, ω\_m^L = K\_m √(EA/ρA);
- σ\_m = b1L + b3L (ω\_m^L)²;
- oscillator coefficients via `compileOscillatorF64`;
- truncation by SPEC-OSC's bandwidth rule (and f\_max), keeping at most M\_L modes;
- report how many were kept: at piano lengths the longitudinal fundamental is 1 to 3 kHz, so the bandwidth rule often keeps fewer than 8.

**A7-R3.** `longitudinalForce(...)`, f\_m^L by the SPEC-GEO pair sum over both polarisations:

- f\_m^L = −(EA − T)K\_m/(2ρA√(2L)) Σ\_P Σ\_{i,j≥1} k\_i k\_j q\_P,i q\_P,j [δ\_{i+j,m} + δ\_{|i−j|,m}];
- implemented in O(N) per longitudinal mode (for each m, the sums over i with j = m − i and j = i ± m);
- fixed summation order, float64 accumulation, result float32 (`S`).

M\_L ≤ 16 uses the pair sum (MUST). The cosine-transform path for M\_L > 16 is optional (MAY). It is not a D-13 option, because it gives the same result faster: do not add it unless time allows, and then behind M\_L > 16 only.

Longitudinal modes advance with the A1 difference form and this explicit force at step n (SPEC-EXEC step 8; one-way, SI-0015). The force is per unit modal mass: check the units against SPEC and state them in docs/dev/geometric.md.

**A7-R4.** `longitudinalBridgeForce(...)`:

- F\_L = EA Σ\_m √(2/L) K\_m (−1)^m ξ\_m + (EA − T)/2 · y\_x(L)²;
- y\_x(L) = Σ\_n q\_v,n √(2/L) k\_n (−1)^n for the speaking end (and the horizontal polarisation's term added in y\_x² + z\_x², as SPEC-GEO sums both; state your reading).

The vertical bridge force then gains κ\_L F\_L + κ\_T ΔT through A4's `BridgePoint::addVerticalForce` (SPEC-EXEC step 2; SPEC-GEO "Transmission to the body"). Both terms enter at step n in both bridge schemes (SPEC-BRG "Bridge scheme"). Write an internal composition `GeometricSegment<S>` (or similar) that holds the flags, the potential, the longitudinal modes and calls the hook. C1 will reuse it.

**A7-R5.**

- `SegmentDesc::tensionModulation` enables A7-R1.
- `longitudinalModes` > 0 enables A7-R2 to A7-R4 (0 = off).
- `kappaL`, `kappaT` scale the transmission.
- Each flag off makes the segment bit-identical to a linear segment (test it).
- Longitudinal modes without tension modulation are allowed (forcing from the transverse slopes is independent of V\_KC).

**A7-R6.**

- Internal `enum class GeometricTier { Auto, Default, Exact }` mirroring ICR-0002, and a resolver taking the quality level (`Realtime` → Default, `HighQuality` → Exact).
- `Exact` returns `Status::NotImplemented` from configuration until A10 adds it; `Default` and `Auto`-under-Realtime select your code.
- No other behaviour change.

**Energy (SI-0015).** Provide in float64:

- the transverse energy: modes, bridge, ψ including V\_KC;
- separately the longitudinal energy E\_L = Σ\_m ρA H\_m^L.

Energy tests check conservation and passivity on the transverse energy only and report E\_L.

## Tests that must pass

- **ORC-11** (Longitudinal forcing), **ORC-12** (Tension modulation): written by the A7 test-author (docs/agents/briefs/A7-test-author.md), not by you. Make `longitudinalForce`, S and ΔT reachable through your internal headers. Do not write tests named `ORC-11` or `ORC-12`.
- **A7-T1** a large-amplitude pluck's instantaneous frequency follows the Kirchhoff–Carrier prediction within 2%;
- **A7-T2** the bridge-force spectrum shows phantom partials at sums f\_i + f\_j;
- **A7-T3** against P0.5, the longitudinal precursor reaches the bridge at the predicted time within 1% — written by the A7 test-author, not by you.

### How each test is built

All doctest in `engine/tests/geometric/`, labels `unit` or `integration`.

**A7-T1.**

- Setup: one plain steel string (262 Hz, L = 0.65 m, d = 1 mm, lossless, then a case with b₁ = 0.5 s⁻¹), tension modulation on, longitudinal modes off, rigid bridge, `S = double`, 48 kHz.
- Excite **mode 1 only** (q₁(0) = A, all others 0). With a single excited mode the gradient on every other mode is ΔT k\_n² q\_n = 0, so the motion stays single-mode. It is then exactly Duffing's equation q̈ + ω₁²q + β q³ = 0 with β = (EA − T)k₁⁴/(2LρA).
- Prediction (harmonic balance, first order): ω(a)² = ω₁² + ¾ β a², where a is the instantaneous amplitude. Derive it and state it in docs/dev/geometric.md. If you find a different factor, stop and report: it is a spec question, not a tuning knob.
- Choose A so that the shift is 10 to 50 cents.
- Measure the instantaneous frequency with `pme_analysis` over short windows (or zero-crossing intervals) and the amplitude envelope a(t) from the same windows.
- Pass: |Δf\_measured/Δf\_predicted − 1| ≤ 0.02 for every window where the predicted shift exceeds 20 times the estimator's resolution, with Δf = f(t) − f₁ (the shift, not the frequency).
- Also run a triangle pluck at 0.12 L with the same peak shift and report its glide against the multi-mode first-order prediction (not bounded).

**A7-T2.**

- Setup: the same string, stiffer (B ≈ 10⁻³, so f\_i + f\_j differs from any partial f\_k by more than 5 analysis bins), large triangle pluck, tension modulation and longitudinal modes on (M\_L as the bandwidth rule allows), κ\_L = 0.026, κ\_T = 0, a lossless mass–spring bridge (m = 0.3 kg, 300 Hz) through A4's hook, 2 s.
- Spectrum of the vertical bridge force F\_b,v with a Hann window.
- Pass:
  - for each pair (i, j) with 1 ≤ i < j ≤ 4, a peak lies within ±2 bins of f\_i + f\_j and at least 20 dB above the local floor (median of the surrounding ±20 bins);
  - the same peaks are at least 20 dB weaker with longitudinal modes off and κ\_L = 0 (control).
- Report the levels.

### Other tests (unit label unless stated)

- `A7-R1 …`:
  - V\_KC and ∇V against finite differences (float64, relative 10⁻⁶) over both polarisations;
  - lossless energy (transverse incl. ψ) conserved within 10⁻¹⁰ over 10⁵ samples in float64 (drift relaxation off);
  - with relaxation on, energy never rises;
  - shared ψ with a second potential (A3's test quartic or A5's hammer if merged) stays rank 1.
- `A7-R2 …`:
  - ω\_m^L against K\_m √(EA/ρA) (10⁻¹²);
  - the bandwidth truncation;
  - EA − T ≤ 0 rejected;
  - a wound string uses the core's EA and the full ρA.
- `A7-R3 …`:
  - pair sum against a direct double sum over all (i, j) (10⁻¹² relative, float64);
  - O(N) per mode (operation count or timing ratio);
  - one-way coupling (transverse modes bit-identical with longitudinal modes on and off when κ\_L = κ\_T = 0 and tension modulation off).
- `A7-R4 …`:
  - F\_L for a static longitudinal shape against the formula;
  - y\_x(L) against finite differences of y(x);
  - `addVerticalForce` receives κ\_L F\_L + κ\_T ΔT before the bridge advances (instrumented order).
- `A7-R5 …`: each flag off is bit-identical to the linear segment.
- `A7-R6 …`: the resolver table; `Exact` returns `NotImplemented`.
- Energy (SI-0015): a large pluck with longitudinal modes on. The transverse energy balance holds within 10⁻¹⁰ per step in float64; E\_L is reported, and its ratio to the transverse energy scales as amplitude² (two amplitudes, ratio of ratios within 5% of 4 for an amplitude ratio of 2).
- RT-safety (label `rt`): a segment with tension modulation and 8 longitudinal modes under `ScopedRealtimeThread`, float32.
- Benchmark `engine/tests/geometric/geometric_bench.cpp` (label `perf`): 60 transverse modes × 2 polarisations, 8 and 16 longitudinal modes, 64 samples per call. No baseline file (D-4, V2).

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A7:

- docs/dev/geometric.md names SPEC-GEO (and the SPEC-NL, SPEC-BRG, SPEC-EXEC parts), and documents:
  - the potential and its SAV layout;
  - the longitudinal compilation and truncation;
  - the pair-sum algorithm and units;
  - F\_L and the hook;
  - the flags and the tier selector;
  - the SI-0015 energy split;
  - the A7-T1 Duffing derivation;
  - measured results.
- Coverage: your files count in the 90% engine/src gate.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A7: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Run before you finish:
  - `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files;
  - `ctest --preset linux-debug` with GCC and with Clang (one build tree at a time);
  - the rt-safety preset's tests;
  - `uv run pytest tools/tests/test_p0_3_r1.py`.

### Reporting rule

Report every test `not run`, `status` `partial`. Put the local evidence in `notes`:

- the A7-T1 per-window ratios and the pluck glide;
- the A7-T2 peak levels and the control;
- the energy figures and the E\_L ratio;
- the number of longitudinal modes kept per test string;
- benchmark numbers.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A7`, `role` `implementer`, `model` `sonnet`;
- `requirements` A7-R1 to A7-R6;
- `tests` A7-T1 and A7-T2 (ORC-11, ORC-12 and A7-T3 are the test-author's);
- every created or changed path in `outputs`;
- `pull_requests` empty;
- `icrs` ICR-0002 (geometric tier, longitudinal losses);
- `spec_issues` SI-0015 as relevant plus any new defect;
- `open_risks` as found.
