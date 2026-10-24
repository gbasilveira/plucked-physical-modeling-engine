# Brief: A9 Implicit bridge step and bridge numerics options — implementer

Status: **ready** (A4 complete). The implicit scheme's numerics (matched coupling g̃, averaged bridge-mode force) are the planner's proposal, verified on the one-step map (SI-0021 "Owner decision on item 3"). The owner reviews the numerics before A9 merges. Implement and test now. If a test fails, stop and report; never tune the scheme.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A9-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

the second bridge scheme required by decision D-13 (owner answers on SI-0018 and SI-0021): an implicit bridge step with matched coupling, selectable against A4's explicit order; the explicit-mode stability check with its policy; and overdamped lumped bridge modes (SI-0020), each behind an engine setting.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (including "Quality settings and numerical options (decision D-13)"); the phase card **A9 Implicit bridge step and bridge numerics options**; for context **A4** (the module you extend), **A5** (hammered segments, which need the two-right-hand-side solve), **A8** (kernels), **C1** (C1-R7 resolves the options).
- SPEC and PLAN sections:
  - SPEC-BRG — docs/plan/engine-specification.md, "Bridge coupling" (all of it, especially **"Bridge scheme"** and **"Stability of the explicit order"**)
  - SPEC-OSC — "Bandwidth" (the overdamped real-pole continuation and its cancellation-free formula), "Difference form", "Discrete energy"
  - SPEC-EXEC — per-sample loop, steps 2 to 7 and **3′ to 5′**
  - SPEC-NL — "Explicit low-rank solve" (for segments with an active nonlinearity: the solve is linear in its right-hand side)
  - SPEC-CONV — "Quality and numerical options"
  - SPEC-ORC — row "Bridge damping" (both criteria)
- docs/spec-issues/SI-0018, SI-0020, SI-0021 (including the owner decisions and the planner's numbers), SI-0019 (rotation). ICR-0002 (proposed: `BridgeScheme`, `UnstableBridgePolicy`, `OverdampedBridgeModes`, `DiagnosticKind::BridgeSchemeSwitched`).
- docs/dev/bridge.md (the A4 internal API, the energy of the partitioned scheme, the stability table), the A4 headers `engine/src/bridge/include/pme/bridge/{terms,model,bridge_point,modal_segment}.hpp`, the A4 tests in engine/tests/bridge/ and engine/tests/oracles/bridge/.
- docs/dev/osc.md, docs/dev/nonlinear.md, docs/dev/testing.md, docs/dev/rtcheck.md, docs/dev/reference-solver.md.
- docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **A4** (complete 2026-10-12; review e44bd4f, oracles aea8a68, registry b3edcb3): `pme_bridge`, `BridgePoint<S>`, `ModalSegment<S>`, `compileBridgeMode`, the explicit scheme and its tests (A4-T1, A4-T2, ORC-06, ORC-07, A4-T3). Through it, A1 and A2.
- Frozen interfaces: **interfaces-v2 = 300b995**. The options are internal enums named as in ICR-0002 until it is applied; then you rebase.
- **The planner's verification** (SI-0021, SPEC-BRG "Bridge scheme"): Python with NumPy, exact one-step map, flexible string at 48 kHz, all modes below 20 kHz.

  | Case | Planner's result |
  | --- | --- |
  | Implicit + matched coupling, Z₀G = 0.01 | every mode within 1.7 × 10⁻⁴ of the formula |
  | Implicit + matched coupling, Z₀G = 0.05 | worst mode 4.3 × 10⁻³; 10⁻³ up to ω\_nΔt ≈ 2.4 |
  | Stiff string, B = 10⁻³ | same errors against the continuous coupled modal system |
  | Lossless mass–spring bridge, m = 10⁻⁶ kg, 8 strings | spectral radius 1 within 3 × 10⁻¹⁴ |
  | Explicit order on SI-0018's table | reproduced exactly (unstable at 10⁻⁴ kg with 3 strings, 10⁻⁵ kg with 1) |

  Use these as expectations. A deviation larger than about 2× is a finding to report.

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

No root `CMakeLists.txt` change: add your tests to engine/tests/bridge/ (yours while A9 runs), which A4's block already builds.

## Scope

- May edit (create or change): engine/src/bridge/**, engine/tests/bridge/**, docs/dev/bridge.md. A4's existing tests must keep passing unchanged. Do not edit or delete them; add new files.
- Read only: everything else, including A4's oracle files in engine/tests/oracles/bridge/ (your test-author adds new files there).
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A9-R1 Bridge scheme selector Explicit or Implicit per bridge point (internal enum mirroring ICR-0002 `BridgeScheme`); Explicit stays bit-identical to A4.
2. A9-R2 Implicit scheme exactly as SPEC-BRG "Bridge scheme": matched coupling g̃\_n = g\_n ω\_n/ω̂\_n in the modal force and in a\_n, trapezoidal conductances, averaged bridge-mode force (Fⁿ⁺¹ + 2Fⁿ + Fⁿ⁻¹)/4, affine segment response and one solve of size 1 or 2 per bridge point, including segments whose response comes from a low-rank solve (two right-hand sides, SPEC-EXEC steps 3′ to 5′).
3. A9-R3 Discrete energy of the implicit scheme derived, documented in docs/dev/bridge.md and computed in float64 for tests.
4. A9-R4 Explicit-mode stability check in `configure` (largest generalised eigenvalue against (1 − 10⁻³)·4/Δt²) with the policy SwitchToImplicit (default) or Reject, and a record of switched points for C1's diagnostic.
5. A9-R5 Overdamped lumped bridge modes (σ ≥ Ω > 0) compiled with the real-pole continuation of SPEC-OSC "Bandwidth" under Accept (default); Reject keeps A4's `Rejected`.
6. A9-R6 Cost of both schemes measured on A4's benchmark configuration and reported.

### How to meet each requirement

**A9-R1.**

- Internal enums `BridgeScheme { Auto, Explicit, Implicit }`, `UnstableBridgePolicy { SwitchToImplicit, Reject }` and `OverdampedBridgeModes { Accept, Reject }`, as in ICR-0002, plus a resolver for `Auto` from the quality level (Realtime → Explicit, HighQuality → Implicit).
- `BridgePoint::configure` takes the options (a new overload; keep A4's signature working with explicit defaults).
- With Explicit, the per-sample path is A4's code, bit for bit (test it against a saved A4 run).

**A9-R2.** Per bridge point and sample, in the implicit scheme:

- **Matched coupling.** Segments attached to an implicit point use g̃\_n = g\_n ω\_n/√ω̂²\_n:
  - ω\_n = the mode's undamped angular frequency, `StringModes::omega`;
  - ω̂²\_n = `oscV[n].omegaHat2` or `oscH[n].omegaHat2` for the polarisation;
  - g̃ is used in the modal force −g̃\_n δ\_tt w and in ã\_n = ρA ω̂²\_n g̃\_n (both polarisations).

  Compute in float64, store float32. `ModalSegment` gains a mode or a sibling class; A4's class stays as it is for explicit points.
- **Segment response.** For a linear segment, qⁿ⁺¹ = **b** − **h̃** wⁿ⁺¹ per polarisation, with:
  - **b** = the difference-form update with all forces except the bridge term, and the known part −g̃ (−2wⁿ + wⁿ⁻¹)/Δt² included;
  - **h̃** = γ ⊙ g̃ / Δt² (constant).

  Then F\_bⁿ⁺¹ = ãᵀ**b** − (ãᵀ**h̃** + T/L) wⁿ⁺¹, with ãᵀ**h̃** precomputed at configure.
- **Nonlinear segments** (A5 hammer, A7 tension modulation, A6 rank-one damper). A3's solve is linear in its right-hand side at fixed H and W. Provide the bridge side of the contract: the point accepts each segment's affine pair (c\_s, κ\_s) = (ãᵀ**b**, ãᵀ**h̃** + T/L), however the segment computed them. Document how a SAV segment produces them: apply the Woodbury solve to **a** and to the bridge column.
  - Do not edit engine/src/nonlinear (A3's). If A3's `savStep` cannot return the two solves without a change, describe the needed change in the report (an A3 follow-up) and test the bridge side with a linear-segment stand-in and a test-only SAV composition.
- **Bridge update.**
  - Conductances (G on ∥, G\_h on ⊥, a modal set's G\_hf): w\_Rⁿ⁺¹ = w\_Rⁿ + Δt G (Fⁿ⁺¹ + Fⁿ)/2.
  - Bridge modes: modal force Φ\_k (Fⁿ⁺¹ + 2Fⁿ + Fⁿ⁻¹)/4 in their difference-form update.
  - Hence w\_∥ⁿ⁺¹ = w̄\_∥ + λ\_∥ F\_∥ⁿ⁺¹, with λ\_∥ = Σ\_k Φ\_k² γ\_k/4 + Δt G/2. Likewise λ\_⊥ = Δt G\_h/2.
- **Rotation (SI-0019).** F\_∥ = c F\_v − s F\_h, F\_⊥ = s F\_v + c F\_h, and w = Rᵀ(w\_∥, w\_⊥). Each polarisation's total force is affine in its w (Σ\_s c\_s − κ\_s w), so the unknowns (w\_∥ⁿ⁺¹, w\_⊥ⁿ⁺¹) satisfy a 2 × 2 linear system. It is diagonal at θ\_b = 0 or when κ\_v = κ\_h. Solve it in float64 in closed form.
- w\_ext is added after the solve, as in A4. The SPEC-GEO hook force enters at step n (`addVerticalForce` adds to Fⁿ only).
- Then form δ\_tt wⁿ and let the segments complete qⁿ⁺¹ = **b** − **h̃** wⁿ⁺¹ (SPEC-EXEC 5′).
- Expose the API as per-sample stages, for example `beginSample` (collect each segment's (c, κ) and Fⁿ), `solve`, and accessors. The order follows SPEC-EXEC 3′ to 5′; an instrumented test records it.

**A9-R3.**

- Derive the discrete energy of the implicit scheme. Start from docs/dev/bridge.md "Energy of the partitioned scheme" with g → g̃. The trapezoidal conductance dissipates Δt G ((Fⁿ⁺¹ + Fⁿ)/2)² ≥ 0. The averaged force on bridge modes gives a modified (θ = 1/4) potential term for the coupling.
- State the conserved quantity for the lossless case and the balance with losses. Implement it in float64 for tests.
- If your derivation shows the scheme as specified is **not** energy-stable, stop and report with the derivation: that is a spec issue, not something to repair locally.

**A9-R4.** In explicit mode, `configure` estimates the largest generalised eigenvalue λ\_max of the bridge point's coupled linear update: the strings attached at configure time (C1 knows them; for A9, the point's `configure` receives the segments' modal data or is called again with them) and the bridge modes.

- Use power iteration on the symmetric (K, M) pencil of docs/dev/bridge.md, with a fixed start vector and at most a few hundred iterations, in float64, non-audio.
- If λ\_max > (1 − 10⁻³)·4/Δt², the point is unstable:
  - SwitchToImplicit: configure the point implicit and record it (`switchedToImplicit()`), for C1's `BridgeSchemeSwitched` diagnostic;
  - Reject: return `Rejected`.
- Document the cost of the check.

**A9-R5.** In `compileBridgeMode`, for σ ≥ Ω > 0 under Accept, use SPEC-OSC "Bandwidth":

- μ = √(σ² − Ω²);
- σ̂Δt = tanh(σΔt);
- ω̂²Δt² = 4 sinh((σ − μ)Δt/2) sinh((σ + μ)Δt/2)/cosh(σΔt), with σ − μ = Ω²/(σ + μ) for no cancellation;
- then β, κ, γ as usual.

Ω = 0 keeps A4's limit coefficients. Under Reject, keep A4's `Rejected`. The planner checked numerically that these coefficients give the poles e^((−σ ± μ)Δt) to round-off (SI-0020).

**A9-R6.** Extend A4's benchmark (`pme_bridge_bench`, label `perf`) with the implicit scheme on the same configuration: 3 segments, about 59 modes × 2 polarisations, a 16-mode modal set, 64 samples per call. Report both medians and the ratio. A8's `bridge_scheme_bench` gives the SIMD figures separately.

## Tests that must pass

- **ORC-06** in the implicit scheme, with the SPEC-ORC criterion for that scheme (every kept mode within 0.1% at Z₀G = 0.01, and at Z₀G = 0.05 within 0.1% for ω\_nΔt ≤ 2.4 and 0.5% for every kept mode) — written by the A9 test-author, not by you.
- **A9-T1** strings on a lossless mass–spring bridge in the implicit scheme keep the A9-R3 energy within 10⁻¹⁰ relative over 10⁶ samples in float64 with no secular trend, and with losses it never increases;
- **A9-T2** SI-0018's unstable configurations (m = 10⁻⁴ kg with 3 and 8 strings, 10⁻⁵ kg with 1 string) run stably in the implicit scheme, and the explicit-mode check flags exactly the unstable configurations of the SI-0018 table (SwitchToImplicit then runs them, and Reject returns `Rejected`);
- **A9-T3** an overdamped bridge mode's free response matches the exact real poles within 10⁻¹² (float64) and its energy never increases;
- **A9-T4** A4-T1, A4-T2 and ORC-07 pass unchanged in the implicit scheme;
- **A9-T5** against P0.5, the A4-T3 setup in the implicit scheme matches partial frequencies within 0.05 cent and decay rates within 2% — written by the A9 test-author, not by you.

### How each test is built

All doctest in engine/tests/bridge/, names starting `A9-T…` or `A9-R…`.

- **A9-T1:** A4-T1's configuration (two lossless strings, 89 modes, m = 0.4 kg, 300 Hz, θ\_b = 0 and 0.1 rad), implicit, double states, 10⁶ samples. Same pass rule as A4-T1, with 10⁻¹⁰. Second case with b₁ > 0, r\_b > 0 and G > 0: energy never increases (the per-step increase is at most 10⁻¹⁴ relative, round-off).
- **A9-T2:** SI-0018's table configurations (plain steel 262 Hz × (1 + 0.001 s), L = 0.65 m, d = 1 mm, f\_max 20 kHz, lossless mass–spring bridge 300 Hz, 48 kHz, double states, 4 × 10⁵ samples).
  - Implicit: max |E − E₀|/E₀ ≤ 10⁻¹⁰ for every row, m = 10⁻¹ … 10⁻⁵ kg, plus 10⁻⁶ kg with 8 strings.
  - Explicit with the check: the check marks exactly the cells marked unstable in SI-0018. SwitchToImplicit runs them stably; Reject returns `Rejected`.
- **A9-T3:** one lumped bridge mode (σ = 2Ω, then σ = Ω, then σ = 10Ω) alone, released from an offset. The float64 free response equals A₁e^((−σ+μ)nΔt) + A₂e^((−σ−μ)nΔt) fitted from the first two samples, within 10⁻¹² relative. SPEC-OSC energy non-increasing. Under Reject, `configure` returns `Rejected`.
- **A9-T4:** run A4-T1, A4-T2 (the thin-string case, 2% per partial) and an ORC-07-equivalent (two identical strings on one conductance, in-phase 4 f₀ Z₀ G plus intrinsic, anti-phase intrinsic, 0.1%) through the implicit scheme, with A4's setups and tolerances. New test cases calling shared helpers; A4's files stay unchanged.

### Other tests

- `A9-R1 …`: explicit selected is bit-identical to A4 (compare against an A4 run at the same commit); the resolver table.
- `A9-R2 …`:
  - g̃ and ã values (10⁻¹² relative in float64);
  - the 2 × 2 solve at θ\_b = 0.3 against a dense float64 solve of the full coupled step;
  - the SPEC-EXEC 3′ to 5′ order (instrumented);
  - a nonlinear stand-in segment's affine pair is consumed correctly.
- `A9-R4 …`: λ\_max against a dense generalised eigen-solve in float64 (Jacobi in the test, no new dependency) within 10⁻⁶ on small systems.
- `A9-R5 …`: coefficients against the formula and mpmath values from a committed fixture.
- RT-safety (label `rt`): an implicit bridge point with three segments under `ScopedRealtimeThread`, float32.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A9: docs/dev/bridge.md gains sections for:

- the two schemes and how to select them;
- the implicit algorithm, the energy derivation and the stability check;
- the overdamped modes;
- measured results (A9-T1 to T4, benchmark ratio);
- the contract for nonlinear segments (affine pair).

A4's sections stay.

## Working rules

- Work on the branch your worktree is on. Commit frequently with `A9: ` and the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Run before you finish:
  - clang-format and clang-tidy;
  - `ctest --preset linux-debug` (GCC and Clang, one tree at a time) — all A4 tests must still pass;
  - the rt-safety preset;
  - `uv run pytest tools/tests/test_p0_3_r1.py`.

### Reporting rule

Report every test `not run`, `status` `partial`. Put in `notes`:

- the energy derivation summary;
- the A9-T1 to T4 measured values;
- the stability-check cells;
- the benchmark ratio;
- any needed A3 change for two-right-hand-side solves.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with:

- `phase` `A9`, `role` `implementer`, `model` `sonnet`;
- `requirements` A9-R1 to A9-R6;
- `tests` A9-T1 to A9-T4 (ORC-06 implicit and A9-T5 are the test-author's);
- `outputs`;
- `pull_requests` empty;
- `icrs` ICR-0002;
- `spec_issues` SI-0018, SI-0020, SI-0021 plus any new.
