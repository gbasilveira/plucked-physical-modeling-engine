# Plate and body FEM compiler (B2)

SPEC sections implemented: SPEC-BODY "Soundboard and bodies": "Plate model (offline)" (Kirchhoff form,
D coefficients, ribs and bridges as attached beams, elastically supported rim, moisture law of SI-0013
with MC\_ref = 0.12, optional static prestress), "Body frame", "Geometry inputs" (SI-0003), "Modal data
(offline output)" (Ω\_k, σ\_k = ½ η(Ω\_k) Ω\_k, Φ\_k at bridge points) and the offline half of "Contracts"
(`compileBody`). Also SPEC-CONV (float64 for compile-time quantities) and SPEC-INST "Instrument file"
(blob manifest). Radiation damping σ\_k,rad and the radiation weights α, β are B3's; B2 provides the
radiation grid they integrate over. ADR-0002 (scikit-fem with Gmsh) and ADR-0003 (pinning) apply.

| What | Where |
| --- | --- |
| Geometry validation, moisture law | `compiler/pme_compiler/body/geometry.py` |
| Gmsh meshing | `compiler/pme_compiler/body/mesh.py` |
| scikit-fem assembly (plate, beams, rim, prestress) | `compiler/pme_compiler/body/fem.py` |
| Windowed shift-invert eigen-solve, mass normalisation | `compiler/pme_compiler/body/eigen.py` |
| `compile_plate` (mesh, assemble, solve, sample) | `compiler/pme_compiler/body/compile.py` |
| Blob layout 1 (shared with B3 and B4) | `compiler/pme_compiler/body/modal_blob.py` |
| Command line | `python -m pme_compiler.body` (`cli.py`) |
| Materials library | `compiler/pme_compiler/materials/` (`library.json`, `library.py`) |
| Tests | `compiler/tests/test_b2_*.py` (`test_b2_common.py` holds shared builders, no tests) |

Nothing here is reachable from `Engine::process`: it is an offline Python tool. Importing
`pme_compiler.body` imports neither Gmsh nor scikit-fem; `compile`, `fem` and `mesh` do.

## Use

```python
from pme_compiler.body.compile import compile_plate
c = compile_plate(plate_params, [bp.position for bp in instrument.bridgePoints])
c.table.omega, c.table.sigma, c.table.phi    # BodyModalTable (float64), phi[k, b]
c.info                                       # modes, dropped rigid-body modes, dofs, seconds
```

```
python -m pme_compiler.body instrument.json -o out/ [--mesh-size M] [--f-max HZ] [--grid-order N] [--force]
```

The CLI reads an instrument whose `body.source` is `PlateModel` (`--force` recompiles one that already
has a modal table, from its plate block), compiles the body for the instrument's `bridgePoints`, and
writes `out/instrument.json` plus `out/blobs/body-modal-table.bin`. Other blobs of the input are copied
beside it. The output is re-loaded with `load(..., verify_blobs=True)` before the command succeeds;
exit code 2 on invalid input. The mesh size defaults to the smaller of a bending wavelength at f\_max
divided by 3.5 and a sixth of the shorter bounding-box side; `--f-max` overrides `plate.fMax` for the
compilation only (the plate block is written unchanged).

**A compiled plate is `source: ModalTable`** (orchestrator decision): B1's cross-field rule requires
`modalTableHash` to be empty for `PlateModel`, so the compiled form of a plate body is a modal table
whose `plate` block records what it was compiled from. Both loaders accept the result; the C++ loader
check (`loadInstrumentFile(..., verifyBlobs = true)`) was run on the compiled `test-monochord` and
`test-guitar-6`.

## Model and discretisation (B2-R1, R3)

**Plate.** The Kirchhoff form of SPEC-BODY in the grain frame (L = (cos θ\_g, sin θ\_g), R = (−sin θ\_g,
cos θ\_g)). The stiffness bilinear form is the energy
`D11 w_LL v_LL + D12 (w_LL v_RR + w_RR v_LL) + D22 w_RR v_RR + 4 D66 w_LR v_LR`, integrated with
h(x, y) inside the D's (D ∝ h³) and the mass form `ρ h w v`, where w\_LL, w\_RR, w\_LR are the Hessian
rotated into the grain frame. h(x, y) is uniform or bilinear on the Nx × Ny grid over the outline's
bounding box (row-major `[j * Nx + i]`, i along x).

**Element: `ElementTriArgyris`** (C¹, quintic, 21 degrees of freedom per triangle), as ADR-0002
recommends. Reason: the Kirchhoff energy needs H² conformity; Argyris is conforming, converges fast
(B2-T1 reaches 1.4 × 10⁻⁴ worst error on the first 20 modes with 168 triangles) and its vertex
derivative degrees of freedom couple to beams without extra constraints. The price is a stiffer
conditioning than Morley; Morley is not offered.

**Ribs and bridges: beam terms on mesh edges along the beam axes** (the "beam elements attached along
their axes" option of ADR-0002). Gmsh fragments each beam segment into the plate surface, so the mesh
has element edges on every axis (crossing ribs, bridges and the boundary are split correctly). On
those edges the bending form adds `E I (w_ss)^2` and the mass form `ρ A w²`, with `E I = E · width ·
height³ / 12` for out-of-plate bending of the rectangular section (height normal to the plate) and
`ρ A = ρ · width · height`, w\_ss the second derivative along the axis. w\_ss of an H² function is
single valued on an edge, so one side suffices. Verified: the assembled beam energy and mass of a
smooth field equal the analytic line integrals (`test_b2_r3_a_rib_adds_...`), and the total mass is
plate plus ρ A L of every beam to 10⁻⁹. Neglected, and stated as limitations: torsional stiffness of
the beam, rotary inertia, and the offset between beam and plate mid-surfaces (the beam acts at z = 0,
as SPEC-BODY places ribs and bridges at z = 0; a Kirchhoff plate has no in-plane degrees of freedom to
carry the composite action).

**Rim support.** `"elastic"` (the only meaning in an instrument file): a translational spring
`k_rim` (N/m², `rimStiffness`) and a rotational spring `k_rim,θ` (`rimRotationalStiffness`) per unit
rim length, `½ k w²` and `½ k\_θ (∂w/∂n)²` on the boundary facets. `"simply_supported"` and `"clamped"`
(tests and comparisons only) use the same two springs with a penalty stiffness of 10⁸ D\_max / L\_c³
and 10⁸ D\_max / L\_c (L\_c = √area): the relative frequency error of the constraint is of order 10⁻⁸,
below the discretisation error, and it works for any polygon, where Dirichlet conditions on Argyris
degrees of freedom are awkward for edges not aligned with the axes. The penalty makes the matrices
ill conditioned in a harmless way (sparse and dense solves agree to 10⁻⁶ on repeated modes). A free
plate (`rimStiffness` = 0) is allowed.

**Moisture (SI-0013).** `E_L(MC) = E_L + dE_L/dMC · (MC − MC_ref)`, likewise E\_R, G\_LR and ρ, with MC =
`plate.moisture` and MC\_ref = 0.12, the module constant `geometry.MC_REF` until the next ICR adds
`PlateParams::moistureRef`. A combination giving a non-positive modulus or density raises `ValueError`
naming the field. Poisson's ratios do not depend on MC.

**Prestress (B2-R4).** `compile_plate(..., prestress=Prestress(nx, ny, nxy))`: uniform in-plane
resultants (N/m, tension positive) add the geometric stiffness `∫ N_x w_x v_x + N_xy (w_x v_y + w_y v_x) +
N_y w_y v_y dA`. There is no instrument-file field for it (candidate for a later ICR). Crown and
downbearing as a non-uniform field are not modelled; compression beyond buckling raises `ValueError`.

**Input validation.** `ValueError` messages start with the field (`plate.outline`, `plate.ribs[1]`,
`plate.bridges[0].path`, `bridgePoints[2]`, ...): an outline with fewer than 3 vertices, zero area or
a self-intersection; a thickness grid of the wrong size; non-positive section or material values; a
rib or bridge whose end points leave the outline or whose segment crosses the boundary; a bridge point
outside the outline.

## Eigen-solve and sampling (B2-R5, R6)

`K φ = Ω² M φ` for every mode with Ω/2π ≤ f\_max. Systems with at most 400 degrees of freedom use a
dense solve; larger ones use SciPy `eigsh` in shift-invert mode over successive windows: the first
shift is at −(2π · 1 Hz)², each window returns the `nev` (40) eigenvalues nearest its shift, the next
shift is the upper end of the previous window (shrunk by 10⁻⁶ so that a degenerate pair cut by the
window edge is found by the next window), duplicates are removed. The start vector is fixed
(`default_rng`), so a compilation is deterministic (the blob bytes are identical between runs on one
machine; BLAS differences between systems are not excluded).

**Rigid-body modes.** Modes with Ω/2π < 1 Hz, or Ω below 10⁻⁶ of the largest kept Ω, are dropped and
counted (`info.n_rigid_dropped`): a free plate has exactly 3 (heave and two tilts; tested), a supported
one has none.

**Mass normalisation.** The eigenvectors are re-orthonormalised against the full mass matrix (plate,
ribs and bridges) with a Cholesky factor when the Gram matrix deviates from I by more than 10⁻¹³, so
`Φᵀ M Φ = I` holds to 10⁻¹⁵ in the tests (required: 10⁻⁶), also across window boundaries.

**Mode sign.** An eigenvector's sign is arbitrary and differs between LAPACK/ARPACK builds, so
`mass_normalise` (which the dense and the windowed path both end in) fixes it: each mode is multiplied
by ±1 so that its largest-magnitude component is positive; components within 10⁻⁹ (relative) of the
largest count as tied and the lowest index among them decides. Blob bytes therefore no longer depend
on the solver's sign choice (test `test_b2_r5_mode_sign_is_deterministic`).

**Bridge points.** Φ\_k(x\_b) is interpolated with the Argyris basis (`Basis.probes`); `phi[k, b]` =
`BodyModes::phi[k * numPoints + b]`. Bridge points must lie inside the outline. The values at mesh nodes
equal the nodal degrees of freedom of the field (tested).

**Radiation grid.** `gridPoints` / `gridWeights` / `gridPhi` are the element quadrature points of order
`--grid-order` (default 2: 3 points per triangle, weights summing to the polygon area to 10⁻¹²),
z = 0. Quadrature points suit B3's Rayleigh integral because the integrand
`Φ_k(x) e^{−iΩ|R−x|/c₀}/|R−x|` is smooth for a channel off the plate (z > 0), so a low-order rule per
element integrates it accurately when elements resolve the plate wavelength (they do, by the mesh
rule), the weights already carry the mesh's area, and no extra interpolation is needed. Order 10 is
exact for the product of two mode shapes (tested); the default is small because the array is
K × G × 8 bytes (for a soundboard with K = 2,500 and 15,000 elements, order 2 gives 45,000 points and
0.9 GB; use `--grid-order 0` to omit it, or a coarser mesh for the grid, if that is too large).

**Decay.** `sigma = ½ η Ω` with the constant `plate.lossFactor`, or `loss_factor=` a function of
frequency in Hz (the library's `Material.loss_factor`). `sigmaRad` is not written (B3).

## Blob layout 1

One blob per body, raw little-endian float64, row-major, arrays at 64-byte-aligned offsets, modes
ascending in Ω; listed in the instrument file's `blobs` (docs/dev/instrument-format.md, "Blob
manifest") with `producer` = `{"tool": "pme_compiler.body" | "pme_compiler.importers", "version": ...}`;
default path `blobs/body-modal-table.bin`. K modes, P bridge points, C channels, G grid points.

| Array | Shape | `unit` | Written by B2 | Meaning |
| --- | --- | --- | --- | --- |
| `omega` | [K] | `rad/s` | yes | Ω\_k |
| `sigma` | [K] | `1/s` | yes | σ\_k = ½ η Ω\_k (B3 adds σ\_k,rad and rewrites it) |
| `phi` | [K, P] | `kg^(-1/2)` | yes | Φ\_k(x\_b), ∫ρh Φ\_k Φ\_l dA = δ\_kl |
| `points` | [P, 3] | `m` | yes | the bridge-point positions sampled |
| `gridPoints` | [G, 3] | `m` | with grid | quadrature points, z = 0 |
| `gridWeights` | [G] | `m^2` | with grid | quadrature areas |
| `gridPhi` | [K, G] | `kg^(-1/2)` | with grid | Φ\_k on the grid |
| `sigmaRad` | [K] | `1/s` | B3 | σ\_k,rad |
| `alpha`, `beta` | [C, K] | `kg^(1/2)/m^2` | B3 or table | α\_c,k, β\_c,k |
| `channelDelay` | [C] | `s` | with `alpha` | τ\_c |
| `residualMobility` | [P] | `s/kg` | B3 | mean residual mobility per bridge point |

Units outside the B1 `x-unit` vocabulary are `rad/s`, `kg^(-1/2)`, `m^2` and `kg^(1/2)/m^2`; the
manifest `unit` is free text, so they are allowed in blob arrays. `gridPoints`, `gridWeights`,
`gridPhi` appear together or not at all; so do `alpha`, `beta`, `channelDelay`. API
(`modal_blob`): `BodyModalTable`, `validate(table) -> list[str]`, `write(table, directory, relpath,
tool) -> BlobEntry`, `read(entry, directory)`, `attach(file, entry, source)`; `read` returns the
written bytes bit-identically.

## Materials library (B2-R2)

`pme_compiler.materials.library` loads `library.json`: constants (E\_L, E\_R, G\_LR, ν\_LR, ν\_RL, ρ at
MC\_ref = 0.12), a constant loss factor or an η(f) table, the four moisture slopes, and a mandatory
`source` (author, title, year, locator, licence, and a `derivation` for every constant and for the
slopes). `validate_entry` rejects an entry without them (tested). `apply(material, plate,
loss_factor=None)` fills the wood fields of a `PlateParams`.

**Tonewoods.** Six entries come from the USDA Forest Products Laboratory Wood Handbook, FPL-GTR-282
(March 2021; US government work, public domain), read from the PDF: `sitka_spruce`,
`engelmann_spruce`, `western_redcedar`, `sugar_maple`, `african_mahogany` (Khaya spp.) and
`american_mahogany` (Swietenia macrophylla; the handbook's "Mahogany, Honduras" rows of Tables 5-1 and
5-2 are matched to Table 5-5a's "Mahogany, true" through Chapter 2, p. 2-29). Each entry's `source`
holds `raw` (the table numbers as printed), `derivation` (the arithmetic per field) and `locators`
(table, printed page and PDF page per field); `test_b2_r2_*` recomputes every value from `raw`.
Derivations: E\_L = 1.10 × the static-bending modulus at 12 % MC (Table 5-3a / 5-5a; the 10 %
shear-deflection correction is the handbook's own, p. 5-2 and Table 5-3a footnote c); E\_R and G\_LR =
Table 5-1 ratio × that E\_L; ν from Table 5-2 (ν\_RL of western red cedar by the reciprocity
relation Eq. 5-1, since the table is blank); ρ at 12 % from the tabulated G\_12 (or, for the Table 5-5a
species, from the green-volume specific gravity with Eqs. 4-9, 4-11, 4-14) as 1000 G\_12 × 1.12;
dE\_L/dMC and dρ/dMC are the analytic derivatives at MC = 0.12 of the handbook's moisture-adjustment
equation 5-3 (M\_p = 27 % for Sitka from Table 5-13, else the handbook's default 25 %) and of
Eqs. 4-9/4-11/4-14, converted to Pa and kg/m³ per unit MC fraction. All values are US-grown (Table
5-3a) or world-literature (Table 5-5a) averages for clear wood and carry the handbook's variability.

**What the handbook does not give, and the library therefore omits (see `omissions`).** Norway spruce,
Indian rosewood (no elastic ratios) and ebony; any loss factor (the handbook has only a range of the
logarithmic decrement, p. 5-21), so `constants.lossFactor` is `null`, `Material.loss_factor` and
`apply` raise `ValueError` unless the caller passes `loss_factor`; and the slopes dE\_R/dMC and
dG\_LR/dMC (null, applied as zero, listed in `Material.unsourced_slopes`). The entry
`p0_5_reference_plate` (`kind: test-reference`) holds the spruce-like constants stated in
docs/dev/reference-solver.md, with zero moisture slopes. The B2-T5 slopes are illustrative values
stated in the test, not wood data.

## Tests and runtimes

All pytest, `compiler/tests/test_b2_*.py`; B2-T4 and `test_b2_r5_reference_*` belong to the B2
test-author. See the handoff report for per-test runtimes; the whole B2 suite (204 tests with the B1 ones) runs in 60 to 100
seconds on the development container.

| ID | Test | Result |
| --- | --- | --- |
| B2-T1 | simply supported P0.5 plate, 20 modes, meshes 0.06 and 0.04 m | worst error 1.4 × 10⁻⁴ and 3.6 × 10⁻⁶ (limit 5 × 10⁻³) |
| B2-T2 | clamped square, Leissa λ | 35.985, 73.394, 73.394, 108.216, 131.581, 132.205 against 35.99, 73.41, 73.41, 108.27, 131.64, 132.24 (within 0.05 %; limit 1 %) |
| B2-T3 | Φᵀ M Φ − I, uniform and ribbed + thickness grid; bridge sampling | 9 × 10⁻¹⁶ and 6 × 10⁻¹⁶ (limit 10⁻⁶) |
| B2-T5 | MC 0.12 → 0.16 and 0.08, 10 modes | worst relative deviation of the frequency change from the prediction 1.7 × 10⁻⁶ for both (limit 10⁻²) |
| B2-R4 | uniform tension N = 2000 and 8000 N/m, 10 modes | worst error 1 × 10⁻⁵ (limit 5 × 10⁻³) |

The Leissa values were transcribed without access to the book (see the test's comment); the
converged FEM agrees with them to 0.05 %, and 35.99 for the first mode is the value quoted in the card.

**Compile time** (ADR-0002 asks for it; one core, development container, default mesh and `--grid-order`
0 unless noted): `test-monochord` (0.6 x 0.4 m, f\_max 3 kHz, free rim) 97 modes, 6.5k degrees of
freedom, 7.6 s with the grid; `test-guitar-6` (f\_max from the file) 160 modes, 10.8k degrees of
freedom, 39 s; `generic-grand-88` at `--f-max 1000` (10-vertex outline, 12 ribs, 2 bridges) 75 modes,
12.5k degrees of freedom, 55 s. Time is dominated by the sparse factorisations of the windows
(about 40 modes each) and grows with the number of modes; a 2,000 to 3,000-mode soundboard (PLAN-COMP)
has not been compiled and will need larger `nev` or a better eigen-solver (revisit with ADR-0002's
"Revisiting" clause if it takes much more than an hour).

## Environment

`import gmsh` needs the system library `libGLU.so.1` on Linux (`apt install libglu1-mesa`; on
Ubuntu runners it may need `libgl1` and `libxrender1` as well). The Gmsh Python wheel is a separate
GPL-2.0-or-later dependency in the `fem` extra, called as a library and never bundled (ADR-0002);
the compiler is MIT source. The test run never needs a display (`General.Terminal` off,
`readConfigFiles=False`).

## Extending

A new element or beam model only touches `fem.py`; the rest sees `System`. A different mesher
returns `PlateMesh` (nodes, triangles, node pairs per beam). Species go into `library.json` with a
full source record; `test_b2_r2_*` fails if a field of the record is missing.
