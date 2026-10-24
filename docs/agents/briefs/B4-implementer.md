# Brief: B4 Modal data import — implementer

Status: **ready (pending CI on A2/A3/B1)**. B1 is approved by review and waits only for one fully green pr.yml run; start now from `ccr-5ef35d73-oqb6fd` at or after the commit that added this brief. Your blob-writing code needs B2's first commit (`compiler/pme_compiler/body/modal_blob.py`); until the orchestrator has merged it, code against the API in "Body modal-table blob" below and keep the blob commits last.

Model: sonnet
Branch: whatever branch your worktree is on (the orchestrator merges it into `ccr-5ef35d73-oqb6fd`); do not create `phase/B4-<slug>`, do not push, do not tag, do not open a pull request
Written by: planner (Opus), from the phase card in docs/plan/phasing-plan.md

## Objective

routes for bodies that do not come from the plate compiler: MAESSTRO output, published or measured modal tables, mobility fits, membranes and air cavities.

## Read first

- docs/plan/phasing-plan.md: **Software guidelines** (Languages and dependencies, Licensing, Testing policy); the phase card **B4 Modal data import** (Outputs amended 2026-10-11 to `compiler/pme_compiler/importers/`: `import` is a Python keyword); for context **B1**, **B2** (shares the blob format), **B3**, **E5** to **E8** (guitar, sitar, tanpura, kantele, shamisen bodies use these routes).
- SPEC and PLAN sections:
  - SPEC-BODY — docs/plan/engine-specification.md, "Soundboard and bodies": "Modal data (offline output)" (Ω\_k, Φ\_k mass-normalised, σ\_k), "Radiation weights" (α, β from the complex gain A\_c,k), "Small bodies", "Contracts"
  - SPEC-CONV — "Scope, conventions and notation" (units; the e^(+iωt) time convention of SI-0012 for mobilities and complex gains)
  - SPEC-INST — "Instrument definitions" (instrument table: membrane body for shamisen, small bodies; "Instrument file")
- PLAN: docs/plan/implementation-plan.md references to MAESSTRO (soundboard modal bases).
- docs/dev/instrument-format.md (B1: `BodyParams::source` `ModalTable` and `MeasuredMobilityFit`, `modalTableHash`, the blob manifest and its checks) and the Python API in `compiler/pme_compiler/schema/`.
- docs/spec-issues/SI-0012 (time convention). docs/adr/ADR-0003 (dependency pinning).
- docs/dev/testing.md; docs/agents/report.schema.json.

## Inputs

- Completed dependencies and their outputs: **B1** (merged at eebf763, reviewer tests 2d06ec1, 1b057f7; approved, pending CI): `pme_compiler.schema` (`models`, `load`, `dump`), the blob manifest checks, `instruments/examples/test-guitar-6`. **B2** (concurrent): `pme_compiler.body.modal_blob` (its first commit).
- Frozen interfaces: **interfaces-v2 = 300b995** (`BodySource`, `BodyParams`, `BodyModes` in engine/include/pme/body.hpp).
- No MAESSTRO output file is in the repository, and no MAESSTRO run is available to you. See B4-R1 below.
## Concurrent work: A4, A6, B2 and B4 run at the same time

A4, A6, B2 and B4 (each with its test-author where briefed) run in separate git worktrees; the orchestrator merges their commits into `ccr-5ef35d73-oqb6fd`. A CI fixer and the D3 lv2lint follow-up may also be committing (`.github/workflows/pr.yml`, `docs/dev/ci.md`, `docs/dev/lv2.md`, `plugin/lv2/**`). Edits must not overlap. This table is identical in every A4, A6, B2 and B4 brief.

| Path | Owner | What the others may do |
| --- | --- | --- |
| `engine/src/bridge/**` (replaces `bridge_stub.cpp`), `engine/tests/bridge/**`, `docs/dev/bridge.md`; in `engine/tests/interfaces/test_stubs.cpp` the `compileBridgeTerms` SUBCASE only | A4 implementer | read only |
| `engine/tests/oracles/bridge/**`; in `engine/tests/oracles.md` the status and test-location cells of rows ORC-06, ORC-07 only | A4 test-author | read only |
| `engine/src/dampers/**` (replaces `dampers_placeholder.cpp`), `engine/tests/dampers/**`, `docs/dev/dampers.md` | A6 implementer | read only |
| `engine/tests/oracles/dampers/**`; in `engine/tests/oracles.md` the status and test-location cells of row ORC-13 only | A6 test-author | read only |
| `compiler/pme_compiler/body/**` (including the shared `body/modal_blob.py`), `compiler/pme_compiler/materials/**`, `compiler/tests/test_b2_*.py` except `test_b2_t4_*.py` and `test_b2_r5_reference_*.py`, `compiler/tests/data/b2/**`, `docs/dev/body-compiler.md` | B2 implementer | B4 imports `pme_compiler.body.modal_blob` (read only) |
| `compiler/tests/test_b2_t4_*.py`, `compiler/tests/test_b2_r5_reference_*.py`, `compiler/tests/data/b2_t4/**`; in `.github/workflows/pr.yml` the one `uv run pytest tools/tests/test_p0_5_*.py …` line of the `reference-python` job (append its two test-file globs, nothing else) | B2 test-author | read only |
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

- May edit (create or change): the paths the ownership table gives the **B4 implementer**; at most one dependency line in an existing extra of `pyproject.toml` plus the regenerated `uv.lock`, in their own commit, only if unavoidable (shared-file plan).
- Read only: everything else (including `compiler/pme_compiler/body/`: import it, do not change it; if `modal_blob` needs a change, describe it in the report).
- Never edit (Sonnet agents): docs/plan/, docs/adr/, docs/gates/, docs/icr/, docs/spec-issues/, docs/status/, docs/agents/

## Requirements

1. B4-R1 Adapter for MAESSTRO modal bases.
2. B4-R2 Modal table import (CSV: frequency, damping, shapes at bridge points, radiation gains).
3. B4-R3 Rational (vector) fitting of measured mobilities into modal tables.
4. B4-R4 Air-cavity (Helmholtz) modes for guitar-type bodies as fitted body modes.
5. B4-R5 Analytic circular and rectangular membrane modes for shamisen-type bodies, from tension and areal density.

### How to meet each requirement

Package `compiler/pme_compiler/importers/` (one module per route), Python 3.12, NumPy and SciPy; ruff and mypy strict clean; every function implementing a SPEC or textbook formula cites it in a comment. All arithmetic float64. Every route returns a `BodyModalTable` (layout 1) and a helper writes it with `modal_blob.write` and `modal_blob.attach`. Provide a CLI `python -m pme_compiler.importers <route> …` that writes the instrument file and blob.

**Conventions.** Mobility (velocity per force) and complex radiation gains use e^(+iωt) (SPEC-CONV, SI-0012). A modal table's shapes are mass-normalised (∫ρh Φ\_k Φ\_l dA = δ\_kl, unit kg^(-1/2)); with them the transfer mobility between points a and b is

Y\_ab(ω) = Σ\_k iω Φ\_k(a) Φ\_k(b) / (Ω\_k² − ω² + 2iσ\_k ω).

**B4-R1 MAESSTRO.** MAESSTRO (open-source soundboard modal-basis toolchain; PLAN references hal-03409789) writes modal bases (frequencies, damping or loss, mode shapes on a mesh). Establish its output format from MAESSTRO's public documentation or source (read only; do not copy its code; record its licence for H1). Write an adapter that reads that format, samples or interpolates the shapes at the instrument's bridge points, checks or performs mass normalisation, and returns a `BodyModalTable` with `source: ModalTable`. For B4-T3, create a **small synthetic fixture in the documented format** under `compiler/tests/data/b4/` with a generator script next to it, clearly labelled synthetic (MIT, ours); do not commit third-party MAESSTRO data. If you cannot establish the format from public material, implement the adapter against the closest documented description you can cite, mark B4-R1 `partial`, and say exactly what sample is needed; the orchestrator asks the owner for one.

**B4-R2 CSV.** One header row; columns in SI units with the unit in the name:

- `frequency_hz` (Ω/2π), and exactly one of `sigma_per_s` (σ) or `loss_factor` (η, σ = ηΩ/2) or `q_factor` (σ = Ω/(2Q));
- `phi_<b>` for every bridge point b = 0 … P−1 (mass-normalised, kg^(-1/2));
- optional `alpha_<c>` and `beta_<c>` for every channel c (both or neither for all channels), plus a separate small CSV or a CLI argument for `channel_delay_s`.

Reject unknown, duplicate or missing columns, non-finite values, negative σ, unsorted or duplicate frequencies, and a P or C that does not match the instrument; errors name the row and column. Rows are sorted by frequency on output.

**B4-R3 Vector fitting.** Implement rational fitting of measured mobilities (Gustavsen and Semlyen, "Rational approximation of frequency domain responses by vector fitting", IEEE Trans. Power Delivery 14(3), 1999; cite it), own implementation in NumPy/SciPy (no new dependency unless unavoidable): complex-conjugate pole pairs p = −σ ± iω\_d with relaxation, fitting a driving-point mobility and optionally transfer mobilities with common poles. Convert each pole pair and its residues to Ω\_k = √(ω\_d² + σ²), σ\_k and the products Φ\_k(a)Φ\_k(b) of the formula above (driving point: Φ\_k(b)² from the residue; signs of the other points from the transfer residues). Output `source: MeasuredMobilityFit`. Inputs: frequency vector (Hz) and complex mobility arrays (s/kg) from NumPy files or CSV (`frequency_hz`, `re_<a>_<b>`, `im_<a>_<b>`); fit band, model order and stopping rule as arguments; report the fit's RMS relative error.

**B4-R4 Helmholtz air cavity.** For guitar-type bodies, an air-cavity mode added as a fitted body mode: f\_H = (c₀/2π) √(S/(V L\_eff)) with cavity volume V, sound-hole area S and effective neck length L\_eff (default L\_eff = t + 1.7 r for a hole of radius r in a plate of thickness t, flanged-end correction; cite the source), c₀ = 343 m/s at 20 °C unless given; damping from a given Q; Φ\_H at the bridge points supplied by the caller from a mobility fit (B4-R3) or a measurement, because the Helmholtz frequency alone does not fix the coupling. Provide a function that merges such a mode into an existing table (sorted by Ω). The two-oscillator top-plate and cavity coupling is out of scope; say so in the module note.

**B4-R5 Membranes.** Circular membrane of radius a and rectangular membrane a × b, tension per unit length T\_m (N/m), areal density ρ\_s (kg/m²), wave speed c = √(T\_m/ρ\_s), fixed edge:

- circular: f\_mn = j\_m,n c/(2πa), j\_m,n the n-th positive zero of J\_m (`scipy.special.jn_zeros`); for m > 0 both the cos mθ and sin mθ modes; mass-normalised shapes Φ\_mn(r, θ) = J\_m(j\_m,n r/a) {cos mθ, sin mθ} / √(ρ\_s N\_mn) with N\_0n = π a² J₁(j\_0,n)² and N\_mn = (π a²/2) J\_m+1(j\_m,n)² for m > 0 (derive and test numerically);
- rectangular: f\_mn = (c/2) √((m/a)² + (n/b)²), Φ\_mn = (2/√(ρ\_s a b)) sin(mπx/a) sin(nπy/b).

Decay from a given loss factor or Q. Sample Φ at the bridge points (membrane frame: centre at the origin for the circle, corner at the origin for the rectangle; state it). The membrane is the body for shamisen-type instruments (SPEC-INST table); air loading is out of scope.

## Tests that must pass

- **B4-T1** vector fitting recovers synthetic modal parameters within 0.5% in frequency and 5% in damping;
- **B4-T2** membrane frequencies match Bessel-zero values within 10⁻⁹;
- **B4-T3** an imported MAESSTRO sample yields a valid blob.

### How each test is built

pytest under `compiler/tests/test_b4_*.py`, names `test_b4_t1_…` and so on (requirement tests `test_b4_r2_…`).

- **B4-T1**: synthesise a driving-point mobility and two transfer mobilities from 12 known modes (20 Hz to 2 kHz, σ from 1 to 40 s⁻¹, two pairs closer than 3% in frequency) with the formula above, on 4000 log-spaced points; cases without noise and with 1% complex Gaussian noise (fixed seed). Fit, match modes to the truth, and check |Ω\_fit/Ω − 1| ≤ 0.005 and |σ\_fit/σ − 1| ≤ 0.05 for every mode, and the Φ products within 5%. State the model order used.
- **B4-T2**: for m = 0 … 5 and n = 1 … 5, the circular-membrane frequencies equal j\_m,n c/(2πa) with j from `scipy.special.jn_zeros` to 10⁻⁹ relative, and independently from a root-finding of J\_m (`scipy.optimize.brentq` on `scipy.special.jv`) to 10⁻⁹; rectangular frequencies against the closed form to 10⁻¹². Also check mass normalisation of the shapes by 2-D quadrature (∫ρ\_s Φ² dA = 1 to 10⁻⁸) and orthogonality of the cos and sin pair.
- **B4-T3**: run the MAESSTRO adapter on the synthetic fixture, write the blob and the instrument file (based on `instruments/examples/test-guitar-6` or a minimal instrument), and check: `modal_blob.validate` returns no error; `pme_compiler.schema.load(path, verify_blobs=True)` succeeds; `modal_blob.read` returns the same arrays bit-identically; the frequencies and shapes equal the fixture's values (after any normalisation the adapter documents).
- Other tests: `test_b4_r2_…` a valid CSV round-trips into a table and blob; every rejection case of B4-R2 fails with row and column named; `test_b4_r4_…` the Helmholtz frequency for a stated geometry against a hand computation, and merging keeps the table sorted.

## Definition of done

- [ ] Every requirement ID of the card is implemented, or explicitly deferred by an approved ADR.
- [ ] Every test ID of the card exists under that name and passes in CI on Windows, macOS and Linux.
- [ ] Unit tests, RT-safety tests (for audio-thread code) and lint pass; coverage rule holds.
- [ ] docs/dev/\<module>.md updated, naming the SPEC sections implemented.
- [ ] No change to frozen interfaces outside an approved ICR.
- [ ] Performance baselines added or updated where the card touches engine/src.
- [ ] Reviewer approval recorded on the pull request.
- [ ] Handoff report submitted and consistent with CI results.

How these apply to B4: no engine/src or audio-thread code, so no RT-safety test, engine coverage or performance baseline; ruff, ruff format and mypy strict clean. Module note docs/dev/body-import.md: SPEC-BODY sections used; each route's input format, units and conventions (e^(+iωt), mass normalisation); the CSV columns; the vector-fitting algorithm, options and citation; the Helmholtz and membrane formulas with sources; the MAESSTRO format source and licence; what the synthetic fixture is.

## Working rules

- Work on the branch your worktree is on. Commit frequently, each message starting `B4: ` and ending with the attribution lines your session provides. Put any `pyproject.toml`/`uv.lock` change in its own commit. **Do not push. Do not tag. Do not open a pull request.**
- Do not stage or commit files outside your May-edit paths; stage them by name.
- Run before you finish: `uv run ruff check`, `uv run ruff format --check`, `uv run mypy`, `uv run pytest compiler/tests` and `uv run pytest tools/tests/test_p0_3_r1.py`.
- If something cannot be done as written (a tolerance not reached, a format not documented, SPEC unclear), stop that item, do not loosen a tolerance or invent data, and describe it in the report; the planner files a spec issue.

### Reporting rule

You do not push, so you cannot produce CI links. Report every test `not run` in `tests`, set `status` to `partial`, and put the local evidence in `notes`: commands, B4-T1 worst errors per case and the model order, B4-T2 worst errors, the MAESSTRO format source, test runtimes.

## Report

Return the handoff report as JSON matching docs/agents/report.schema.json, with `phase` `B4`, `role` `implementer`, `model` `sonnet`; `requirements` B4-R1 to B4-R5; `tests` B4-T1 to B4-T3; every created or changed path in `outputs`; `pull_requests` empty; `spec_issues` SI-0012 as relevant plus any new defect; `open_risks` as found (including the MAESSTRO sample and licence).
