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

---

# Update 0.7b (2026-10-27): HF board, string and plate radiation, diffraction, full loaded admittance

Status: items 1 to 4 of the 0.7b brief done and validated against Salamander without fitting; the vibrating lid (item 5) was not attempted. Offline only; the 0.6 plug-ins are unchanged
(`piano-previous-bitexact` passes; the only edits under `mvp/piano-v4` are read-only probes, `Piano::visitStrings`, `visitLightStrings`, `voiceStruck`, `compiledString`, `keyStringCount`,
and `TensionStruckNote::horizontalQ/U`, which no audio path calls).

## What was added and why

| stage | file(s) | method |
| --- | --- | --- |
| 1 board above 3.6 kHz | `hfboard.py`, `build_hf_b.py`, `hf_continuity.py` | statistical continuation 3.6 to 16 kHz (below) |
| 2 radiation of strings and plate | `string_radiation.py`, `grand_render.cpp`, `scene_b.py` | line dipoles from the modal string states, both polarisations; the plate as a rigid obstacle in the FDTD and the ray model, and a reflector for the strings |
| 3 diffraction, shadowed paths | `rayleigh_b.py`, fine FDTD (`scene_b.py`, `run_fdtd_b.py`) | fine grid (dx 12.5 mm) to ~2.2 kHz plus a Fresnel knife-edge model in the ray part |
| 4 loaded 88 x 88 admittance | `admittance.py`, `make_table_b.py`, `grand_fdtd.cpp` (`--mode kin`) | kinematic FDTD runs give the modal radiation impedance Z(w); Y_port = Phi (D + Z)^-1 Phi^T |

### 1. Board above 3.6 kHz: statistical continuation (3.6 to 16 kHz)

*Why not more FEM.* Resolving the board at 8 kHz with 15 elements per across-grain bending wavelength (about 30 mm at 8 kHz, shear-limited: the Mindlin plate's across-grain
phase speed saturates at the rolling-shear speed 270 m/s, below the speed of sound) needs a 2 mm mesh, about 2 million nodes and 3000 modes; not feasible here. The 7.5 mm mesh is converged to 3.6 kHz and not beyond, as in 0.7a.

*Model* (every ingredient from the same orthotropic Mindlin plate; nothing fitted to a recording):
* dispersion omega(k, theta) of the lowest branch from the 3 x 3 Hermitian stiffness matrix of the homogeneous plate at the mean thickness (the thickness-shear branch starts at 16.7 kHz, so one branch up to 16 kHz); thin-plate limit checked (test G-T9);
* modal density: Weyl count with a clamped-edge perimeter term; the FEM count in the last half octave (0.8 to 0.985 of the last FEM mode) is 0.945 of the bare-plate count (ribs and bridges), and the continuation is scaled by that. Mode spacing is Gamma(2)-distributed (level repulsion). Above 10 kHz the modal overlap is above 50 (bandwidth 0.02 f against spacing 3 Hz), so the set is decimated by 6 and the participations are re-scaled by the calibration: 2061 modes (1659 below 10 kHz, 402 above);
* mobility: for mass-normalised modes Re Y_k = <phi_k^2> n_f / 4 (modal sum of Lorentzians; for an infinite thin plate it gives 1/(8 sqrt(rho h D))). The per-key target is the FEM band-averaged bridge-point mobility of 2.2 to 3.2 kHz, continued with the median log-log slope of the FEM over 1.1 to 3.2 kHz (-0.14; clipped to [-1/2, 0], the infinite-Euler-beam trend of the bridge, Cremer-Heckl). The realised mobility is calibrated per key and third-octave band (scale factors 0.15 to 0.25: the bridge points are 14 to 17 dB less mobile than a bare plate point, as in the FEM);
* mode shapes: 40 plane waves on the dispersion contour (direction pdf proportional to the density of states of the contour), random phases, weight (rho h(x, y))^-1/2 with the local thickness, a clamped-edge layer 1 - (1 + kd) exp(-kd), mass-normalised on a 10 mm grid; the string points see the same field, so neighbouring keys are correlated as a diffuse field is;
* loss factor: flat 0.02 (as the FEM). A rising option exists (`eta_of_f`) but is **not used**: modal loss factors of soundboards are reported to rise with frequency (Ege, Boutillon and Rebillat 2013 and Chaigne et al. are the usual references) but I could not verify the numbers here, and the flat value keeps the continuity;
* radiation of those modes: the same Rayleigh + diffraction path model as the FEM modes above 2.2 kHz, evaluated narrow-band at each mode's own frequency (surface grid 6 mm below 10 kHz, 4 mm above: the integrand needs more than 2 points per bending wavelength).

*Continuity across 3.6 kHz* (`hf_continuity.py`, `grand-0.7b-reports/hf-continuity-*-0.7b.json`): the combined mobility step across the junction is 0.1 dB (median over keys; the median level is -33.7, -33.6, -33.1 and -34.1 dB re 1 m/(s N) in the bands at 2.2, 2.8, 3.6 and 4.5 kHz, p10 to p90 spread of about 3.5 dB). Radiated mean-square pressure per unit port force of a statistical set built for the converged band 2.2 to 3.6 kHz against the FEM modes through the same ray model: close-AB mic (open lid) -1.4, -1.8, -0.6, +0.5 dB in sixth-octave bands from 2.5 to 3.6 kHz, so no level step at the close mics; **at the audience mic the statistical set is lower by 2.1, 4.0, 5.8 and 6.3 dB** (not corrected: the far field of the statistical shapes is weaker than the FEM's, a missing rib/bridge scattering that would put energy into the radiating wavenumbers). Test G-T9 (`tests/test_hf_board.py`) repeats the held-out check on a 20 mm FEM: density +19 %, mobility +/- 0.4 dB median, junction step -0.5 dB.

### 2. Radiation of the strings and the plate

*Strings.* A thin cylinder moving transversally exerts the added-mass force rho0 pi a^2 dv/dt on the air per unit length, a line of dipoles (`string_radiation.py`: p = F (e . r) (1 + jkr) exp(-jkr) / 4 pi r^2). Mode n of a string has v(x) = sqrt(2/L) sin(n pi x/L) (the engine's normalisation); the gain per unit analytic modal velocity is integrated along the string at the partial's frequency for each of the six microphones, vertical and horizontal polarisation, and the renderer applies p = Re[G (u/dt + j omega (q - u/2))] to the states of every struck string (`piano-grand-render --strrad`). Geometry (nominal): straight strings from the bridge points of `geometry.py` to the front, inclined towards the treble just enough for the overstrung bass to fit the case (front line at board y = -0.35 m), 0.705 m above the floor (0.08 m above the board, the height the close mics assume), unison offsets 9.5 mm. Reflecting plane under a string element: the plate top (0.680 m) where the plate is solid, else the board plane; tangential force doubles, normal force cancels (a vertical dipole 1 cm above the plane radiates 29 dB less than free at 200 Hz; with the 25 mm gap of the model the cancellation is smaller but still large at low frequency, the C4 strings are 60 to 70 dB below the board below 500 Hz). Test G-T6 (`tests/test_string_radiation.py`): the short-string gain matches the analytic point dipole to 1.5e-4 relative, the far-field phase advance matches -k dr to 0.001 rad, and the image rule gives 1.99 (tangential) and 0.035 (normal). The output is delayed by the distance of each microphone to the middle of the string field (a whole number of samples), the phase being referred to that distance: the envelope of the nearest strings arrives up to about 1 ms early.
Result: the strings are 69 dB below the board in 125 to 250 Hz at the close AB and equal to it (-7 dB) from 4 kHz; with the board model alone empty there, they fill the attack at 13 kHz (the 0.6 strings stop at 12 kHz).

*Iron plate (nominal geometry, not measured).* A 25 mm rigid sheet 30 mm above the board plane covering the board outline minus windows (outline shrunk by 0.08 m, minus four cross struts 70 mm wide at board y = 0.27, 0.62, 0.97, 1.32 m; 60 % open). It is in the FDTD (all 18 coarse runs and 4 fine runs have a plate version) and in the ray model, and it is the reflector for the strings. Plate-versus-no-plate in the coarse FDTD changes the band energies at the close mic by -0.3 to +1.1 dB (60 Hz to 2.4 kHz), at the player mic by up to +2.1 dB. It is rigid and unexcited: the plate's own vibration (not justified without its modes and its coupling at the bridge) is **not** modelled.

### 3. Diffraction and the trusted band

*Fine FDTD.* dx 12.5 mm, dt 1/48000, a 2.65 x 3.1 x 2.4 m sub-domain (0.5 m absorbing layer inside it), 286 board modes (to 3 kHz), 32 min per run on 4 cores: the close microphones with the plate, and without it (ablation runs `runF_openN_*`). Free-field check (`wave_check.py`, point source, rigid floor, analytic p/Q): magnitude within 0.1 dB to 2.5 kHz and 0.7 dB at 4 kHz (coarse grid: 0.3 dB to 2 kHz, 3 dB at 4 kHz); complex relative error 0.03 to 0.19 up to 2 kHz, 0.4 at 2.5 kHz, 0.7 to 1.3 above 3 kHz (coarse: 0.08 to 0.2 at 1 to 1.25 kHz, 0.4 to 0.7 at 1.6 to 2 kHz). **Trusted band: fine grid below about 2.2 kHz (phase error below 0.2), coarse below 1 kHz.** Crossovers (raised cosine in complex pressure): coarse to fine 0.9 to 1.3 kHz, fine to rays 2.2 to 3.0 kHz; without a fine run (player, audience, half and closed lids) coarse to rays 1.4 to 2.0 kHz as in 0.7a. Agreement fine against coarse in 1 to 2.5 kHz (summed over the 191 modes, close mic): -0.3 to -0.95 dB. Agreement of the new (plate) close-mic response against 0.7a below 1.4 kHz: within 0.3 dB per octave band (60 Hz to 1 kHz) and +1.1 dB at 1.0 to 1.4 kHz.

*Ray part.* The 0.7a rule (an obstructed leg contributes nothing) is replaced by the Kirchhoff-Fresnel knife-edge factor K(v) = (1 + j)/2 integral_v^inf exp(-j pi t^2/2) dt on every leg, with v = h sqrt(2 (d1 + d2) / (lambda d1 d2)) from the deepest sample of the leg inside an obstacle (rim: depth below the rim top; lid: distance to the lid edge; plate: distance to the nearest window edge, and, for a strut narrower than 90 mm with openings within reach on both sides, the strip form K(v1) + K(v2) so that struts narrower than the wavelength do not shadow). Test G-T7 (`tests/test_diffraction.py`): K(v) against the exact Sommerfeld half-plane solution within 1.12 dB over +/-14 degrees around the shadow line for kr 60 to 800 (it is paraxial: v uses sin where the exact solution uses 2 sin(x/2)); the ray-marched depth, d1 and d2 against the analytic geometry within 3 % for a synthetic wall. **Level matching:** the ray model of the open board is 4.4 to 5.1 dB (close mics, open lid), 13 to 14 dB (player and audience) and 6 to 15 dB (other lids) below the best FDTD over the converged FEM modes in the overlap (1 to 2.5 kHz with the fine run, 1 to 1.3 kHz with the coarse one): the wave solver also has the rim tray, the lid and the plate, and the ray model has no multiple scattering. To avoid a level step at the crossover the ray part (FEM and statistical modes) is multiplied by that per-microphone factor (`rayleigh_to_fdtd_correction_dB` in the assembly diagnostics). It uses no recording; it is also the least certain number of the model: the validation shows the effect (rows "A3" and "A3 without matching" below).

### 4. Full loaded admittance and the strings' board

Kinematic FDTD (`grand_fdtd --mode kin`): board mode j carries a Gaussian velocity pulse in its own 62.5 ms slot (16 modes per run), the generalised force of the air on every mode is recorded: the modal radiation impedance Z_ij(w) of the first 144 modes (to 2.05 kHz; open lid, plate; 9 runs of 4.4 min). Y_port = Phi (D + Z)^-1 Phi^T.
* Reciprocity: Z is symmetric to 6.4 % median (relative asymmetry; the rest is noise of the float32 force records and the finite slots); it is symmetrised. Passivity: the Hermitian part of Z has negative eigenvalues of noise level (0.7 % of the Frobenius norm of Re Z), projected to zero; then the 88 x 88 Y_port is symmetric to 1e-15 and positive real (smallest eigenvalue of its Hermitian part -6e-16 of the largest) at every frequency. Tests G-T8 on a synthetic box (asymmetry 2 % median, panel loaded mass +0.28 and damping positive, Y_port positive real).
* The exact Y_port is not a bank of real oscillators with positive residues. Two reductions were tried and compared on the own-port mobility of the 88 keys (median |dB| error, vacuum / diagonal loading / eigenvector reduction): 30 to 100 Hz 6.2 / 1.3 / 0.8; 100 to 250 Hz 5.4 / **1.5** / 4.2; 250 to 500 Hz 4.4 / **1.0** / 3.3; 500 to 1000 Hz 2.8 / **1.2** / 2.2; 1 to 2 kHz 1.1 / **0.5** / 1.9. The eigenvector reduction (quadratic eigenproblem with Z frozen at each mode, real part of the rotated port vector; non-proportional part 26 % median) is worse above 100 Hz, so **the table uses diagonal loading for all 144 modes** (added modal mass and damping from Z_ii at the mode's own frequency, vacuum participations; the 0.7a estimate for three modes extended to all). The loaded/vacuum frequency ratio is 0.90 for mode 1 and 0.968 median for the first 20 modes; the damping rate is 1.64 times the structural one (first 20 modes) and 2.29 times (modes 20 to 144): the air is a second damping of the same order as the structure. Modes above 144 keep their vacuum poles.
* What was not done: off-diagonal coupling between modes in the strings' board (the real-oscillator form cannot carry it); only its effect on the own-port mobility is bounded by the numbers above.

## Validation against Salamander (close AB, open lid, no fitting)

Notes A2, C4, C6 at three velocities each (MIDI 40, 60, 92 against Salamander layers 4, 8, 12), 9 comparisons. One level factor per variant matches the C4 v60 close-AB rms (0.05 to 1 s) to the 0.6 render. Full tables (`grand-validation-0.7b.txt`, JSON with all metrics): late window (0.5 to 1.5 s) octave-band energy-share error in dB, mean signed (mean absolute), bands 62-125 / 125-250 / 250-500 / 500-1k / 1-2k / 2-4k / 4-8k / 8k+:

| model | 62-125 | 125-250 | 250-500 | 500-1k | 1-2k | 2-4k | 4-8k | 8k+ | mean abs, all bands |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0.6 Full | -4.6 | +1.3 | +1.3 | -3.0 | -3.8 | -8.8 | +2.5 | -16.3 | 7.5 |
| 0.7a | -9.2 | -1.3 | -2.8 | -1.4 | -1.6 | -7.9 | -20.6 | -81.0 | 17.7 |
| A1 + HF board | -9.2 | -1.4 | -2.8 | -1.4 | -1.6 | -7.5 | -0.6 | -24.3 | 8.9 |
| A2 + strings | -9.2 | -1.4 | -2.8 | -1.4 | -1.6 | -7.5 | -0.7 | -21.7 | 8.5 |
| A3 + plate, fine FDTD, diffraction (level matched) | -7.6 | 0.0 | -1.5 | -0.9 | -2.1 | -2.4 | +7.1 | -17.5 | 7.1 |
| A3 without level matching | -7.7 | 0.0 | -1.5 | -1.0 | -2.1 | -3.8 | +2.4 | -20.5 | 7.3 |
| **A4 = 0.7b (+ loaded admittance)** | -8.9 | +1.3 | +3.3 | -0.1 | +0.4 | +2.5 | +12.6 | -12.0 | 7.6 |

Ablation (change of the mean absolute error, dB, all bands): HF board -8.8; strings -0.3; plate + fine FDTD + diffraction -1.5 (2 to 4 kHz share from -7.5 to -2.4 dB); admittance +0.5 (2 to 4 kHz and 1 to 2 kHz come closer, 4 to 8 kHz overshoots by 5 dB and 125 to 500 Hz change sign). Attack window, decay rates, attack centroid and stereo coherence are in the text file; headline numbers: attack share error 4 to 8k 17.4 dB (0.6) / 20.7 (0.7a) / 8.2 (0.7b), 8k+ 9.3 / 69.0 / 8.1; mean absolute decay-rate error 7.6 / 5.3 / 6.0 dB/s; attack centroid 0 to 30 ms 561 / 523 / 517 Hz (reference 572), share above 2 kHz 0.009 / 0.002 / 0.007 (0.023); L/R coherence reference 0.90 0.69 0.69 0.61 0.67 0.63 (125 to 4000 Hz centres): 0.6 0.89 0.63 0.63 0.91 0.95 1.00; 0.7a 0.81 0.79 0.70 0.95 0.94 1.00; 0.7b 0.65 0.68 0.79 0.83 0.98 0.98 (closer at 250 Hz and 1 kHz, no better above 2 kHz where a few modes radiate and the mics see one source).

**Reading, honestly.** The high-frequency hole is closed (4 to 8 kHz from -20.6 to within 1 dB before the level matching; the spectral shape from 1 to 10 kHz is within about +/-4 dB of the reference once one global level is removed), but 0.7b is **not** better than 0.6 on the late-window mean (7.6 against 7.5 dB), because the full model overshoots the 4 to 8 kHz share by 12.6 dB and keeps a 62 to 125 Hz deficit. Two causes are identified, neither tuned away: (1) the ray-to-FDTD level matching adds 5 dB to everything above the crossover at the close mics (without it the 4 to 8 kHz share is +2.4 dB, at the price of a 5 dB step at the crossover), and whether a rim-tray gain measured at 1 to 2.5 kHz still holds at 4 to 10 kHz is unknown; (2) the air-loaded modes decay about twice as fast (damping 1.6 to 2.3 times the structural one), so the late-window energy of the low bands falls and the high bands gain share; the reference decays slower than the model at 125 to 500 Hz (decay rates -4 -5 -6 dB/s against -7 to -9 for 0.7b), which suggests that the structural loss factor 0.02 plus radiation damping is too high at low frequency, i.e. the 0.02 assumed for the board (not measured) is the first thing to revisit. The 8k+ band stays 12 to 22 dB low: the 0.6 strings stop at 12 kHz and there is no hammer noise (0.8), the board set is calibrated to 16 kHz. Coherence is not improved above 2 kHz.

## Limits (honest)

* The statistical board has no rib or bridge scattering: far-field radiation of the continuation is up to 6 dB below the FEM's across the junction at the audience mic (close mics within 2 dB). The mobility target above 3.2 kHz is an extrapolation (slope -0.14) of the FEM; the loss factor is flat 0.02.
* The ray part is level matched to the FDTD by one factor per microphone (4 to 15 dB), and its diffraction (knife edge per leg, single dominant obstacle, no multiple scattering) is paraxial; shadowed audience and player paths above 2 kHz remain model-level estimates, not solutions. The fine FDTD covers the close microphones only; the player is in the fine domain's absorbing layer.
* Plate geometry, string fan and heights, rim, lid and floor stay nominal assumptions; the plate does not vibrate; the lid does not vibrate (item 5 not attempted); sympathetic (unstruck) strings radiate only in the diagnostic stems (vertical modes below 2 kHz); hammer and action noise (0.8) are absent; the strings' board has the diagonal loading only.
* The IRs are 1 s long (faded over the last 0.15 s): the body cannot ring longer than that in these renders (the slowest board mode is 50 Hz with sigma 3.1/s; 1 s truncates its tail).
* Validation is nine notes of one close-AB pair of one recorded piano; the reference coherence is note dependent; the level factor (0.0059 for 0.7b) is a global gain, not a fit per band.

## Diagnostic renders (`out/piano-mvp-v4/grand/diag/`)

* `body-tap-0.7b.wav` (+ `-order.txt`): the body's reverberation alone, a 1 ms raised-cosine 20 N force tap at the bridge point of A1, C4 and C6 (no strings) through board, air, lid and floor: close AB, player and audience (open lid) for each point, then C4 with the half and closed lids, 2 s each.
* `body-tap-compare.wav` (+ `.txt`): the same C4 tap through the 0.6 board (its 512-mode bank, `piano-body-tap-06`), the 0.7a board and the 0.7b board, close AB.
* `stems-chopin-op28-no4-0.7b.wav` (+ order, `stems-report.json`): the pinned 40 to 66 s excerpt, close AB, open lid, five 26 s stems: (a) full mix, (b) struck strings (bridge force through the body plus their direct radiation), (c) unstruck strings only (excited through the board; their direct radiation, vertical modes below 2 kHz), (d) direct string radiation only, (e) the board without the direct string sound. (b)+(c) and (d)+(e) both reproduce (a) to -320 dB (by construction, float32 records); rms re (a): (b) +0.9 dB, (c) -3.7 dB, (d) -52.4 dB, (e) 0 dB, so the unstruck strings carry more than half of the amplitude under the pedal in this model, and the direct string radiation is 52 dB below the mix over the whole excerpt (it matters in the attack above 4 kHz).
* `string-solo-54-0.7b.wav`, `string-solo-54-pickup-0.7b.wav`, `string-solo-54-energy.png` (+ `-info.json`): key 54 (F#3), struck four times at 51.24, 51.74, 52.21 and 52.65 s (a repeated note), rung sympathetically under the pedal during the rest of the excerpt (its unstruck bridge-force energy is 1.4 times its struck energy). The first file is that string's bridge force through the body plus its direct radiation; the pickup file is its summed modal velocity 20 mm from the bridge (no body), normalised to a peak of 0.5; the plot shows the pickup velocity and bridge-force energy over time with strikes (red) and pedal events.

## Cost and files

Compute (i3-8100, 4 cores): statistical set 3 min (+ shape tables 10 min at 6 mm, 5 min at 4 mm); string tables 25 s; coarse reciprocal run 4.5 min (18 with the plate); fine run 32 min (4); kinematic runs 4.4 min (9); wave check 2 min per grid; one assembly (6 microphones, 1 lid state) 10 min; admittance table 15 min unloaded (47 min under load); validation render per note 20 s per variant (3 variants); whole Prelude Op. 28 No. 4 (105 s) 6 min including convolution; Revolutionary Etude to 101 s 9 min.
Tests added (ctest): `grand-diffraction` (G-T7), `grand-string_radiation` (G-T6), `grand-hf_board` (G-T9), `grand-admittance` (G-T8); with the 0.7a ones 25 tests.
Files: `out/piano-mvp-v4/grand/`: `grand-ab-0.7b.wav` (+ order), `grand-mics-0.7b.wav` (+ order) (24-bit; the float32 raw copies stay in the local output folder, not committed), `chopin-op28-no4-0.7b-{closeab,audience}.wav` (the whole prelude, 0 to 105 s; MIDI by Bernd Krueger, CC BY-SA 3.0 DE, see `mvp/piano-v4/listening/README.md`), `chopin-op10-no12-0.7b-{closeab,audience}.wav` (Revolutionary Etude excerpt 89 to 101 s; same licence and attribution, `listening/ETUDE-README.md`), `grand-validation-0.7b.{json,txt}`, `grand-0.7b-reports/` (admittance, continuity, wave-check, assembly diagnostics), `diag/`.
