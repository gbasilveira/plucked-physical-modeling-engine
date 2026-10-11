<!-- SPDX-License-Identifier: MIT -->
# Reference solver

An independent float64 time-domain solver that validates the engine (phase P0.5). It simulates
strings, hammers, bridges, an orthotropic plate, barriers and geometric nonlinearity in physical
space with finite differences and Newton iterations. It shares only the description structures
(`engine/include/pme/*.hpp`, header-only use) and the analysis library (`pme_analysis`) with the
engine; it never links an engine module and never includes `engine/src/` (a configure-time check in
`reference/CMakeLists.txt` enforces the link rule). The schemes, their sources and orders, and
which tests run in which builds are in [docs/dev/reference-solver.md](../docs/dev/reference-solver.md).

## Build and run

```bash
cmake --preset linux-release                 # or macos-release / windows-release
cmake --build --preset linux-release --target pme_reference_cli
ctest --preset linux-release -R "P0.5"       # the reference tests

build/linux-release/tools/reference-cli/pme_reference_cli render reference/scenarios/string_pluck.json \
    --features features.json --wav string.wav --energy energy.json
build/linux-release/tools/reference-cli/pme_reference_cli refine reference/scenarios/string_pluck.json \
    --grids 64,128,256 --quantity partial-frequencies --partials 5
```

`render` writes the golden-feature file (through `pme_analysis`, so `pme_golden_compare` and the
Python comparator read it), a float32 WAV of the observation signals at the scenario's `fs` (low-pass
filtered and decimated by `oversampling`; `--normalise` scales to a peak of 0.9) and the energy
trace with its summary. `refine` runs the first string on the given grids (a constant integer ratio),
refines the time step with the grid, measures the partial frequencies of the first observation and
prints the observed convergence orders of the three finest grids (also as JSON with `--json`).
Exit status: 0 success, 1 a rendering error (stderr names the offending field), 2 usage error.

### Python

```bash
cmake --preset linux-release -DPME_BUILD_PYTHON_BINDINGS=ON \
      -DPython_EXECUTABLE="$(uv run python -c 'import sys; print(sys.executable)')"
cmake --build --preset linux-release --target pme_reference
PME_REFERENCE_PYDIR=build/linux-release/reference/python uv run pytest tools/tests/test_p0_5_*.py
```

```python
import pme_reference

result = pme_reference.render(scenario)  # scenario: dict or JSON text
result["signals"]["y"]  # NumPy float64 array, one value per simulation step
result["energy"]["total"]  # energy of the state pairs, steps + 1 values
result["features"]  # the dict the CLI writes with --features
```

Errors for scenarios the reference does not model are `pme_reference.RefError` (a `ValueError`).
The default build (without `PME_BUILD_PYTHON_BINDINGS`) builds and tests everything except the module.

## Scenario format (schema_version 1)

JSON, described by [scenario.schema.json](scenario.schema.json); examples in
[scenarios/](scenarios/). SI units. Top level: `schema_version` (1), `name`, `fs`, `oversampling`
(the simulation runs at `fs * oversampling`), `duration`, `theta` (default 1/4, range [1/4, 1]),
`strings`, `hammers`, `bridges`, `plate`, `barriers`, `observations`, `feature_partials`,
`feature_threshold_db`.

| Object | Fields |
| --- | --- |
| `strings[]` | `params` (the `StringParams` members: `L dCore rhoCore ECore dOuter rhoWind packing epsWind f1Target b1 b2 b3 boundary`), `grid.intervals`, `bridge` (index; absent = fixed end), `initial_displacement` (`amplitude position angle`), `geometric` (`tension_modulation longitudinal longitudinal_b1 longitudinal_b3 longitudinal_initial`) |
| `hammers[]` | `mass K p mu width strike_x velocity gap strings[] wall` |
| `bridges[]` | `position {x,y,z}`, `theta_b`, `conductance_h`, `model` (`mass_spring_dashpot {mass stiffness damping}` or `conductance {G}`) |
| `plate` | the `PlateParams` members in snake_case, `grid {nx ny}`, `loss {b1 b3 reference_hz}`, `initial_displacement {x y amplitude}` |
| `barriers[]` | `string side K alpha mu x[] b[] w[]` |
| `observations[]` | `name`, `kind`, `string`/`bridge`/`hammer`/`barrier` index, `x`, `polarisation`, `point` |

Observation kinds: `string_displacement`, `string_velocity`, `bridge_force`, `bridge_force_perp`,
`bridge_displacement`, `bridge_velocity`, `hammer_force`, `hammer_penetration`, `hammer_position`,
`barrier_force`, `plate_displacement`, `longitudinal_bridge_force`, `longitudinal_displacement`.
Signals are recorded at the simulation rate, one value per step.

## Limits

The reference rejects what it does not model, with an error that names the field:

- **b2 = 0 only.** The `b2 sqrt(omega)` loss of SPEC-STR has no local finite-difference form.
  Comparisons against the engine (A2-T3 and later) use `b2 = 0`. `sigmaOverride` (a per-partial
  table), a horizontal detune `detuneH` and `bridgeEnd = At0` are rejected too.
- **Rectangular plates only**, grain along x (`grainAngle = 0`), simply supported edges: a polygon
  `outline`, a `thicknessGrid`, `ribs`, `bridges` as beams and rim stiffness are rejected. A rotated
  bridge axis (`theta_b`) is not supported together with a plate.
- A clamped string end is supported only without a bridge.
- Tension modulation and longitudinal waves: a single fixed-end string with an initial displacement,
  without hammer, barrier, bridge or plate. The longitudinal coupling is one-way (transverse motion
  drives the longitudinal modes), as in the default tier of SPEC-GEO.
- Hammers strike the vertical polarisation; the barrier damping is lumped per contact point.
- Moisture coefficients of the plate are applied linearly to the listed reference values
  (`value = reference + coefficient * moisture`).
