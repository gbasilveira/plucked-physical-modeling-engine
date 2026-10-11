# Brief: B2 Plate and body FEM compiler — implementer

Status: **ready (pending CI on A2/A3/B1)**. B1 is approved by review and waits only for one fully green pr.yml run; start now from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief. If B1's CI run forces a change in `compiler/pme_compiler/schema/`, rebase onto it.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/B2-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

computes mass-normalised body modes from geometry and materials, as SPEC-BODY defines.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Languages and dependencies, Numerics rules, Testing policy, Licensing); the phase card **B2 Plate and body FEM compiler**; for context **B1** (your input), **B3** (consumes your modes and radiation grid), **B4** (shares the blob format) and **E1** (fits).
- SPEC and PLAN sections:
  - SPEC-BODY — docs/plan/engine-specification.md, "Soundboard and bodies": "Plate model (offline)" including the body frame, geometry inputs and the SI-0013 moisture law; "Modal data (offline output)"; "Contracts"
  - SPEC-CONV — "Scope, conventions and notation" (units, float64 for compile-time quantities)
  - SPEC-INST — "Instrument definitions" ("Instrument file") for the blob manifest
- docs/adr/ADR-0002-scikit-fem.md (scikit-fem with Gmsh; element choice; Gmsh licence handling), ADR-0003 (dependency pinning).
- docs/dev/instrument-format.md (B1: `PlateParams`, `BodyParams`, `RibDesc`, `BridgeBeamDesc`, blob manifest, cross-field rules) and the Python API in `compiler/pme_compiler/schema/` (`models`, `load`, `dump`).
- docs/spec-issues/SI-0003 (body geometry inputs), SI-0013 (moisture reference, MC\_ref = 0.12).
- docs/dev/reference-solver.md (the P0.5 plate: simply supported rectangle, spruce-like constants, closed-form `plate_scheme_frequency_hz`; B2-T4 is the test-author's).
- docs/dev/testing.md; docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **B1** (merged at eebf763, reviewer tests 2d06ec1, 1b057f7; approved, pending CI): `compiler/pme_compiler/schema/` (`models.PlateParams`, `BodyParams`, `BlobEntry`, `InstrumentFile`, `load`, `dump`), `instruments/examples/` (test-monochord, test-unison-pair, generic-grand-88, test-guitar-6), docs/dev/instrument-format.md. **P0.5** (merged at 103badf) for B2-T4, used by the B2 test-author.
- Frozen interfaces: **interfaces-v2 = 300b995** (`PlateParams`, `BodyParams`, `BodyModes` in engine/include/pme/body.hpp; the Python models mirror them). No `moistureRef` field yet (SI-0013: next ICR); use MC\_ref = 0.12.
- Python dependencies: `scikit-fem`, `gmsh`, `meshio` are in the `fem` extra of pyproject.toml and in uv.lock; CI's python job installs `--all-extras`. Add nothing unless unavoidable (then follow the shared-file plan).
- Known gaps from the B1 review, for your awareness (B1 owns them): a plate `outline` has no minimum vertex count in the schema (treat fewer than 3 vertices as invalid input in B2); for a non-`PlateModel` source the plate block holds zeros. The B1 examples set `rimStiffness` = 0 (free edge), so they are **not** simply supported.
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

## Body modal-table blob, layout 1 (shared by B2, B3, B4 and the C++ side later)

Planner decision for B2-R7 and B4; B2 implements it in `compiler/pme_compiler/body/modal_blob.py` as its **first commit** (merged before B4's blob-writing commits), B4 imports it, B3 extends it with the optional radiation arrays. One blob file per body, raw little-endian **float64**, row-major, arrays at 64-byte-aligned offsets, listed in the instrument file's `blobs` manifest (docs/dev/instrument-format.md, "Blob manifest"). Modes are sorted by ascending Ω. K modes, P bridge points (the instrument's `bridgePoints` order), C channels (the instrument's `channels` order), G radiation-grid points.

| Array `name` | Shape | `unit` | Required | Meaning (SPEC-BODY) |
| --- | --- | --- | --- | --- |
| `omega` | [K] | `rad/s` | yes | Ω\_k |
| `sigma` | [K] | `1/s` | yes | σ\_k so far: ½ η(Ω\_k) Ω\_k from B2 (B3 adds σ\_k,rad and rewrites it); measured or fitted values from B4 |
| `phi` | [K, P] | `kg^(-1/2)` | yes | Φ\_k(x\_b), mass-normalised (∫ρh Φ\_k Φ\_l dA = δ\_kl); `phi[k, b]` = `BodyModes::phi[k * numPoints + b]` |
| `points` | [P, 3] | `m` | yes | Body-frame positions the shapes were sampled at (must equal the instrument's bridge-point positions) |
| `gridPoints` | [G, 3] | `m` | with grid | Radiation-grid points on the plate (z = 0) |
| `gridWeights` | [G] | `m^2` | with grid | Quadrature area of each grid point |
| `gridPhi` | [K, G] | `kg^(-1/2)` | with grid | Φ\_k on the grid |
| `sigmaRad` | [K] | `1/s` | B3 | σ\_k,rad |
| `alpha`, `beta` | [C, K] | `kg^(1/2)/m^2` | B3 or table | α\_c,k, β\_c,k (`BodyModes::alpha[c * K + k]`) |
| `channelDelay` | [C] | `s` | with `alpha` | τ\_c |
| `residualMobility` | [P] | `s/kg` | B3 | mean residual mobility per bridge point |

`gridPoints`, `gridWeights`, `gridPhi` appear together or not at all; so do `alpha`, `beta`, `channelDelay`. Units outside the B1 x-unit vocabulary (`rad/s`, `kg^(-1/2)`, `m^2`, `kg^(1/2)/m^2`) are allowed in blob arrays (the manifest's `unit` is free text) and are listed in the module note. `producer` = `{"tool": "pme_compiler.body" | "pme_compiler.importers", "version": pme_compiler.__version__}`. Default file path `blobs/body-modal-table.bin` relative to the instrument file.

API (names fixed so B4 can code against it before B2 merges):

```python
@dataclass(frozen=True)
class BodyModalTable:            # numpy float64 arrays, shapes as above; optional ones may be None
    omega: NDArray[np.float64]; sigma: ...; phi: ...; points: ...
    grid_points: ... | None = None; grid_weights: ... | None = None; grid_phi: ... | None = None
    sigma_rad: ... | None = None; alpha: ... | None = None; beta: ... | None = None
    channel_delay: ... | None = None; residual_mobility: ... | None = None

def validate(table: BodyModalTable) -> list[str]: ...          # empty list = valid (shapes, finiteness, sorted omega, omega >= 0, sigma >= 0, groups complete)
def write(table: BodyModalTable, directory: Path, relpath: str, tool: str) -> models.BlobEntry: ...
def read(entry: models.BlobEntry, directory: Path) -> BodyModalTable: ...
def attach(file: models.InstrumentFile, entry: models.BlobEntry, source: str) -> models.InstrumentFile: ...
    # returns a copy with the entry in `blobs`, body.source = source ("ModalTable" or "MeasuredMobilityFit"),
    # body.modalTableHash = entry.hash; the plate block is kept (provenance)
```

A compiled plate is written with `source: "ModalTable"`: B1's cross-field rule requires `modalTableHash` to be empty for `PlateModel`, so the compiled form of a plate body is a modal table whose `plate` block records what it was compiled from. Every written instrument must load with `pme_compiler.schema.load(..., verify_blobs=True)` and with the C++ loader.

## Scope

- May edit (create or change): the paths the ownership table gives the **B2 implementer**.
- Read only: everything else.
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. B2-R1 Geometry input: outline polygon, thickness map, grain angle, ribs (position, section, wood), bridges (polyline, section), rim support stiffness.
2. B2-R2 Material library with orthotropic constants, density, loss factor η(f) and moisture coefficients for common tonewoods; every entry records its source.
3. B2-R3 Meshing with Gmsh; Kirchhoff plate elements in scikit-fem; ribs and bridges as local heterogeneities; MAY add Reissner–Mindlin elements.
4. B2-R4 Optional static prestress for crown and downbearing.
5. B2-R5 Sparse generalised eigen-solve up to f\_max; mass normalisation; mode shapes at bridge points and on the radiation grid.
6. B2-R6 Decay rates from η(Ω\_k), with radiation damping added by B3.
7. B2-R7 Output as a body blob described by the B1 manifest.

### How to meet each requirement

Package `compiler/pme_compiler/body/` (FEM, blob) and `compiler/pme_compiler/materials/` (data). Python 3.12, NumPy, SciPy, scikit-fem, Gmsh; ruff and mypy strict clean; every function that implements a SPEC formula cites it in a comment (`# SPEC-BODY …`). All arithmetic float64.

**B2-R1 Geometry.** Read `models.PlateParams` (SI-0003): outline polygon in the body frame (empty = the `lengthX` × `lengthY` rectangle; fewer than 3 vertices, self-intersection or zero area is invalid input), thickness h on the regular `thicknessNx` × `thicknessNy` grid over the outline's bounding box with bilinear interpolation (empty = uniform `thickness`), grain angle θ\_g, ribs (`RibDesc`: straight beam, rectangular section width × height, E along the beam, density), bridges (`BridgeBeamDesc`: polyline, rectangular section, E, density), rim support k\_rim (N/m², translational per unit length) and k\_rim,θ (rotational). Raise a `ValueError` naming the field for invalid input. Also accept, for tests and comparisons only, an explicit boundary condition argument (`"elastic"` default from k\_rim, `"simply_supported"`, `"clamped"`); the instrument file always means `"elastic"`.

**B2-R2 Materials.** A library of common tonewoods (at least: Sitka spruce, Norway/European spruce, Engelmann spruce, western red cedar, maple, mahogany, Indian rosewood, ebony is optional) with E\_L, E\_R, G\_LR, ν\_LR, ν\_RL, ρ at MC\_ref = 0.12, loss factor η (constant, or η(f) as a short table), and linear moisture slopes dE\_L/dMC, dE\_R/dMC, dG\_LR/dMC, dρ/dMC at MC\_ref (SI-0013). **Every entry records its source**: author, title, year, table or page, and how each value was derived (for example "E\_R = E\_L × 0.078, Wood Handbook FPL-GTR-282 (2021), Table 5-1"). Values come only from a cited published source you have actually consulted; never invent or estimate a number; omit a species or a field you cannot source and list the omission in the report. The USDA Forest Products Laboratory Wood Handbook is a US government work (public domain) and is a suitable primary source; record each source's licence or status for H1. Store the library as data (JSON or a Python module) with a loader that fills `PlateParams` fields; the instrument file remains the single source of truth for a compiled body.

**B2-R3 FEM.** Mesh the outline with Gmsh (Python API; ADR-0002: Gmsh is called as a separately installed library, never vendored or bundled), assemble the orthotropic Kirchhoff plate of SPEC-BODY in the grain frame rotated by θ\_g, with D₁₁, D₂₂, D₁₂, D₆₆ from SPEC-BODY and h(x, y), in scikit-fem. Recommended element: `ElementTriArgyris` (ADR-0002); record the choice and the reason in the module note. Ribs and bridges as local heterogeneities, as the card says: either beam elements attached along their axes (ADR-0002's suggestion) or locally increased bending stiffness and mass along a strip of the beam's width; say which, and verify that a rib adds the stiffness E I and mass ρA of its section. Rim support: translational and rotational boundary springs per unit length (`"elastic"`), or Dirichlet conditions for `"simply_supported"` (w = 0) and `"clamped"` (w = ∂w/∂n = 0). Reissner–Mindlin is optional.

**B2-R4 Prestress.** An optional argument (not in the instrument file: there is no field; a candidate for a later ICR) giving uniform in-plane resultants N\_x, N\_y, N\_xy (N/m) in the body frame, added as the geometric stiffness ∫ (N\_x w\_x² + 2N\_xy w\_x w\_y + N\_y w\_y²) dA to the stiffness matrix. Test: a simply supported isotropic rectangle under uniform tension N matches the analytic ω²\_mn = [D (k\_m² + k\_n²)² + N (k\_m² + k\_n²)]/(ρh) within 0.5% for the first 10 modes.

**B2-R5 Eigen-solve.** Sparse generalised eigenproblem K φ = Ω² M φ for every mode with Ω/2π ≤ f\_max (`PlateParams::fMax`), with SciPy (`eigsh` in shift-invert mode, split into frequency windows if needed). Free or weakly supported plates have rigid-body modes: drop modes with Ω/2π below 1 Hz (or 10⁻⁶ of the largest kept Ω) and report how many. Mass-normalise with the full mass matrix (plate plus ribs and bridges): ∫ρh Φ\_k Φ\_l dA = δ\_kl. Interpolate Φ\_k at the bridge points (the instrument's `bridgePoints`, body frame) with the element's own basis, and on a radiation grid (`gridPoints` with quadrature weights `gridWeights` covering the plate area; for example the mesh's quadrature points or nodes with lumped areas; document which and why it suits B3's Rayleigh integral).

**B2-R6 Decay.** σ\_k = ½ η(Ω\_k) Ω\_k with η = `PlateParams::lossFactor` (or the library's η(f) when the caller supplies one). Radiation damping σ\_k,rad is B3's (it adds `sigmaRad` and rewrites `sigma`).

**Moisture (SI-0013).** E\_L(MC) = E\_L + dE\_L/dMC (MC − MC\_ref), likewise E\_R, G\_LR, ρ, with MC = `PlateParams::moisture` and MC\_ref = 0.12 (a module constant until the next ICR adds `PlateParams::moistureRef`). Reject a combination that makes any modulus or the density non-positive.

**B2-R7 Output.** Write the body modal-table blob (layout above) with `modal_blob.write`, attach it with `modal_blob.attach(…, source="ModalTable")`, and provide a CLI `python -m pme_compiler.body <instrument.json> -o <out-dir>` that writes the compiled instrument file and its blob. Round trip: `load(out, verify_blobs=True)` succeeds and `modal_blob.read` returns the arrays bit-identically. Do not change `compiler/pme_compiler/schema/`.

## Tests that must pass

- **B2-T1** simply supported orthotropic rectangle: first 20 frequencies within 0.5% of the analytic values;
- **B2-T2** clamped rectangle against published reference values within 1%;
- **B2-T3** mass normalisation within 10⁻⁶;
- **B2-T4** a ribbed rectangle against the P0.5 plate: first 20 modes within 2% — written by the B2 test-author (docs/agents/briefs/B2-test-author.md), not by you;
- **B2-T5** a moisture change shifts frequencies in the direction and by the amount the coefficients predict, within 1%.

### How each test is built

All pytest under `compiler/tests/test_b2_*.py`, names `test_b2_t1_…` and so on (B2-R tests `test_b2_r3_…`). Mark slow cases so the python job stays within its time budget (state the runtime of each test in the report; keep the whole B2 suite under 5 minutes per system).

- **B2-T1**: the P0.5 plate (docs/dev/reference-solver.md: E\_L = 10 GPa, E\_R = 0.5 GPa, G\_LR = 0.6 GPa, ν\_LR = 0.37, ν\_RL = ν\_LR E\_R/E\_L, ρ = 400 kg/m³, h = 3 mm, 0.6 m × 0.4 m, grain along x), `"simply_supported"`. Analytic: ω²\_mn = [D₁₁ k\_m⁴ + 2(D₁₂ + 2D₆₆) k\_m² k\_n² + D₂₂ k\_n⁴]/(ρh), k\_m = mπ/L\_x, k\_n = nπ/L\_y, computed in the test from SPEC-BODY's D formulas. Pass: the first 20 FEM frequencies (sorted) each within 0.5% of the 20 lowest analytic values; state the mesh size used and show convergence on two meshes.
- **B2-T2**: an isotropic clamped square (or rectangle) with published frequency parameters λ = ωa²√(ρh/D), from Leissa, *Vibration of Plates*, NASA SP-160 (1969) (for the clamped square λ₁ ≈ 35.99) or another cited source; cite table and values in the test. Pass: each compared mode (at least the first 6) within 1%.
- **B2-T3**: for B2-T1's plate and for a ribbed plate with a thickness grid, check Φᵀ M Φ = I to 10⁻⁶ (max absolute entry of the difference), and the bridge-point interpolation against the assembled field at mesh nodes.
- **B2-T5**: the B2-T1 plate with non-zero moisture slopes from the materials library (or stated values with their source); MC from 0.12 to 0.16. Predicted: for a simply supported orthotropic plate each ω\_mn² scales with the D's and 1/ρ, so compute the predicted ratio ω\_mn(0.16)/ω\_mn(0.12) from the SI-0013 law in the test. Pass: the FEM ratio matches the predicted ratio within 1% of the predicted frequency change (|Δf\_FEM/Δf\_pred − 1| ≤ 0.01) for the first 10 modes, and the sign is right.
- Other tests: `test_b2_r1_…` invalid geometry rejected with the field named; thickness-grid interpolation; a rib adds its E I and mass (compare a plate with one rib against a beam-plate analytic or a refined-mesh result, stated); `test_b2_r2_…` every library entry has a source and loads into `PlateParams`; `test_b2_r4_…` the prestress analytic case above; `test_b2_r5_…` rigid-body modes dropped for a free plate; `test_b2_r7_…` blob round trip and `load(verify_blobs=True)` of the compiled test-monochord and test-guitar-6.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to B2: no engine/src or audio-thread code, so no RT-safety test, engine coverage or performance baseline; ruff, ruff format and mypy strict clean. Module note docs/dev/body-compiler.md: SPEC-BODY sections implemented; element and heterogeneity choices; boundary options; rigid-body mode rule; radiation grid; the moisture law (SI-0013, MC\_ref = 0.12); the blob layout (units outside the B1 vocabulary listed); the CLI; the materials library and its sources; test runtimes.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `B2: ` and ending with the attribution lines your session provides. **First commit: `compiler/pme_compiler/body/modal_blob.py` with its tests**, so the orchestrator can merge it before B4's blob code. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- Run before you finish: `uv run ruff check`, `uv run ruff format --check`, `uv run mypy`, `uv run pytest compiler/tests` and `uv run pytest tools/tests/test_p0_3_r1.py`; check that `import gmsh` works in a clean `uv sync --locked --all-extras` environment on your system. If Gmsh needs system libraries on a CI runner (for example OpenGL libraries on Linux), report it; do not edit workflows.
- If something cannot be done as written (a tolerance not reached, a source not found, SPEC unclear), stop that item, do not loosen a tolerance or invent data, and describe it in the report; the planner files a spec issue.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, per-test worst errors and runtimes, mesh sizes, the element choice, the number of dropped rigid-body modes, library entries and omissions.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `B2`, `role` `implementer`, `model` `sonnet`; `requirements` B2-R1 to B2-R7; `tests` B2-T1, B2-T2, B2-T3, B2-T5 (B2-T4 is the test-author's); every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0003, SI-0013 as relevant plus any new defect; `open_risks` as found (including Gmsh licence and CI install observations).
