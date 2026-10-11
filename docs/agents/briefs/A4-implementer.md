# Brief: A4 Bridge coupling — implementer

Status: **ready (pending CI on A2/A3/B1)**. A2 is approved by review and waits only for one fully green pr.yml run; start now from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief. If A2's CI run forces a change in `engine/src/string/`, rebase onto it.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/A4-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

strings on a moving bridge, with pluggable bridge models, unison sharing and polarisation rotation.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it; especially Real-time rules, Numerics rules, Testing policy); the phase card **A4 Bridge coupling**; for context the cards **A2** (your input), **A5**, **A7** (use your hooks), **B3** (builds the modal-set bridge you consume) and **C1** (composes you into the note-group loop).
- SPEC and PLAN sections:
  - SPEC-BRG — docs/plan/engine-specification.md, "Bridge coupling" (all of it)
  - SPEC-CONV — "Scope, conventions and notation" (units, sign conventions, the e^(+iωt) time convention of SI-0012, precision)
  - SPEC-OSC — "The modal oscillator" ("Difference form", "Derived quantities", "Discrete energy") for the bridge modes
  - SPEC-BODY — "Soundboard and bodies", paragraphs "Local bridge model" and "Sympathetic path" (the modal-set model and the w\_ext input)
  - SPEC-EXEC — "Execution order", per-sample loop steps 2 to 5 (the order your component implements)
  - SPEC-GEO — "Transmission to the body" (the κ\_L, κ\_T hook only)
- engine/include/pme/bridge.hpp (frozen: `SegmentBridgeTerms`, `BridgePointDesc`, `compileBridgeTerms`), engine/include/pme/body.hpp (`LocalBridge`), engine/include/pme/string.hpp (`StringModes`), engine/include/pme/osc.hpp (`OscCoeffs`).
- A1 and A2 internal headers: `engine/src/osc/include/pme/osc/{oscillator,mode_bank}.hpp`, `engine/src/string/include/pme/string/string_model.hpp`; docs/dev/osc.md, docs/dev/string.md.
- docs/spec-issues/SI-0010 (rear segments; `applyMute` takes `detuneH`), SI-0012 (time convention). docs/adr/ADR-0006 (status returns).
- docs/dev/testing.md, docs/dev/signal-analysis.md, docs/dev/rtcheck.md, docs/dev/reference-solver.md ("Using the reference from later phases"; for context: A4-T3 is the test-author's).
- docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **A2** (stiff string compiler; merged at b80a7d4, test-author bbaa854, A2-T3 b4ecaa9; approved, pending CI): `pme_string`, `compileString`, `pme::string::applyMute`, `windowedShapes`, docs/dev/string.md. Through A2: **A1** (`pme_osc`, `ModeBank`, `stepModes`, `compileOscillatorF64`, complete). **A3** (`pme_nonlinear`, approved, pending CI) is not needed by A4. **P0.5** (reference solver, merged at 103badf; approved, pending CI) is used only by the A4 test-author.
- Frozen interfaces: **interfaces-v2 = 300b995**. If `interfacesVersionMajor` is not 2, stop and report.
- Current state of your module: `engine/src/bridge/bridge_stub.cpp` returns `NotImplemented`; `engine/tests/interfaces/test_stubs.cpp` checks that in its `compileBridgeTerms` SUBCASE.
- Carried follow-up from the A2 review: every caller of `pme::string::applyMute` passes the string's `StringParams::detuneH` as the 4th argument (`StringModes` does not store it). Your tests that build rear or duplex segments with a mute MUST do so.
## Concurrent work: A4, A6, B2 and B4 run at the same time

A4, A6, B2 and B4 (each with its test-author where briefed) run in separate git worktrees; the orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. A CI fixer and the D3 lv2lint follow-up may also be committing (`.github/workflows/pr.yml`, `docs/dev/ci.md`, `docs/dev/lv2.md`, `plugin/lv2/**`). Edits must not overlap. This table is identical in every A4, A6, B2 and B4 brief.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/bridge/**` (replaces `bridge_stub.cpp`), `engine/tests/bridge/**`, `docs/dev/bridge.md`; in `engine/tests/interfaces/test_stubs.cpp` the `compileBridgeTerms` SUBCASE only | A4 implementer | read only |
| `engine/tests/oracles/bridge/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-06, ORC-07 only | A4 test-author | read only |
| `engine/src/dampers/**` (replaces `dampers_placeholder.cpp`), `engine/tests/dampers/**`, `docs/dev/dampers.md` | A6 implementer | read only |
| `engine/tests/oracles/dampers/**`; in `engine/tests/oracles.md` the status and test-location cells of row ORC-13 only | A6 test-author | read only |
| `compiler/pme_compiler/body/**` (including the shared `body/modal_blob.py`), `compiler/pme_compiler/materials/**`, `compiler/tests/test_b2_*.py` except `test_b2_t4_*.py` and `test_b2_r5_reference_*.py`, `compiler/tests/data/b2/**`, `docs/dev/body-compiler.md` | B2 implementer | B4 imports `pme_compiler.body.modal_blob` (read only) |
| `compiler/tests/test_b2_t4_*.py`, `compiler/tests/test_b2_r5_reference_*.py`, `compiler/tests/data/b2_t4/**`; in `.github/workflows/pr.yml` the one `uv run pytest tools/tests/test_p0_5_*.py …` line of the `reference-python` job (append one path, nothing else) | B2 test-author | read only |
| `compiler/pme_compiler/importers/**`, `compiler/tests/test_b4_*.py`, `compiler/tests/data/b4/**`, `docs/dev/body-import.md` | B4 implementer | read only |
| `engine/src/string/**`, `engine/src/nonlinear/**`, `engine/src/osc/**`, `compiler/pme_compiler/schema/**`, `engine/src/runtime/loader/**`, `instruments/examples/**`, `reference/**`, `tools/reference-cli/**` and their tests and module notes | A2, A3, A1, B1, P0.5 (complete or pending CI; follow-ups only when the orchestrator routes them) | read only; call their internal headers and Python APIs |
| `engine/include/pme/**`, the other `engine/src/*/` stubs and placeholders (`body`, `runtime`, `exciters`, `barriers`, `sched`, `simd`), `engine/tests/interfaces/**` (except the A4 SUBCASE), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2 = 300b995; changes only by ICR) | read only |
| `.github/workflows/**`, `tools/ci/**`, `CONTRIBUTING.md`, `docs/dev/ci.md`, `CMakePresets.json`, `cmake/**` (including `cmake/dependencies.cmake`), `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody in these phases (CI fixer and D3 only), except the B2 test-author's one pr.yml line above | read only |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

**Shared files: minimal, delimited, append-only edits, merged in this order: A4, A6, B2, B4, then the B2 test-author's pr.yml line.** The second and later merges rebase onto the earlier ones; keep every block self-contained so the order does not change behaviour.

| Shared file | A4 | A6 | B2 | B4 |
| --- | --- | --- | --- | --- |
| Root `CMakeLists.txt` | one block `# ---- A4: bridge coupling ----` … `# ---- end A4 ----` appended after the last line | one block `# ---- A6: dampers and pedals ----` … `# ---- end A6 ----` appended after the last line | none | none |
| `engine/tests/oracles/CMakeLists.txt` | test-author appends one line `add_subdirectory(bridge)` | test-author appends one line `add_subdirectory(dampers)` | none | none |
| `docs/dev/interfaces.md`, sentence under "Stubs (P0.4-R3)" | move `bridge` out of the stub list, add "`bridge`: implemented by A4" next to the existing "implemented by" notes | move `dampers` out of the placeholder list, add "`dampers`: implemented by A6" | none | none |
| `pyproject.toml`, `uv.lock` | none | none | none expected (scikit-fem, gmsh and meshio are already in the `fem` extra; CI installs `--all-extras`) | at most one new line in an existing extra, in its own commit, then `uv lock`; if B2 also changed the file, B4 rebases and reruns `uv lock` (never hand-merge `uv.lock`) |
| `cmake/dependencies.cmake` | none | none | none | none |
| `.github/workflows/pr.yml` | none | none | test-author: one line (see table), after the CI fix and D3 have merged | none |

**Rules for every agent in these phases.**

- Test names start with a requirement, test or oracle ID followed by a space (C++ doctest) or `test_<id>_` (pytest), and contain none of `,` `*` `?` `\` `"` `;` (they break ctest filters). Check with `uv run pytest tools/tests/test_p0_3_r1.py`.
- In any GitHub Actions workflow you are allowed to touch, a step `name:` that contains `: ` must be quoted, and you run `actionlint` on the edited workflow before committing. Phases other than the B2 test-author do not edit workflows.
- Stage files explicitly (`git add <path>`); never `git add -A` or `-u`.

Your root `CMakeLists.txt` block, appended after the last line and nothing else changed:

```cmake
# ---- A4: bridge coupling ----
target_include_directories(pme_bridge PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/bridge/include")
target_link_libraries(pme_bridge PUBLIC pme_string pme_osc)
add_subdirectory(engine/tests/bridge)
# ---- end A4 ----
```

## Scope

- May edit (create or change): the paths the ownership table gives the **A4 implementer**, plus your root `CMakeLists.txt` block and your `docs/dev/interfaces.md` words.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. A4-R1 `compileBridgeTerms` for both bridge ends; `bridgeForce` as one dot product plus the T/L term.
2. A4-R2 `BridgePoint` component implementing the SPEC-BRG coupling order, with pluggable models: conductance, mass–spring–damper, and the modal-set interface used by B3.
3. A4-R3 Unison sharing of one bridge point by several segments.
4. A4-R4 Polarisation rotation θ\_b and horizontal conductance G\_h.
5. A4-R5 Hooks for w\_ext (sympathetic input) and for κ\_L, κ\_T contributions (wired in A7).

### How to meet each requirement

Internal headers go in `engine/src/bridge/include/pme/bridge/*.hpp`, namespace `pme::bridge`, so tests, A5, A7, B3 and C1 can use them. Every formula cites `// SPEC-BRG <paragraph>`. Compile-time arithmetic in float64; runtime states float32 in the engine. Write the per-sample code as templates on the state type `S` (float in the engine, double in tests), as A3 does, so energy tests can run in float64. Status per ADR-0006.

**A4-R1.** Implement `pme::compileBridgeTerms(const StringModes&, BridgeEnd, SegmentBridgeTerms&)` (delete `bridge_stub.cpp`):

- g\_n = √(2/L)(−1)^(n+1)/k\_n for `AtL` and √(2/L)/k\_n for `At0` (SPEC-BRG "Modal equation with a moving support"), float64 then float32.
- a\_n = ρA ω̂²\_n g\_n with ω̂²\_n = `modes.oscV[n].omegaHat2` (SPEC-BRG "Bridge force": the discrete ω̂² MUST be used), float32.
- `tOverL` = T/L; `end` = the argument.
- `InvalidArgument` (out unchanged) for empty modes, inconsistent vector lengths (`k`, `omega`, `sigma`, `oscV`, `oscH`), L ≤ 0, T ≤ 0, ρA ≤ 0, or a non-finite value.

The horizontal polarisation has its own discrete ω̂²\_h,n (`modes.oscH[n].omegaHat2`, detuned by `detuneH`), and SPEC-BRG requires the discrete ω̂² of the polarisation that is coupled. `SegmentBridgeTerms` (frozen) holds one `a`; provide the horizontal vector through an internal function, for example `Status horizontalForceCoefficients(const StringModes&, BridgeEnd, std::vector<float>& aH)`, and use it for F\_h. (A member `aH` is a candidate for the next ICR; do not change bridge.hpp.)

Provide `template <class S> S bridgeForce(const SegmentBridgeTerms&, const S* q, S wb) noexcept` (SPEC-BRG "Contract": F\_b = Σ a\_n q\_n − (T/L) w\_b) and the same with an explicit coefficient span (for `aH`). The sum is one dot product in a fixed order (A8 vectorises it later; keep a scalar reference). The float instantiation is the engine's `float bridgeForce(const SegmentBridgeTerms&, const float*, float)`.

**A4-R2.** A `BridgePoint` class implementing SPEC-BRG "Discrete coupling order" and SPEC-EXEC per-sample steps 2 to 4 for one bridge point:

1. Segments add their forces F\_b,s for each polarisation (vertical v, horizontal h) in a fixed order (segment index).
2. Rotate (F\_v, F\_h) to the bridge frame with R(θ\_b) (A4-R4), advance the ∥ model and the ⊥ conductance with the force at step n to w\_∥ⁿ⁺¹, w\_⊥ⁿ⁺¹, add the integrated external motion (A4-R5), rotate back to (w\_v, w\_h).
3. Expose δ\_tt wⁿ = (wⁿ⁺¹ − 2wⁿ + wⁿ⁻¹)/Δt² per polarisation, so each segment applies the modal force −g\_n δ\_tt wⁿ.

The ∥-axis model is pluggable. Use one representation for all three kinds: a set of M bridge modes (Ω\_k, σ\_k, Φ\_k) advanced in the A1 difference form with modal force Φ\_k F\_b,∥ⁿ, plus a conductance G (w\_Rⁿ⁺¹ = w\_Rⁿ + Δt G F\_b,∥ⁿ), w\_∥ = Σ Φ\_k r\_k + w\_R (SPEC-BODY "Local bridge model"):

- **Conductance:** M = 0, G given.
- **Mass–spring–damper** (m, k\_b, r\_b): M = 1, Ω = √(k\_b/m), σ = r\_b/(2m), Φ = 1/√m, G = 0. k\_b = 0 is a free mass (Ω = 0); check that `compileOscillatorF64` accepts it or handle it explicitly, and document.
- **Modal set:** built from a `pme::LocalBridge` (body.hpp: Ω\_k, σ\_k, Φ\_k(x\_b), G\_hf,b), with the oscillator coefficients compiled at the engine's fs (SPEC-BODY "Contracts"). This is the interface B3's `buildLocalBridge` output plugs into.

Configuration (`configure`, non-audio, allocates) and the per-sample path (audio thread: `noexcept`, no allocation, lock, exception or I/O; Real-time rules 1–2) are separate. Provide the discrete energy of the bridge model (SPEC-OSC "Discrete energy", mass-weighted: Σ ½ (u\_k/Δt)² + ½ Ω̂²\_k r\_k r\_k' per unit modal mass) and the conductance's dissipated power, in float64, for energy tests.

**Explicit coupling and stability.** The SPEC-BRG order is explicit (the bridge advances with Fⁿ before the strings). If a configuration you test is unstable or shows growing energy (for example a light bridge mass against many strings), do not add damping or change the order: record the parameters and the measured growth in the report; the planner files a spec issue.

**A4-R3.** Any number of segments (speaking `AtL` and rear or duplex `At0`) attach to one `BridgePoint`; all share w\_b (SPEC-BRG "Unison coupling"). Forces are summed in segment-index order.

**A4-R4.** R(θ\_b) as in SPEC-BRG "Polarisation coupling"; the ⊥ axis is a pure conductance G\_h = `BridgePointDesc::conductanceH`, w\_⊥ⁿ⁺¹ = w\_⊥ⁿ + Δt G\_h F\_⊥ⁿ. θ\_b = 0 decouples the polarisations exactly (test it bit-for-bit against two independent single-axis runs).

**A4-R5.** Hooks, both inert by default:

- External motion (SPEC-BODY "Sympathetic path"): `setExternalVelocity(S vExt)` per sub-block or per sample; w\_extⁿ⁺¹ = w\_extⁿ + Δt v\_extⁿ is added on the ∥ axis in step 2 (SPEC-EXEC step 3, "add the integrated external motion w\_ext").
- Longitudinal and tension terms (SPEC-GEO "Transmission to the body"; SPEC-EXEC step 2): an extra vertical force per segment, F\_b,v += κ\_L F\_L + κ\_T ΔT, for example `addVerticalForce(S f)`. A4 only provides and tests the hook (a constant extra force moves the bridge as predicted); A7 computes F\_L and ΔT.

## Tests that must pass

- **ORC-06** (Bridge damping), **ORC-07** (Unison coupling): written by the A4 test-author (docs/agents/briefs/A4-test-author.md), not by you. Make them possible: `compileBridgeTerms`, `bridgeForce`, `BridgePoint` and its conductance model must be reachable from `pme_bridge` through your internal headers. Do not write tests named `ORC-06` or `ORC-07`.
- **A4-T1** strings plus a lossless mass–spring bridge show no secular energy growth over 10⁶ samples and relative deviation below 10⁻⁶;
- **A4-T2** a mass-like bridge shifts partials as Δω ≈ −2 f₀ Z₀ Im Y within 2% for Z₀|Y| < 0.01;
- **A4-T3** against P0.5, two strings on a mass–spring–damper bridge match partial frequencies within 0.05 cent and decay rates within 2% — written by the A4 test-author, not by you.

### How each test is built

**A4-T1** (`engine/tests/bridge/`, doctest `A4-T1 …`, label `unit` or `integration`): two lossless strings (b₁ = b₂ = b₃ = 0; for example 262 Hz and 330 Hz plain steel, 40–60 modes each) on one lossless mass–spring bridge (r\_b = 0, G = 0, G\_h = 0, θ\_b = 0 and a second case with θ\_b = 0.1 rad), started from a small initial displacement, run 10⁶ samples at 48 kHz with **double** states. Total energy = Σ segments ρA Σ\_n H\_n (SPEC-OSC discrete energy of each mode) + bridge-mode energy + the coupling energy your derivation of the partitioned scheme requires; derive and document it in docs/dev/bridge.md. Pass: max |E − E₀|/E₀ < 10⁻⁶ and no secular trend (a least-squares slope over the run below 10⁻⁶/E₀ per 10⁶ samples, or an equivalent stated criterion). If the partitioned scheme only conserves a modified energy, use that one and say so. Report the measured deviation.

**A4-T2**: one plain string on a pure mass bridge (k\_b = 0, r\_b = 0, G = 0), lossless. Under SPEC-CONV's e^(+iωt) convention (SI-0012) Y(ω) = 1/(iωm), so −2 f₀ Z₀ Im Y(ω\_n) = 2 f₀ Z₀/(ω\_n m) > 0 (a mass-like bridge raises the partials). Choose m so that Z₀/(ω₁ m) < 0.01 for every compared partial. Measure the partial frequencies of the coupled discrete system from a long lossless run in double states (for example 20 s at 48 kHz) with `pme_analysis` partial tracking, and the same string on a rigid bridge the same way; the shift is the difference (no new dependency: an eigen-solver is not available, do not add one). Compare it with Δω\_n = 2 f₀ Z₀/(ω\_n m). Check first that the estimator resolves the shift: its frequency error on the rigid-bridge run against the compiled ω\_d,n must be below 0.2% of the predicted shift. Pass: |Δω\_measured/Δω\_predicted − 1| ≤ 0.02 for partials 1 … 10. Also report the shift predicted by the exact end-mass equation cot(kL) = m k/ρA (SPEC-BRG / P0.5-R3 cross-check).

### Other tests (unit label unless stated)

- `A4-R1 …`: g\_n for both ends against the formulas (10⁻¹² relative in float64, float32 correctly rounded); a\_n uses `oscV[n].omegaHat2` (and the horizontal vector `oscH[n].omegaHat2`); invalid inputs return `InvalidArgument` with `out` unchanged; `bridgeForce` equals a float64 dot product within float32 round-off; the static term −(T/L) w\_b.
- `A4-R2 …`: each model's free response (conductance: w grows as Δt G Σ F; mass–spring: frequency and decay of the bridge mode alone; modal set: equals the sum of its modes); the coupling order (an instrumented run records add-force, advance, read-acceleration in that order).
- `A4-R3 …`: three segments (two `AtL`, one `At0` rear segment compiled with `applyMute(modes, m_d, 20.0, params.detuneH)`) share one w\_b; the result does not depend on attaching order beyond the documented fixed summation order.
- `A4-R4 …`: θ\_b = 0 separates the polarisations bit-for-bit; θ\_b ≠ 0 transfers energy from a vertically started string into the horizontal polarisation.
- `A4-R5 …`: a constant v\_ext moves w\_∥ by Δt v\_ext per sample; a constant extra vertical force on a mass–spring bridge gives the static deflection F/k\_b.
- RT-safety: the per-sample path of a `BridgePoint` with three segments runs under `pme_rtcheck` without allocation or blocking (label as in engine/tests/osc and engine/tests/nonlinear).
- Benchmark `engine/tests/bridge/bridge_bench.cpp` (`pme_add_benchmark`, label `perf`): 3 segments × 60 modes on one bridge point with a 16-mode modal set, 48 kHz. No baseline file (D-4, V2); report the numbers.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to A4:

- Module note: docs/dev/bridge.md — SPEC-BRG sections implemented (and the SPEC-BODY, SPEC-EXEC, SPEC-GEO parts touched); the internal API (`bridgeForce`, the horizontal coefficients, `BridgePoint`, the three model kinds and how B3's `LocalBridge` plugs in, the two hooks); the energy expression of A4-T1; the stability observations; the e^(+iωt) convention used in A4-T2.
- `engine/tests/interfaces/test_stubs.cpp`: replace only the `compileBridgeTerms` SUBCASE with one that checks `InvalidArgument` for empty modes leaves `out` unchanged. In docs/dev/interfaces.md move `bridge` out of the stub list as the ownership table says.
- Coverage: `engine/src/bridge` is part of the 90% engine/src line-coverage gate.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `A4: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- Run before you finish: `clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug` on your C++ files; `ctest --preset linux-debug` with GCC and with Clang; the rt-safety preset's tests; `uv run pytest tools/tests/test_p0_3_r1.py` (test names).
- If something cannot be done as written (a tolerance not reached, an instability, SPEC unclear), stop that item, do not loosen a tolerance, and describe it in the report with the measured values; the planner files a spec issue.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, compilers, presets, the A4-T1 deviation and slope, the A4-T2 per-partial ratios and the end-mass comparison, any instability found, benchmark numbers.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `A4`, `role` `implementer`, `model` `sonnet`; `requirements` A4-R1 to A4-R5; `tests` A4-T1 and A4-T2 (ORC-06, ORC-07 and A4-T3 are the test-author's); every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0010, SI-0012 as relevant plus any new defect; `open_risks` as found.
