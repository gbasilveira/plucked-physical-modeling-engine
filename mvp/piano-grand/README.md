# Piano 0.7a "Grand" (offline reference of the grand's board and near field)

Status: stages 1 to 5 done; limits and what 0.7b needs are at the end of this file.
This is an experimental MVP iteration, not a phasing-plan phase. Nothing here is built into the 0.6 plug-ins.

## Why

0.6 Full sounds like an upright. Its board is a simply supported rectangle 1.4 x 1.6 m with homogenised ribs, assumed bridge curves,
a one-sided baffled Rayleigh radiation, mics 1.5 m above the board, no rim, cavity, lid, floor or air loading. 0.7a builds the most accurate
offline reference of a grand's body and near field that this machine allows (the room is excluded: it comes later as a downstream reverb).
Hammer and damper are unchanged (they are 0.8). Baking is 0.7b/0.7c.

## Offline tools used (not vendored, not linked into any plug-in)

| tool | version | licence | use |
| --- | --- | --- | --- |
| Python | 3.14 | PSF | scripts |
| NumPy | 2.5.4 | BSD-3-Clause | all |
| SciPy | 1.18.1 | BSD-3-Clause | sparse shift-invert eigensolver (ARPACK), Delaunay triangulation, interpolation |
| Matplotlib | 3.11.2 | PSF-based (BSD compatible) | figures |
| Pillow, contourpy, fonttools, kiwisolver, cycler, pyparsing, packaging, python-dateutil, six | pip-resolved | HPND / BSD / MIT / OFL / Apache-2.0 | Matplotlib dependencies |

No GPL tools (no gmsh, no Triangle). The mesher is our own (`fem.py: make_mesh`). The venv lives outside the repository.
Large intermediates (meshes, mode shapes, FDTD fields, raw IRs) live in `~/trabalho/gbasilveira/piano-grand-work/`, not in git.

## Stage 1: grand soundboard by FEM

Files: `geometry.py` (outline, thickness, ribs, bridges, key table), `fem.py` (element, beams, solver), `fem_verify.py`, `fem_beam_verify.py`,
`board_study.py` (convergence, variants), `converge_report.py`, `make_modes.py`, `board_analysis.py`, `tests/`.

### Model

* **Plate theory: Reissner-Mindlin** (unknowns w, phi_x, phi_y per node), orthotropic spruce, grain along the long axis y, across-grain axis x.
  Material: Chabassier-Durufle (RT-0425 v2) as the MVP: E_L = 11.6 GPa, E_R = 0.9 GPa, G_LR = 0.75 GPa, nu_LR = 0.37, rho = 390 kg/m3.
  Not in that table and therefore an assumption: the through-thickness shear moduli, taken from the Wood Handbook ratios for spruce
  (G_LT/E_L = 0.061, G_RT/E_L = 0.003) times E_L: G_LT = 0.71 GPa (shear in the y-z plane), G_RT = 0.035 GPa (x-z, rolling shear); shear
  correction factor 5/6. Structural loss factor 0.02 (sigma = 0.01 omega) as MVP 3, so the comparison with 0.6 isolates geometry and acoustics.
* **Why Mindlin, not Kirchhoff.** The across-grain rolling shear modulus is 26 times smaller than E_R, so shear deformation matters from a few
  hundred hertz. Same element with the shear stiffness x 1e4 (Kirchhoff limit): f1 = 53.2 Hz instead of 48.5 Hz, and only 128 modes below
  2.5 kHz instead of 201 (`board-variants-0.7a.txt`). Kirchhoff overestimates the frequencies by up to about 10 % at the lowest modes and makes
  the board far too stiff above 1 kHz.
* **Element.** Three-node triangle, linear w and phi, constant curvature, constant shear strain from the three edge shear gaps (least
  squares, discrete-shear-gap idea), lumped mass. Verified (`fem_verify.py`, test G-T2) against the exact simply supported Mindlin plate: thin
  (h/a = 0.002) and thick (0.1) plates differ by the same 1.1 % and 0.4 % at 24 x 24 x 2 elements: **no shear locking**; second-order
  convergence. Lumped mass was chosen because the consistent mass gave 2.7 times larger dispersion error for this element.
* **Variable thickness.** 9.0 mm in the middle, 6.5 mm at the rim, raised-cosine taper over the outer 0.25 m (typical of published boards;
  assumption). Mass of the board plate 4.7 kg.
* **Outline.** A 2 m grand class wedge with a curved bent side, board length 1.50 m, width 1.22 to 1.28 m at the front, 0.45 m at the tail, area
  1.495 m2 (a 200 cm grand has a soundboard of about 1.4 to 1.6 m2; the case is 200 x 152 cm for the Yamaha C5, manufacturer figures from
  memory). The board starts about 0.40 m behind the front edge of the case (pin block). `geometry.py` has the outline function; figure
  `geometry.png`.
* **Explicit ribs.** 12 ribs across the grain (along x) at 0.115 m spacing from y = 0.12 m, spruce 22 mm wide x 30 mm high (grain along the rib,
  E = 11.6 GPa), height tapering to 40 % over the last 0.10 m at each end, stopping 20 mm short of the rim. Each rib is a chain of Timoshenko beam
  elements on mesh edges: bending stiffness of the T section (plate flange of width b + 2h) about the composite neutral axis minus the flange
  already counted in the plate, Timoshenko shear, torsion, line mass, rotary inertia with the eccentricity of the rib above the mid-plane. Membrane
  coupling is not modelled (the neutral-axis shift is taken into account instead).
* **Bridges.** Treble (long) bridge from key 45 near (0.50, 1.10) to key 108 near (1.10, 0.14), 1.14 m long; bass bridge from key 21 (0.13, 1.33)
  to key 44 (0.40, 0.95), 0.49 m long; hard maple 28 x 45 mm, E = 12 GPa, rho = 650 kg/m3 (0.82 kg/m); also beams on mesh edges. The mesh is built
  with the beam lines as chains of nodes (lattice points closer than 0.6 h are dropped) so each beam is a chain of mesh edges. (An earlier version
  embedded the beams in elements; its frequencies varied by 8 % with the position of a rib inside the element, so it was replaced. The embedded
  version is kept out of the code.) Beam length modelled 13.26 m against 13.25 m nominal, beam mass 4.15 kg. Total board mass 8.9 kg.
* **String points.** `board-key-table-0.7a.csv`: 88 keys, evenly spaced in arc length along their bridge with half a pitch margin at each end
  (17.9 mm treble, 20.3 mm bass). Keys 21 to 44 on the bass bridge, 45 to 108 on the treble bridge (same split as 0.6). The 0.6 assumed curves are
  gone. Minimum distance from any key point to the rim 0.13 m. The per-key participation phi (88 x 395, mass normalised) is in `board-modes-0.7a.npz`.
* **Edge.** w = 0 on the whole rim, and the rotation clamped by default (`theta_k = None`); a rotational spring per length `theta_k` makes it elastic
  (parameter of `fem.build_board`). Sensitivity (`board-variants-0.7a.txt`): f1 = 48.5 Hz clamped, 44.4 Hz with 2000 N m/rad/m, 40.4 Hz with free
  rotation. Clamped follows the Chabassier-Chaigne-Joly model; a real board glued into the rim is stiffer than free rotation.
* **Not modelled:** crown and downbearing prestress, the cut-off bar, the bridge notches and pins, the iron plate's coupling, inhomogeneity of the
  wood, the rim's own vibration.

### Convergence (mesh study)

`board-convergence-0.7a.txt`, produced by `board_study.py convergence` and `converge_report.py`. Six Delaunay meshes, h = 40, 30, 20, 15, 10 and 7.5 mm
(2871 to 89238 degrees of freedom). The frequency error is measured mode by mode against the finest mesh:

| mesh h | 0-250 Hz median / max | 250-500 | 500-1000 | 1-2 kHz | 2-3 kHz |
| --- | --- | --- | --- | --- | --- |
| 20 mm | 1.3 / 1.5 % | 2.8 / 4.7 | 3.6 / 5.5 | 1.9 / 5.3 | 2.5 / 3.3 |
| 15 mm | 0.8 / 2.8 | 1.6 / 3.3 | 2.1 / 3.3 | 1.0 / 3.3 | 1.2 / 1.7 |
| 10 mm | 0.45 / 1.4 | 0.41 / 2.1 | 0.71 / 1.2 | 0.39 / 2.3 | 0.37 / 0.77 |

Observed order 1.8 (three finest meshes, modes below 1 kHz; the element is second order). Against the Richardson extrapolation (p = 2) of
the two finest meshes the 7.5 mm mesh is within a median 0.6 % and at most about 3 % of the mesh-converged frequency in every band up to 3 kHz.
The mesh-topology jitter around beam crossings keeps the error from falling below about 1 %. **Converged range: the production mesh (h = 7.5 mm)
resolves modes up to 3.6 kHz (395 modes, 479 up to 4.0 kHz) to about 1 % median and 3 % worst case.** Above 3.6 kHz the model is not used; the board
there is the statistical one (see the end of this file). Mesh sizes needed: 15 elements per across-grain wavelength at 3 kHz would need 3 mm; the
extrapolation is what makes 7.5 mm sufficient.
Timings (i3-8100, shift-invert ARPACK, up to 4 kHz): 5 s at 2871 dof, 168 s at 89 238 dof; the production run (3.6 kHz) 187 s.

### Results

First ten in-vacuo modes (Hz): 49.9, 84.4, 133.2, 142.9, 187.3, 197.8, 238.2, 253.4, 267.5, 311.3. Mode counts: 19 below 500 Hz, 50 below 1 kHz,
138 below 2 kHz, 286 below 3 kHz (0.118 modes/Hz at 1 to 3 kHz). Variants (h = 15 mm, `board-variants-0.7a.txt`): without ribs f1 = 25.4 Hz and 102
modes below 1 kHz; with the ribs (and bridges) 48.5 Hz and 48 modes: the ribs roughly halve the modal density and double the lowest frequencies; the bridges add mass and shift f1 only slightly (without bridges f1 = 50.4 Hz). The ranking is the one the literature gives for a ribbed board, whose across-grain stiffness is dominated by the ribs.

Driving-point mobility at the bridge (`board_analysis.txt`, `mobility.png`; in vacuo, loss factor 0.02). The infinite-plate mobility of the
bare board is 5.7 mm/s/N (Z = 175 N s/m). The band-averaged real part of the mobility at the 88 bridge points is 0.24, 0.17, 0.13, 0.11, 0.10,
0.09 of that in the bands 50-100, 100-200, ..., 1.6-3.2 kHz, that is about 0.5 to 1.5 mm/s/N for most keys (0.2 to 2 mm/s/N overall; the two
ends of each bridge, A0 and C8, are several times larger in some bands). Published grand soundboards are reported with mean bridge mobilities of the order of 1e-3 m/s/N
(Giordano 1998; Wogram 1980; Ege, Boutillon and Rebillat 2013). That order of magnitude, and a first board mode at 50 Hz (published in-situ
grands: roughly 50 to 100 Hz), are consistent; **these literature values are quoted from memory and were not re-checked against the papers**, so
the comparison is qualitative, as asked. The old 512-mode table had 20.7 Hz for its first mode and a mobility that the ribs did not reduce.

### Tests (ctest)

* `grand-board-fem` (`tests/test_board_fem.py`, skipped without scipy): G-T2 element verification (exact SS Mindlin plate thin and thick, embedded beam
  against a thick strip) and G-T1 regression: the first 24 modes of a 30 mm mesh against the production frequencies (limits 8 % max, 3.5 % median), board
  area and mass.

## Stage 2: near-field acoustics (no room)

Files: `grand_fdtd.cpp`, `scene.py`, `run_fdtd.py`, `rayleigh.py`, `assemble_ir.py`, `tests/test_fdtd_*.py`.

* **Method: 3-D staggered-grid FDTD of the air, with the board as a two-way coupled modal moving boundary.** First-order p-v leap-frog (the 7-point
  scalar scheme), dx = 25 mm, dt = 1/24000 s (Courant 0.572 of the 0.577 limit), 216 x 160 x 104 cells (5.4 x 4.0 x 2.6 m). The 191 board modes below 2.4 kHz
  move a layer of z-faces (the board is a thin sheet; both faces see the air, so the cavity short circuit and the dipole cancellation are in the solution);
  the generalised force on a mode is the pressure difference across the sheet integrated with the mode shape. Modal oscillators are leap-frogged with the
  stiffness pre-warped so the numerical frequency equals the FE frequency. Rigid cells: rim ring 0.10 m thick from 0.05 m below the board to 0.975 m, a front
  block (pin block, fall board, desk), the lid (a one-cell-thick plane, hinged on the spine side at the rim top: open = 35 deg, half stick = 12 deg, closed = 0),
  and the floor (z = 0, rigid, 0.625 m below the board). Absorbing: a 0.5 m quadratic sponge (damping of p and v with equal sigma) on the four sides and the top.
  **Assumptions that are nominal, not measured:** board height, rim thickness and height, lid angles and size (1.3 m wide panel), rigid lid (no transmission), rigid floor,
  front block. **Not modelled:** the iron plate (a perforated rigid plane above the board), strings, keybed, legs, lyre.
* **Reciprocity: one run per microphone.** A unit volume source at the microphone cell records the modal velocities of all 191 modes; the pressure per unit
  generalised force on mode i is gamma_im = -V_i/Q. `tests/test_fdtd_reciprocity.py` (ctest `grand-fdtd-reciprocity`) checks this against forward runs in a
  synthetic box: forward and reciprocal transfer functions agree to 1e-4 relative (five modes). 18 runs (3 lid states x 6 microphones), 24000 steps (1 s),
  5 to 7 minutes each on 4 cores (about 12 ms per step); 190 MB per run (modal velocities), kept in the work directory.
* **Accuracy of the air solution.** Grid dispersion of this scheme: phase-velocity error (kdx)^2(1-S^2)/24 along the axes = 0.14 % at 500 Hz, 0.55 % at 1 kHz, 1.1 % at
  1.4 kHz, 2.2 % at 2 kHz. Free-field check against the analytic point source plus its floor image (empty scene, `wave` mode): magnitude within 3 % to 1.3 kHz;
  residual (dispersion plus sponge reflection) -29 dB (0.5 m) and -21 dB (1 m) re signal at 125 to 1000 Hz, -9 to -12 dB at 1 to 2 kHz. The sponge is only
  0.5 m thick, so below about 200 Hz its reflection is not negligible (not measured separately). **Trusted: below 1.4 kHz. Crossover: raised cosine 1.4 to 2.0 kHz.**
* **Passivity.** `tests/test_fdtd_energy.py` (ctest `grand-fdtd-energy`): in a closed box with a lossless board the air-plus-modes energy stays constant (drift 1.6e-4,
  1.5 % exchange oscillation between air and board), with a damped board it falls in every window. `grand-board-passivity` (C++): the FEM board in the 0.6
  difference-form scheme conserves its energy to 5e-15 (lossless) and never gains energy (damped).
* **Above the crossover: baffled Rayleigh integral on the top face** (`rayleigh.py`) with one specular reflection from the lid (image of the microphone in the lid
  plane, valid where the specular point lies on the panel) and a line-of-sight test through the rigid cells. A blocked path contributes nothing (no diffraction). In
  1.0 to 2.0 kHz, where both exist, the FDTD is 2 to 5 dB above the Rayleigh model at the close AB (open lid), and 10 to 20 dB above it at the player and audience
  microphones, because the shadowed Rayleigh path is zero while the FDTD diffracts over the rim. **So above 2 kHz the player and audience renders are lower bounds.**
  The statistical board above 3.6 kHz (the old analytic approach) was not built: **there is no board radiation above about 4 kHz in 0.7a** (see the validation).
* **Air-loaded board for the strings.** The strings see the board through the 395 modal poles. From the narrow-band ring of the modal velocities the loaded pole was
  estimated for the lowest modes; accepted where the fit was clean and plausible: modes 1, 2 and 4 (open lid): frequency 0.897, 0.952, 0.955 of the vacuum value
  (49.9 to 44.8 Hz: the air mass lowers the lowest mode by 10 %), decay rate 1.24, 1.13, 0.94 of the structural one. All other modes keep their vacuum poles:
  **the diagonal air-loading of the 0.6-style coupling is estimated for three modes only**; the full loaded admittance matrix needs one forward run per bridge point (0.7b).
* **Mic presets** (world frame, X to the treble, Y away from the player, Z up; `scene.py`): close AB: (0.40, 1.00, 0.825) and (0.80, 1.00, 0.825), 12 cm above the strings (strings
  taken 8 cm above the board), 0.40 m apart; player: ears 0.16 m apart at (0.52/0.68, -0.40, 1.15); audience: 2 m beyond the bent side, (3.35, 1.0/1.4, 1.25).

## Stage 3: coupled render

`grand_render.cpp` (`piano-grand-render`) runs the unchanged 0.6 Full strings and hammer (Piano class: same flags as `piano-midi-render`, quality Full) with the
FEM board installed through `mvp/piano-v4/board_override.hpp` (omega, sigma, phi of the 395 modes in the 512-row table; unused rows have phi 0). It writes the bridge
force records g_k(t) (the quantity the 0.6 radiation stage consumes); `render_grand.py` convolves them with the port-space impulse responses E[mic, port, lag] of
`assemble_ir.py` (FFT convolution; 1 s IRs). With no override installed the 0.6 code is arithmetically unchanged (the hook only selects table values;
`piano-previous-bitexact` is the test). The coupled-strings board uses the first 128 modes explicitly (as 0.6) and the rest as residual loss.

## Stage 4: validation against Salamander (Yamaha C5, close AB), open lid, close-AB preset

Physics alone, no fitting; one global level factor (0.7a close AB RMS of C4 at velocity 60 over 0.05 to 1 s equals the 0.6 Full render) so levels are comparable; the
metrics are level independent anyway. Notes A2 (velocity 40, layer 4), C4 (60, layer 8), C6 (92, layer 12); `grand-validation-0.7a.json`, `metrics.py`.
Octave bands 62.5 Hz to 8 kHz+ (0.5 to 1.5 s window), error in dB of the band's energy share relative to the reference:

| note | model | 62-125 | 125-250 | 250-500 | 500-1k | 1k-2k | 2k-4k | 4k-8k | 8k+ |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A2 | 0.6 Full | +0.4 | -0.8 | +0.9 | -8.9 | -1.7 | -0.9 | +8.6 | -30.7 |
| A2 | 0.7a | -15.6 | +9.7 | -0.3 | +3.2 | +5.5 | -5.8 | -28.6 | -50.9 |
| C4 | 0.6 Full | -5.5 | +10.6 | +4.8 | -8.0 | -9.3 | -4.3 | -1.0 | -9.2 |
| C4 | 0.7a | +0.5 | +5.1 | +0.6 | +0.4 | -9.5 | -7.3 | -28.8 | -52.5 |
| C6 | 0.6 Full | -2.4 | -5.2 | +1.6 | +5.2 | +0.1 | -23.0 | -1.8 | +0.1 |
| C6 | 0.7a | -5.6 | -17.2 | -6.5 | -8.5 | +0.2 | -14.9 | -13.0 | -64.4 |

Stereo L/R coherence (average over 0.1 to 2 s, bands 125 to 4000 Hz centres): reference A2 1.0, 0.99, 0.83, 0.84, 0.77, 0.57; C4 0.77, 0.74, 0.97, 0.42, 0.36, 0.95;
C6 0.92, 0.30, 0.28, 0.65, 0.94, 0.49. 0.6 Full: A2 1.0, 0.38, 0.90, 0.98, 0.97, 1.0; C4 0.74, 0.77, 0.45, 0.81, 0.93, 1.0; C6 0.93, 0.69, 0.56, 0.96, 0.95, 1.0.
0.7a: A2 0.88, 0.88, 0.67, 0.97, 0.99, 1.0; C4 0.82, 0.83, 0.84, 0.90, 0.84, 1.0; C6 0.74, 0.64, 0.53, 1.0, 1.0, 1.0. Decay rates (dB/s, 125 Hz to 4 kHz) and attack
centroids are in the JSON.

**Reading, honestly.** The reference coherence is very note dependent (0.28 to 0.99) and is not 0.4 to 0.65 everywhere. 0.7a does not reduce the coherence: in the low bands it
is 0.6 to 0.9 (reference 0.3 to 1.0), above 1 kHz it is about 1 because only a few board modes radiate there and the mics see one source. The octave-band errors are no better
than 0.6 at low and mid bands and **far worse above 2 kHz (-6 to -65 dB)**: the board model has no radiation above about 4 kHz (395 modes to 3.6 kHz, no statistical continuation),
and the 2 to 4 kHz bands rest on a crude Rayleigh model. Physics alone, with this geometry, gets the low-frequency air loading and the cavity effects, not a grand's
brightness. What is missing for that is above all the high-frequency board (statistical modes, bridge and rib scattering, strings radiating directly, the plate), which 0.7b/0.7c
must address before baking.

### Diagnosis of the high-frequency shortfall (not tuned away)

Ranked by evidence:
1. **No board above 3.6 kHz (certain).** The FEM modes stop at 3.6 kHz and nothing continues them, so every band from 4 kHz up is empty (-28 to -65 dB). This alone explains the 4-8 kHz and 8 kHz+ columns. A statistical continuation (the old analytic modal table, or random modes with the measured bridge admittance) is the missing piece.
2. **The 2-4 kHz band rests on the Rayleigh/lid-image model plus the 3.6 kHz truncation.** At the close AB the FDTD and the Rayleigh model agree within 2 to 5 dB in 1 to 2 kHz (open lid), so the crossover itself is not the culprit; but a 2 kHz to 3.6 kHz board radiating through a baffled-plane integral cannot make up for the missing direct string and hammer radiation and the plate.
3. **Not the FDTD band or the board's HF damping:** the FDTD is used only below 1.4 to 2 kHz, where the bands 62 Hz to 2 kHz are within about 10 dB of the reference (like 0.6); the structural loss factor 0.02 is the 0.6 value, so it cannot explain a difference with 0.6.
4. **Mic placement:** the close pair sits 12 cm above strings that are not in the model (the strings' own radiation and the hammer noise are absent), so the near-field HF of the real recording is not reproducible with a board-only source. This is untested here.
5. **Coherence:** the reference spans 0.28 to 0.99 depending on note and band; 0.7a's 0.6 to 0.9 at low frequency is in that range, and its 1.0 above 1 kHz is a symptom of 1 . It cannot be claimed as an improvement.

**Anomaly checked (closed and half lid logs):** `fdtd_over_rayleigh_dB` of about 3011 to 3023 dB and `peak 0.0` for the player and audience microphones were not a broken assembly. They were a ratio against an exactly zero Rayleigh energy (every Rayleigh path to those microphones is shadowed by the rim, front block or closed lid) floored at 1e-300, and a peak rounded to two decimals (E arrays are finite, peaks 0.002 to 0.005 against 0.03 at the close microphones, which is the distance and, with the closed lid, the sealed cavity). The ratio is now reported as `null` when undefined. Closed lid: the close-AB IR RMS is 2.8 times the open lid's (sealed cavity), the outside microphones are 1.8 times lower than with the open lid (a rigid closed lid only leaks around the rim).

## What 0.7b needs

The statistical board above 3.6 kHz; the 88 x 88 loaded bridge admittance (forward FDTD runs, or one per bridge segment); direct string radiation and the iron plate; a diffraction model (or finer FDTD) for the shadowed paths; the lid as a vibrating panel; a measured (or published) mobility to check the absolute board admittance; finer grids for the presets that matter.

## Tests (all ctest, `cmake --build out/piano-mvp-v4 && ctest --test-dir out/piano-mvp-v4`)

`grand-board-fem` (G-T1/G-T2), `grand-fdtd-reciprocity` (G-T3), `grand-fdtd-energy` (G-T4), `grand-board-passivity` (G-T5), and the unchanged 0.6 tests including `piano-previous-bitexact`
(the hook leaves the 0.6 arithmetic untouched). Configure with `-DPIANO_GRAND_PYTHON=<python with numpy and scipy>` to run the Python tests; they skip (77) without scipy.

## Files and outputs

`out/piano-mvp-v4/grand/`: `grand-ab-0.7a.wav` (+`-raw`), `grand-mics-0.7a.wav` (+`-raw`, `-order.txt`), `chopin-op28-no4-0.7a-{closeab,audience}.wav` (+`-raw`; the Chopin MIDI performance is by Bernd Krueger, CC BY-SA 3.0 DE, see
`mvp/piano-v4/listening/README.md`; the whole prelude was not rendered), `grand-validation-0.7a.json`, board reports, figures and tables. Compute (i3-8100): board mesh study 6 meshes about 7 min; production modes 3 min; FDTD 18 runs x 5 to 10 min (about 2 h with other load); IR assembly 17 to 30 min per lid state; render of a 12 s note about 15 s plus 60 s of convolution.
