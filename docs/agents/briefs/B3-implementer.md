# Brief: B3 Radiation, mobility and local bridge builder — implementer

Status: **ready** (B2 complete). Start from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief. B3 needs no interfaces-v3 field, so it does **not** wait for ICR-0002.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/B3-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

radiation weights, radiation damping, mobilities, high-frequency conductance and the per-point local bridge models.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (all of it, including "Quality settings and numerical options (decision D-13)", Numerics rules, Testing policy, Licensing); the phase card **B3 Radiation, mobility and local bridge builder**; for context **B2** (your input), **B4** (imported tables you must also accept), **A4** and **A9** (consume `LocalBridge` through `modalSetModel`), **C1** (compileInstrument: applies your integer-delay function) and **V3** (confirms f\_split).
- SPEC and PLAN sections:
  - SPEC-BODY — docs/plan/engine-specification.md, "Soundboard and bodies": "Modal data (offline output)", "Local bridge model (per bridge point, per sample, exact)", "Global body (shared, per sub-block)" step 3, "Radiation weights (offline)", "Small bodies", "Contracts"
  - SPEC-CONV — "Scope, conventions and notation": "Sign conventions" (time convention e^(+iωt), SI-0012), "Quality and numerical options", float64 for compile-time quantities
  - SPEC-INST — "Piano defaults" (M\_b ≤ 64, f\_split = 2 kHz provisional: B3-T2 measures it), "Instrument file"
  - SPEC-API — "Engine interfaces" (`buildLocalBridge`, `LocalBridge`, `BodyModes`)
- engine/include/pme/body.hpp (`BodyModes`, `LocalBridge`, `buildLocalBridge`, `ChannelDesc`) — frozen at interfaces-v2.
- docs/dev/body-compiler.md (B2: API, radiation grid, blob layout 1, materials library), `compiler/pme_compiler/body/{compile,eigen,fem,modal_blob}.py`, `compiler/pme_compiler/materials/library.py`.
- docs/dev/body-import.md (B4: tables without a radiation grid), docs/dev/bridge.md (A4: `modalSetModel(const LocalBridge&, ParallelModel&)` and its input checks), docs/dev/instrument-format.md, docs/dev/interfaces.md, docs/dev/testing.md, docs/dev/traceability.md.
- Spec issues: **SI-0022** (this phase's conventions: air constants, delay reference, integer delay, radiating sides, G\_hf band edge, `residualMobility`, mean, selection; planner defaults in effect), SI-0012 (time convention), SI-0002 (sample-rate independence of `buildLocalBridge`), SI-0003 (f\_split default), SI-0013.
- ICR-0002 (approved, not applied; nothing in it concerns B3). docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs:
  - **B2** (complete 2026-10-12; CI run 38286118033 at c7c6533): `pme_compiler.body.compile.compile_plate` returning `Compiled` (`table: BodyModalTable`, `modes: eigen.Modes` with `omega` and M-orthonormal `vectors[:, k]`, `system: fem.System` with `basis`, `mesh`, `mass`, `stiffness`), `compile.sample_matrix(basis, points)` (P × ndofs, Argyris interpolation), `compile.quadrature_matrix(basis)` (G × ndofs evaluation matrix, points, weights of the basis's quadrature rule); `BodyModalTable.grid_points` / `grid_weights` / `grid_phi` (order-2 element quadrature by default, `--grid-order`); `modal_blob.validate/write/read/attach`; the CLI `python -m pme_compiler.body`. Blob layout 1 already defines `sigmaRad` [K], `alpha` and `beta` [C, K], `channelDelay` [C], `residualMobility` [P]: you fill them, you do not change `modal_blob.py`.
  - **B2's suggestion for B3**, adopted: work from `Compiled.modes.vectors` and `Compiled.system.basis` when you have the `Compiled` object (you can then build a finer quadrature with `quadrature_matrix` on a higher-order basis), and from `table.grid_points/grid_weights/grid_phi` when you only have a blob.
  - **Materials library contract** (B2): `Material.loss_factor` and `library.apply` raise `ValueError` for the tonewoods unless the caller passes `loss_factor` explicitly (the Wood Handbook gives no loss factor); `apply` emits `UnsourcedMoistureSlopeWarning` when moisture differs from MC\_ref and a slope is unsourced. Your tests that build a plate from the library pass `loss_factor` explicitly and either keep `moisture` = 0.12 or assert the warning (`pytest.warns`); never silence it globally.
  - **B4** (complete, e4f895b): imported modal tables may lack the grid (no radiation possible) or already carry `alpha`, `beta`, `channelDelay`.
  - **B1**: `models.ChannelDesc` (name, position), `models.Defaults.localBridgeModes` (64) and `localBridgeFSplit` (2000 Hz), `load(..., verify_blobs=True)`; the C++ loader.
  - **A4** (complete): `pme::bridge::modalSetModel` (engine/src/bridge/include/pme/bridge/model.hpp) accepts a `LocalBridge` whose `omega`, `sigma` are finite and ≥ 0, `phi` finite, `conductanceHf` finite and ≥ 0. Your output must pass it (test that, read only).
- Frozen interfaces: **interfaces-v2 = 300b995**. If `interfacesVersionMajor` is not 2 when you start, stop and report. B3 uses only `BodyModes`, `LocalBridge`, `buildLocalBridge`, which ICR-0002 does not change; if interfaces-v3 is applied while you are still working, rebase and carry on.
- Current state of the C++ module: `engine/src/body/body_stub.cpp` holds the stubs of `compileBody` and `buildLocalBridge`. `compileBody` stays a stub (the offline compile is Python: B2 and B3).

## Concurrent work: A5, A7, A8, A9 run in engine/src at the same time

A5, A7, A8 and A9 (with their test-authors) run in separate worktrees under the ownership table in their briefs (docs/agents/briefs/A5-implementer.md, "Concurrent work"). B3 is added to that set. B6 is briefed but waits for ICR-0002, which the orchestrator applies **after A5, A7, A8 and A9 merge**. Edits must not overlap.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `compiler/pme_compiler/radiation/**`, `compiler/tests/test_b3_*.py`, `compiler/tests/data/b3/**`, `docs/dev/radiation.md` | B3 implementer | read only |
| New files `engine/src/body/local_bridge*.cpp` and `engine/src/body/include/pme/body/local_bridge*.hpp`; in `engine/src/body/body_stub.cpp` only the removal of the `buildLocalBridge` stub; `engine/tests/body/**`; in `engine/tests/interfaces/test_stubs.cpp` the `buildLocalBridge` SUBCASE only | B3 implementer | read only |
| `compiler/tests/test_b3_review_*.py` | B3 code-reviewer | read only |
| `engine/src/exciters/**`, `engine/src/simd/**`, `engine/src/bridge/**`, new `longitudinal*` / `tension*` files in `engine/src/string` and `engine/src/nonlinear`, their tests and module notes | A5, A8, A9, A7 | read only (B3 reads `engine/src/bridge/include/pme/bridge/model.hpp` only) |
| `compiler/pme_compiler/body/**` (including `modal_blob.py`), `compiler/pme_compiler/materials/**`, `compiler/pme_compiler/importers/**`, `compiler/pme_compiler/schema/**`, `engine/src/runtime/loader/**`, `instruments/examples/**`, `reference/**` and their tests and notes | B2, B4, B1, P0.5 (complete) | read only; import their public Python APIs |
| `engine/include/pme/**`, `engine/tests/interfaces/**` (except your SUBCASE), `tools/params/**`, `params.json` | nobody (frozen at interfaces-v2) | read only |
| `pyproject.toml`, `uv.lock`, `.github/workflows/**`, `tools/ci/**`, `tools/trace/**`, `tools/tests/**`, `CMakePresets.json`, `cmake/**`, `.clang-format`, `.clang-tidy`, `README.md`, `LICENSE`, `CLAUDE.md`, `.claude/` | nobody in this phase | read only. B3 needs no new dependency (NumPy, SciPy, scikit-fem are present). |
| `docs/plan/`, `docs/adr/`, `docs/gates/`, `docs/icr/`, `docs/spec-issues/`, `docs/status/`, `docs/agents/` | planner | never edit (Sonnet agents) |

**Shared files: minimal, delimited, append-only edits.** A5, A7, A8 append to the same two files; whoever merges later rebases. Keep each block self-contained.

| Shared file | B3 |
| --- | --- |
| Root `CMakeLists.txt` | one block, appended after the last line (below) |
| `docs/dev/interfaces.md`, the sentence under "Stubs (P0.4-R3)" | add "`body`: `buildLocalBridge` implemented by B3; `compileBody` stays a stub (bodies are compiled offline in Python, B2 and B3)" next to the other "implemented by" notes; change nothing else |

```cmake
# ---- B3: local bridge builder ----
target_include_directories(pme_body PUBLIC "${PROJECT_SOURCE_DIR}/engine/src/body/include")
add_subdirectory(engine/tests/body)
# ---- end B3 ----
```

(`pme_body` is created by `pme_add_module` from `engine/src/body/*.cpp`, so a new `local_bridge.cpp` is compiled automatically. Model `engine/tests/body/CMakeLists.txt` on `engine/tests/bridge/CMakeLists.txt`; the B3-R5 test that calls `modalSetModel` links `pme_bridge` as a test dependency only, never `pme_body`.)

**Rules for every agent in these phases.**

- **Test names** start with a requirement or test ID followed by a space (C++ doctest: `TEST_CASE("B3-R5 …")`) or `test_<id>_` (pytest: `test_b3_r1_…`, `test_b3_t2_…`), and contain none of `,` `*` `?` `\` `"` `;`. Check with `uv run pytest tools/tests/test_p0_3_r1.py`. **Every requirement ID and every test ID of the card needs at least one test whose name starts with that ID** (the traceability job, docs/dev/traceability.md, fails a complete phase that lacks one). B3-R6 included: see below.
- Stage files explicitly (`git add <path>`); never `git add -A` or `-u`.
- **Disk space is limited.** Build one CMake preset at a time; delete the previous build directory (`rm -rf build/<preset>`) before configuring another; never keep more than two build trees. Do not commit compiled blobs larger than 1 MB (generate them in the test into `tmp_path`); delete scratch programs, meshes and blobs from your scratchpad when done.
- **Engine and instrument options (D-13).** Where SPEC or SI-0022 offers alternatives, implement each behind an option and never pick one silently. B3's options are listed under "D-13 options in B3". A choice that only changes accuracy (quadrature order, integration method) is not an option; pick it, verify it, document it.
- If something cannot be done as written (a tolerance not reached, SPEC unclear, a criterion that fails on a realistic body), stop that item. Do not loosen a tolerance or invent data. Report the measured values; the planner files or updates a spec issue.

## Scope

- May edit (create or change): the paths the ownership table gives the **B3 implementer**, plus your root `CMakeLists.txt` block and your `docs/dev/interfaces.md` words.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. B3-R1 Rayleigh-integral complex gains A\_c,k per mode and channel; α, β and integer delays τ\_c exactly as SPEC-BODY.
2. B3-R2 Radiation damping σ\_k,rad from each mode's radiated power.
3. B3-R3 Driving-point and transfer mobilities at every bridge point.
4. B3-R4 G\_hf,b as the mean real part of the residual mobility over \[f\_split, f\_max\].
5. B3-R5 `buildLocalBridge`: the M\_b modes with largest Φ\_k(x\_b)² below f\_split, M\_b ≤ 64.
6. B3-R6 MAY: boundary-element radiation for lid and case effects.

### Conventions (SI-0022 defaults, in effect)

| Item | Rule |
| --- | --- |
| Air | ρ₀ = 1.204 kg/m³, c₀ = 343.2 m/s; keyword arguments `rho0`, `c0` and CLI flags `--rho0`, `--c0` |
| Time convention | e^(+iωt) (SI-0012): outgoing wave e^(i(ωt − kr)), d/dt ↦ iω, Y\_mass = 1/(iωm) |
| Delay reference | R\_c = ‖R\_c‖, distance from the body-frame origin; τ\_c = ‖R\_c‖ / c₀ in seconds in `channelDelay` |
| Integer delay at fs | n\_c = ⌊τ\_c fs⌋, δ\_c = τ\_c − n\_c / fs, A′\_c,k = A\_c,k e^(−iΩ\_k δ\_c), α′ = Re A′, β′ = −Im A′ |
| G\_hf band | ω₁ = 2π f\_split, ω₂ = 2π f\_max with f\_max = max\_k Ω\_k / 2π; G\_hf,b = 0 if f\_split ≥ f\_max; arithmetic mean over linear frequency, exact per-mode integral |
| Selection | kept modes have Ω\_k / 2π < f\_split strictly; rank by Φ\_k(x\_b)² descending, ties to the lower k; `modeIndex` ascending |
| `residualMobility` blob array | G\_hf,b for the instrument's `defaults.localBridgeModes` and `defaults.localBridgeFSplit`; `buildLocalBridge` recomputes for its own arguments |

### D-13 options in B3

1. **Radiating sides** (`sides="front"` default, or `"both"`; CLI `--sides`). SI-0022 item 4, physics pending owner review, so implement both and default to the SPEC formula as written:
   - `front`: one baffled face; every channel must have z > 0 (else `ValueError` naming `channels[c].position`); σ\_k,rad is the power radiated into z > 0.
   - `both`: channels with z > 0 hear the front face as above; channels with z < 0 hear the back face, whose pressure has the opposite sign (A\_c,k computed with −Φ\_k; the distances |R\_c − x| are unchanged because the plate lies at z = 0); σ\_k,rad is twice the one-face value. z = 0 is rejected in both modes.
   It is an instrument option by nature (a piano board radiates from both faces, a guitar top from one), but there is no instrument-file field yet and the blob manifest has no place for it. Print the value used in the CLI summary, state it in the module note, and do not touch the schema; a later ICR adds the field (SI-0022).
2. **Radiation model** (`model="rayleigh"` default; `"bem"` is B3-R6). See B3-R6.

### How to meet each requirement

Package `compiler/pme_compiler/radiation/` (Python 3.12, NumPy, SciPy; ruff, ruff format and mypy strict clean; float64 throughout; every function implementing a SPEC formula cites it, `# SPEC-BODY "Radiation weights"`). Suggested modules: `rayleigh.py`, `power.py`, `mobility.py`, `local_bridge.py`, `cli.py`, `__main__.py`. Importing `pme_compiler.radiation` must not import Gmsh or scikit-fem (only the paths that take a `Compiled` object may).

**B3-R1 Radiation weights.**

- `radiation_gains(grid_points, grid_weights, grid_phi, omega, channels, *, rho0, c0, sides) -> (A: complex [C, K], tau: float [C])` implementing
  A\_c,k = (ρ₀ / 2π) Σ\_g w\_g Φ\_k(x\_g) e^(−iΩ\_k |R\_c − x\_g| / c₀) / |R\_c − x\_g| · e^(+iΩ\_k ‖R\_c‖ / c₀),
  then α = Re A, β = −Im A (units kg^(1/2)/m², layout 1). Vectorise over g and chunk over k so memory stays below about 1 GB.
- **Accuracy check, not an option.** The quadrature is accurate when the grid resolves both the acoustic wavelength 2πc₀/Ω\_k and the distance to the channel. Compute, per channel, the smallest distance d\_c from R\_c to the plate and the largest grid spacing; if the element size exceeds d\_c / 2 or the acoustic wavelength at the highest mode / 6, raise a `RadiationAccuracyWarning` naming the channel and the numbers. With a `Compiled` object, offer `quadrature_order=` to rebuild a finer rule with `compile.quadrature_matrix` on a basis of higher integration order over the same mesh (from `Compiled.system.basis` and `Compiled.modes.vectors`).
- `fold_integer_delay(alpha, beta, omega, tau, fs) -> (alpha_fs, beta_fs, n_samples)` implementing the integer-delay row of the conventions table. C1 calls it in compileInstrument; you test it (B3-T3).
- Report, per channel, the largest residual delay max\_g |(|R\_c − x\_g| − ‖R\_c‖)| / c₀ in the CLI summary.

**B3-R2 Radiation damping.** Definition (one face, derived from the Rayleigh integral; state the derivation in docs/dev/radiation.md):

  σ\_k,rad = (ρ₀ Ω\_k / 4π) ∫∫ Φ\_k(x) Φ\_k(y) sin(k\_k |x − y|) / |x − y| dA\_x dA\_y, k\_k = Ω\_k / c₀, the kernel's value at x = y being k\_k,

which is W\_k / (2E\_k) for the radiated power W\_k of mode k at unit modal amplitude and its energy E\_k = ½ Ω\_k² (mass normalisation). Low-frequency limit: σ\_k,rad → ρ₀ Ω\_k² (∫ Φ\_k dA)² / (4π c₀). For a uniform plate it equals ρ₀ c₀ σ\_eff,k / (2ρh), with σ\_eff,k the mode's radiation efficiency.

- Evaluate it on the grid. The kernel is smooth, so you may aggregate the grid onto acoustic cells no larger than λ\_ac,k / 8 (summing w\_g Φ\_k(x\_g) per cell, which integrates Φ\_k exactly over the cell) before the double sum; or use the equivalent far-field (hemisphere) integral of |A|². Choose one, prove it against the other on a small case, document the choice, its cost and the compile time.
- `sides="both"` doubles it. Cross-modal radiation coupling (off-diagonal terms) is neglected, as SPEC's "each mode's radiated power" implies; say so in the module note.
- Output: `sigmaRad` [K] and `sigma` rewritten as σ\_k = ½ η(Ω\_k) Ω\_k + σ\_k,rad. Recompute idempotently: if the input table already has `sigmaRad`, subtract it first (`sigma_new = sigma − sigma_rad_old + sigma_rad_new`), so running B3 twice gives the same blob.

**B3-R3 Mobilities.** `mobility(omega, sigma, phi, freqs_hz, *, rows=None, cols=None) -> complex [F, P, P]` with

  Y\_ab(ω) = Σ\_k Φ\_k(x\_a) Φ\_k(x\_b) iω / (Ω\_k² − ω² + 2iσ\_k ω)   (velocity at a per force at b, s/kg, e^(+iωt)),

driving point a = b, transfer a ≠ b, for every bridge point. Works on any `BodyModalTable` (B2 or B4). Provide a CLI subcommand or option that writes the driving-point mobilities of every bridge point to CSV for inspection (frequency grid stated).

**B3-R4 G\_hf,b.** `conductance_hf(omega, sigma, phi_b, kept, f_split) -> float` per the conventions table, with the exact per-mode integral of Re Y\_k. A closed form (verify it yourself before use): with p± = iσ ± √(Ω² − σ²) (principal root; overdamped modes give two imaginary roots),

  ∫ Φ² iω / (Ω² − ω² + 2iσω) dω = −iΦ² / (p₊ − p₋) · [p₊ Log(ω − p₊) − p₋ Log(ω − p₋)],

whose real part between ω₁ and ω₂ is the integral of Re Y\_k. For real ω and σ > 0 the arguments ω − p± stay in the lower half-plane, so the principal Log has no branch crossing. Handle the double root (σ = Ω, use the limit form) and σ = 0 (the integral of Re Y\_k is π Φ²/2 if ω₁ < Ω < ω₂, π Φ²/4 at an edge, else 0) explicitly. Check against adaptive quadrature (`scipy.integrate.quad` with breakpoints at the Ω\_k) to 10⁻⁸ relative and against ∫₀^∞ Re Y\_k dω = πΦ²/2. Write `residualMobility[b]` for the instrument's defaults (conventions table).

Sanity check from SPEC-BODY (not the value to use): for a large uniform plate and a point far from the edges, G\_hf,b approaches 1/(8√(D\_eff ρh)), D\_eff = √(D₁₁ D₂₂). Report the ratio for one such case.

**B3-R5 `buildLocalBridge`.** Two implementations of one algorithm:

- Python reference `local_bridge.build(table_or_modes, point, max_modes, f_split) -> LocalBridgeData` (same fields as the C++ struct).
- C++ `pme::buildLocalBridge` in `engine/src/body/local_bridge.cpp` (namespace `pme`; internal helpers in `engine/src/body/include/pme/body/local_bridge.hpp`, namespace `pme::body`), replacing the stub. Contract (body.hpp, frozen):
  - `InvalidArgument` and `out` unchanged if `point` is out of range, `maxModes < 0`, `fSplit` not finite or ≤ 0, or the body's arrays are inconsistent (`omega.size() != numModes`, `sigma.size() != numModes`, `phi.size() != numModes * numPoints`) or contain a non-finite value, a negative Ω or a negative σ.
  - Selection per the conventions table; M\_b = min(maxModes, number of modes below f\_split). `maxModes` > 64 is accepted (SPEC: "M\_b ≤ 64 SHOULD hold"); the instrument default is 64.
  - Fills `point`, `modeIndex` (ascending), `omega`, `sigma`, `phi` of the kept modes, `conductanceHf` = G\_hf,b over the modes **not** kept (including modes below f\_split that lost the ranking, as SPEC-BODY's "full mobility minus the kept modes" says), and `fSplit`.
  - Non-audio: may allocate; float64 throughout. No `compileBody` change.
- Cross-language parity: commit a small fixture generated by the Python reference (≤ 200 modes, a few bridge points, JSON under `engine/tests/body/data/`, with the generator script under `compiler/tests/data/b3/`), and a C++ test that reproduces `modeIndex` exactly and `conductanceHf` within 10⁻¹² relative. Test also that the output passes `pme::bridge::modalSetModel`.
- Update the `buildLocalBridge` SUBCASE of `engine/tests/interfaces/test_stubs.cpp`: it can no longer expect `NotImplemented`; make it check `InvalidArgument` for the empty `BodyModes{}` with point 0 (and `out.point` unchanged), and nothing else in that file.

**B3-R6 (MAY) Boundary-element radiation.** Under D-13 a viable alternative is implemented behind an option, but viability is not established: a BEM solver means a new dependency (licence, wheels for Windows, macOS and Linux, no GPU or OpenCL requirement in CI) and an ADR. In this phase:

- Add the option `model="rayleigh" | "bem"` to the radiation API and CLI (`--model`); `"bem"` raises `NotImplementedError("B3-R6 boundary-element radiation: deferred pending ADR")`. Test: `test_b3_r6_bem_option_is_explicitly_deferred`.
- Write a feasibility note in docs/dev/radiation.md, "Boundary-element option": candidate libraries (for example bempp-cl), licence, installation on the three systems, what a lid or case model would need as input. Do not add a dependency.
- Report B3-R6 as `deferred` with that reason; the planner writes the ADR and, if viable, a follow-up phase. SPEC's lighter alternative (a measured or simulated correction filter per channel) is run-time work for Track C, not B3.

**Command line.** `python -m pme_compiler.radiation <compiled-instrument.json> -o <out-dir> [--rho0 --c0 --sides front|both --model rayleigh --quadrature-order N --force]` reads a compiled instrument (a `ModalTable` body with a grid), computes `sigmaRad`, rewrites `sigma`, writes `alpha`, `beta`, `channelDelay` for the instrument's `channels` and `residualMobility` for its `bridgePoints`, writes a new blob with `modal_blob.write(..., tool="pme_compiler.radiation")` and attaches it with `modal_blob.attach(..., source=<the input's source>)`; the output must load with `load(..., verify_blobs=True)` and with the C++ loader. A table without a grid: compute only mobility and `residualMobility`, keep existing `alpha`/`beta`/`channelDelay`, and print why radiation was skipped (exit 0); an instrument with channels but no grid and no `alpha` exits 2 with that message. An input that already has `alpha` is recomputed only with `--force`.

## Tests that must pass

- **B3-T1** a uniformly moving baffled rectangle matches the analytic baffled-piston on-axis pressure within 1%;
- **B3-T2** local bridge mobility within 0.5 dB of the full modal mobility below f\_split, and mean real part within 10% above it;
- **B3-T3** the α, β real-time sum equals the complex gain at each mode's frequency.

### How each test is built

pytest under `compiler/tests/test_b3_*.py`; C++ doctest under `engine/tests/body/`. Mark slow cases; the whole B3 Python suite stays under 5 minutes per system (state each test's runtime in the report).

- **B3-T1** (`test_b3_t1_…`). A rectangle of half-sides a = 0.2 m, b = 0.15 m centred on the z axis (outline from (−a, −b) to (a, b)), meshed by B2 (`compile_plate` or the mesh and `quadrature_matrix` path) with element size ≤ min(λ\_ac / 6 at the highest frequency, z\_min / 2); uniform normal velocity U on the grid, pressure from your Rayleigh-integral code (the same kernel as `radiation_gains`). Reference: the exact on-axis pressure, reduced to one angular integral by integrating the Rayleigh integral in polar coordinates about the axis,
  p(z) = (ρ₀ c₀ U / 2π) ∫₀^(2π) [e^(−ikz) − e^(−ik√(z² + ρ\_m(θ)²))] dθ, ρ\_m(θ) = min(a / |cos θ|, b / |sin θ|),
  evaluated with `scipy.integrate.quad` (breakpoints at the corner angles) to 10⁻¹⁰. State the derivation in the test docstring; also check that the same formula with ρ\_m = r₀ gives the classical circular piston ρ₀c₀U[e^(−ikz) − e^(−ik√(z² + r₀²))] (Kinsler et al., *Fundamentals of Acoustics*, 4th ed., §7.4; or Pierce, *Acoustics*, §5.7) and that the far field tends to iωρ₀US e^(−ikz)/(2πz). Cases: z ∈ {0.1, 0.5, 2, 10} m, f ∈ {100, 500, 1000, 2000, 4000} Hz. Pass: |p − p\_ref| / |p\_ref| ≤ 0.01 at each case; on-axis points where |p\_ref| is below 0.1 of its maximum over z at that frequency (near an on-axis null) are listed in the report and compared with |p − p\_ref| ≤ 0.01 × max\_z |p\_ref| instead.
- **B3-T2** (`test_b3_t2_…`, and a C++ `TEST_CASE("B3-T2 …")` on the parity fixture). Y\_full,b = all modes; Y\_loc,b = kept modes + G\_hf,b (the conductance is a real mobility at every frequency, as in SPEC-BODY's w\_b,R). Bodies: (1) a plate compiled by B2 with at least 150 modes and at least 80 modes below f\_split = 2 kHz at some bridge point, so the M\_b = 64 ranking matters; (2) the compiled `test-guitar-6`; (3) the compiled `generic-grand-88` at the highest `--f-max` the time budget allows (state it; if f\_max < f\_split, say that the "above" half is vacuous for it). For every bridge point:
  - below f\_split: |20 log₁₀(|Y\_loc,b| / |Y\_full,b|)| ≤ 0.5 dB at every point of a grid that is uniform with spacing ≤ 1 Hz and refined to ±5σ\_k with spacing ≤ σ\_k / (4π) Hz around every Ω\_k / 2π below f\_split;
  - above: |mean Re Y\_loc,b / mean Re Y\_full,b − 1| ≤ 0.10, means over \[f\_split, f\_max\] computed by adaptive quadrature (not with the closed form used to build G\_hf,b, so the check is independent).
  Report, per body and point, M\_b, the number of modes below f\_split, the worst dB error and the frequency where it occurs, and the error in third-octave bands. **f\_split = 2 kHz is provisional (SPEC-INST): this test is the measurement V3 uses.** If the pointwise 0.5 dB criterion fails on a realistic body (for example at the resonance of a mode that lost the ranking), do not change f\_split, M\_b or the metric: report the measured values and the smallest f\_split for which it passes; the planner files the spec issue.
- **B3-T3** (`test_b3_t3_…`). For a compiled plate with K ≥ 40 modes and two channels (one near, one far; with `sides="both"` also one channel at z < 0): drive each mode alone, undamped, r\_k(t) = cos(Ω\_k t). Real-time sum s\_c(t) = α\_c,k r̈\_k + β\_c,k Ω\_k ṙ\_k (exact derivatives), delayed by τ\_c. Direct reference: the Rayleigh pressure of the plate moving as w = Φ\_k cos(Ω\_k t), each grid point with its own retarded time |R\_c − x\_g| / c₀. Pass: max over t of |s\_c(t − τ\_c) − p\_c(t)| ≤ 10⁻⁹ × max\_t |p\_c(t)| for every k and c (float64). Repeat with `fold_integer_delay` at fs = 44 100 and 48 000 Hz (delay n\_c / fs, gains α′, β′): same bound. Also show in the phasor form that α(−Ω²) + βΩ(iΩ) = −Ω² A for every k and c.
- Further tests (named by requirement):
  - `test_b3_r1_…`: reciprocity/symmetry (a channel mirrored about a symmetry axis of a symmetric plate gives equal |A| for symmetric modes); `front` rejects z ≤ 0; `both` gives the antiphase back face; the accuracy warning fires on a coarse grid; `fold_integer_delay` gives 0 ≤ δ\_c < 1/fs.
  - `test_b3_r2_…`: (a) the low-frequency limit ρ₀Ω²(∫Φ dA)²/(4πc₀) for the (1,1) mode of a simply supported plate at k a ≤ 0.05 within 1%; (b) radiation efficiencies of simply supported modes (1,1), (1,2), (2,2), (3,1) at three frequencies against an independent computation in the test (far-field hemisphere integral of the closed-form Fourier transform of sin(mπx/L\_x) sin(nπy/L\_y)), within 2%; where you can consult Wallace, "Radiation resistance of a rectangular panel", JASA 51 (1972) 946–952, cite the values you compare; never transcribe a value you have not read; (c) the two-face option doubles σ\_rad; (d) idempotent rewrite of `sigma`.
  - `test_b3_r3_…`: Y\_ab = Y\_ba; a single mode reduces to Φ²iω/(Ω² − ω² + 2iσω); a free mass (Ω = 0, σ = 0) gives 1/(iωm) with m = 1/Φ² (SI-0012 sign); Re Y\_aa ≥ 0.
  - `test_b3_r4_…`: closed form against adaptive quadrature; σ = 0 and critically damped modes; f\_split ≥ f\_max gives 0; the infinite-plate sanity ratio (reported; assert it lies in [0.75, 1.33] for the stated large-plate case, a planner sanity bound, not a card criterion).
  - `TEST_CASE("B3-R5 …")`: argument errors leave `out` unchanged; ties go to the lower index; strict f\_split boundary; maxModes = 0 keeps no mode and puts everything in the conductance; parity with the Python fixture; output accepted by `modalSetModel`.
  - `test_b3_r5_…`: the Python reference on the same cases; the CLI round trip (`load(verify_blobs=True)` of the radiated `test-monochord` and `test-guitar-6`; `residualMobility` equals `build(...).conductance_hf` for the instrument's defaults within 10⁻⁹).

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to B3: `buildLocalBridge` runs off the audio thread, so no RT-safety test and no performance baseline; report its run time for 2,000 modes and 64 kept modes. The engine coverage rule (≥ 90% of engine/src lines) applies to `local_bridge.cpp`. ruff, ruff format, mypy strict, clang-format and clang-tidy clean. B3-R6 is deferred pending the planner's ADR (see above). Module note **docs/dev/radiation.md**: SPEC-BODY sections implemented; the SI-0022 conventions; the derivations of A\_c,k, σ\_k,rad and the G\_hf closed form; the D-13 options (`sides`, `model`) with defaults; accuracy rules of the grid; mobility conventions; the CLI; the parity fixture; runtimes; the BEM feasibility note.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `B3: ` and ending with the attribution lines your session provides. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- Run before you finish: `uv run ruff check`, `uv run ruff format --check`, `uv run mypy`, `uv run pytest compiler/tests`, `uv run pytest tools/tests/test_p0_3_r1.py`; configure and build one debug preset, run `ctest -R "B3-|P0.4-R3"`, then delete that build tree; then build the coverage preset alone if disk allows and report the line coverage of `local_bridge.cpp`.
- If something cannot be done as written, stop that item and report it with measured values; never loosen a tolerance or change a convention silently.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, per-test worst errors and runtimes, the σ\_rad method chosen and its cross-check, B3-T2's per-body table, the radiated example instruments' compile times, the BEM feasibility summary.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `B3`, `role` `implementer`, `model` `sonnet`; `requirements` B3-R1 to B3-R6 (R6 `deferred`); `tests` B3-T1, B3-T2, B3-T3; every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0022 plus any new defect; `open_risks` as found (compile time of σ\_rad for a 2,000-mode soundboard, B3-T2 on the piano body, grid size).
