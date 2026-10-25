# MVP 4: bridge feedback

## Update 0.5.1: one shared soundboard, sympathetic strings, key-dependent bridge points

Listener feedback on 0.5.0/0.4.9: "still rather bright and small ... lacks wood and
dimension", and "on a real long piano its shape imposes different harmonics on different
strings". Until now every key had a private 128-mode bridge and unstruck strings did not
exist. This iteration changes two things, each with its own setter (before `prepare`), on by
default so the listener can attribute differences.

| What | Setter | Off means |
| --- | --- | --- |
| One shared board; every simulated string coupled to it at its own bridge point | `setSharedBoard` | 0.5.0 private bridges, bit-for-bit |
| Key-dependent bridge points on a bass + treble bridge curve | `setGrandBridgeGeometry` | the 0.5.0 two-column pan blend (shared board only) |
| Unstruck strings with lifted dampers are simulated | `setSympatheticStrings`, `setSympatheticLimit(n)` | no sympathetic ringing |

`piano-midi-render ... --previous` is now **0.5.0** and reproduces the committed
`chopin-prelude-0.5.0-raw.wav` byte-for-byte (verified: `cmp` identical). `--shared-pan` is
0.5.1 coupling with the 0.5.0 bridge positions; `--v049` is the former `--previous`; `--v048`,
`--v046`, `--legacy` are unchanged. `piano-render` takes `previous` (0.5.0), `sharedpan`,
`v049`, `v048`, `v046`.

### Design

1. **Shared board (`grand_board.*`).** The 128 board modes that were private per key are now one
   set. Mode force is `sum_s phi_s,i F_s` and the motion of port `s` is
   `w_s = sum_i phi_s,i r_i`: the discretisation of `pme::bridge::BridgePoint` (SPEC-OSC
   difference form, forces at step n, accelerations `delta_tt w` from increments), generalised
   from one port to 88 (the engine's `BridgePoint` has a single `phi` per mode, so it cannot
   serve several bridge positions; its coefficients come from the same
   `compileBridgeMode`). Each key's `StruckNote` still owns a `BridgePoint`, but that point has no
   modes or conductance: the board's port increment `w^{n+1}-w^n` enters through the engine's
   external-velocity hook (SPEC-BODY Sympathetic path), so the string sees exactly the board's
   `delta_tt w`. Per sample: ports add `F^n`, the board advances, strings advance, then each port
   adds `F^{n+1}` to the radiating modes (as 0.5.0 did). All 512 modes still radiate with the
   MVP 3 zero-order-hold formulation, fed the same forces through the 0.4.9 bridge-mass low-pass;
   modes 129-512 are driven one way and reach the strings only as the passive residual loss, now
   evaluated with each key's own participation. **Documented simplification:** the 128 coupled
   modes exist twice, as mechanical (raw force) and as radiating (low-passed force) states of the
   same equations; this keeps the 0.4.9 high-frequency calibration intact and makes the
   one-voice case equal 0.5.0 (relative difference 2e-8, `piano-check` 0.5.1-T2).
2. **Sympathetic strings.** A string group is simulated while it is struck or while its damper is
   lifted (sustain pedal down, key held, or undamped treble, keys >= F#6) and the board moves its
   bridge point faster than `Piano::wakeVelocity` = 1e-4 m/s (assumption: about -55 dB re a
   loud strike's 0.06 m/s; checked every 32 samples, deterministic, allocation free). An
   unstruck group (`sympathetic_string.hpp`) is the engine's `ModalSegment` equations,
   vertical polarisation only (horizontal modes are never driven at a point without
   perpendicular conductance) and only modes below 2 kHz (the board is coupled to 1.87 kHz;
   truncating a passive system is passive). Checked against the full string model: the same
   A1 strike gives sympathetic forces of 8.0 vs 8.1 N (key 21), 1.8 vs 1.8 N (key 22), etc.
   Strings **start from rest** when they wake: if the pedal goes down while the board is
   already ringing, each string begins to follow the board at that moment (nothing is
   back-calculated), and a string struck while ringing hands its vertical modes over to the
   hammer voice. Releasing the pedal (or the key) damps woken strings with the existing damper
   law and they retire. Idle strings go back to sleep after 1 s with force below 1e-7 N and
   board speed below half the wake threshold. `liftDamper(key)` lifts a damper without a strike
   (used by the demo; there is no MIDI message for it). Cap: `setSympatheticLimit(n)`
   (offline default 88; plug-ins 8, `Piano::pluginSympatheticLimit`). When more strings are
   eligible than slots, those with the highest harmonic coincidence (partials 1-8 within 20 cents,
   weight 1/(m n)) with the loudest sounding struck strings are woken first. This ranking is a
   heuristic.
3. **Key-dependent bridge points.** Mode shapes are the analytic ones of the MVP 3 plate
   (simply supported 1.4 x 1.6 m orthotropic spruce plate with homogenised ribs; Chabassier &
   Durufle RT-0425 material, other numbers nominal) evaluated at each key's bridge point:
   `phi_mn = 2/sqrt(mu lx ly) sin(m pi x/lx) sin(n pi y/ly)`. `generate_grand_board.py`
   re-derives the (m, n) of each table row and checks all 512 omegas against the MVP 3 table to
   1e-12 (`grand_board_table.hpp`); a test shows the shapes reproduce the two old drive columns at
   the old bridge points (0.42, 0.91) and (0.97, 0.58) to 1e-9. **Bridge curve (assumption, not
   measured, no source):** bass bridge keys 21-44 straight from (0.30, 1.15) to (0.55, 0.82) m;
   long (treble) bridge keys 45-108 a quadratic Bezier from (0.62, 0.90) through control point
   (0.95, 0.78) to (1.25, 0.25) m, keys evenly spaced in the parameter (about 1.8 cm per key
   on the bass bridge, 1.5 cm on the treble bridge), at least 0.15 m from the plate edge. Split
   at key 44 is a guess (a real grand's bass bridge usually carries the wound strings). The
   rectangle is **not a grand outline**.
   **B2 body FEM:** `compiler/pme_compiler/body` can mesh an arbitrary outline with rib and
   bridge beams (Gmsh + scikit-fem + SciPy) but those packages are not installed here, and the
   MVP radiation table is built from the analytic plate, so a FEM board would also need a new
   radiation table. Not done; a grand outline with explicit ribs from B2 is the obvious next
   step for the board. Stereo stays the nominal two-microphone radiation (fixed microphones at x = 0.2 and 1.2 m):
   the right/left level of a 0.7-velocity note is within +-2.5 dB for keys 21-60 in both versions,
   and scatters in the treble (key 81: +14 dB in 0.5.0, +11 dB now; key 87: -9 dB now). There is
   no systematic bass-left/treble-right tendency, as in 0.5.0.

### Tests (all pass; `ctest`: 6/6)

- `bridge-check` (extended): 8 strings (keys 36-84) on the shared 128-mode board, energy =
  board modes + every segment. Lossless: maximum relative drift **5.1e-15** (pan columns) and
  **2.0e-14** (grand geometry) over 40 000 steps; lossy: never above the initial energy, final
  45% / 29% of it. Energy moves between strings: up to 56% / 27% of the initial energy sits in the
  seven unexcited strings (lossless), 23% / 11% with damping.
- `piano-check` 0.5.1-T1 key-dependent coupling (shapes reproduce the old columns; all bridge points
  inside the plate margins; adjacent keys differ, distant keys decorrelate, geometry differs from
  the pan blend), -T2 one voice on the shared board equals the 0.5.0 bridge to 1e-6 for keys
  33/60/84, -T3 sympathetic transfer (pedal, held key: an unstruck octave string wakes and has
  >1e-6 N force; damped, disabled, limit 0: it does not; pedal release damps it; pedal pressed
  after the strike wakes it later; two pianos produce identical bits).
- The existing LV2 playback test (no `operator new` during `run()`, identical output to the
  engine) passes with sympathetic strings on (limit 8).

### Results

CPU (single thread, -O3, no SIMD, this machine):

| Case | 0.5.0 | 0.5.1 |
| --- | --- | --- |
| Chopin excerpt offline (66 s simulated, 88 voices, pedal heavy) | 33 s (0.79x excerpt length) | **93 s** (0.28x excerpt length, 0.71x real time overall) |
| Full keyboard cluster, pedal, 88 struck | 23.0 s per 4 s | 22.1 s per 4 s |
| Pedal down, one loud A1, 87 sympathetic strings | 4.04x real time | **0.80x real time** (worst case) |
| Plug-in settings (12 voices, cap 8), Chopin 52-58 s | 1.83x real time | 1.62x real time |
| Plug-in settings, 12-key cluster with pedal | 1.12x real time | 1.05x real time |

The plug-ins are capped at 8 sympathetic strings; a 12-key cluster with a cap of 16 runs at 0.99x.
The cost per awake unstruck string is dominated by its 128-mode port (force, readout) rather than
its few modes; threading or SIMD would be the next lever and are not used.

Reference calibration (`reference-calibration-0.5.1.json`, 24-bit `calibration-*-0.5.1.wav`
plus `-0.5.1-pan.wav`, mean attack energy-share error above 2 kHz): **0.4.9 5.68 dB, 0.5.1-pan
5.63 dB, 0.5.1 3.80 dB.** The change from 0.4.9 to the new geometry is partly luck of the
modal sampling at each key's bridge point (C6 velocity 40 improves 13.3 to 4.5 dB, C6 velocity
92 worsens 1.5 to 5.0 dB): with only nine reference points do not read this as a calibrated
gain. Octave-band shares (band lower edges 62.5 ... 8000 Hz, model minus Salamander, dB), velocity 60:

```
A2 attack 0.4.9  +0.5 +0.8 -4.1 -0.7 -7.0 +4.5 +20.5 -17.5   0.5.1 -7.5 +3.7 +0.6 -3.5 -9.8 +2.6 +23.8 -14.3
A2 late   0.4.9  -0.1 +4.7 -6.5 -10.9 -10.6 -1.5 +8.9 -30.0  0.5.1 -6.8 +9.9 +0.5 -17.6 -13.9 -5.0 +11.6 -25.1
C4 attack 0.4.9  -5.1 +10.4 +2.2 -4.2 -0.5 +4.6 +10.5 -5.1   0.5.1 -3.1 +12.0 +1.0 -1.2 -1.4 +4.2 +12.4 -0.6
C4 late   0.4.9 -25.3 -31.8 +4.9 -8.8 -8.7 -3.1 -1.3 -19.1   0.5.1 -21.2 -31.1 +5.2 -17.3 -10.8 -7.2 -3.2 -13.4
C6 attack 0.4.9  -8.3 +1.4 -10.0 -6.6 -0.3 +5.3 +17.5 -8.6   0.5.1 -4.0 -5.8 -11.7 -10.6 +0.2 -2.2 +9.5 -9.8
C6 late   0.4.9 -14.6 -36.2 -57.3 -47.2 -0.1 +11.5 +13.2 -11.5 0.5.1 -16.4 -41.3 -63.1 -52.2 +0.4 -12.6 +1.5 -13.7
```

Velocities 40 and 92 and the `-pan` variant are in the JSON. **The low-band deficit is not
fixed.** C4 and C6 late 125-500 Hz stay 28-65 dB below the reference (C4 late 62-125 Hz improves
by about 4 dB, C6 does not), and the attack 62-500 Hz of C6 gets slightly worse. This is the
honest outcome of the experiment: the Salamander notes were recorded with the dampers down on
all other keys and no pedal, so for an isolated note only the undamped treble (>= F#6, here an
assumed boundary) can ring sympathetically in the model; damped strings, which are not simulated,
leak a little in reality. The reference's late-window 62-500 Hz content of C6 is about 15 dB above
the recording's own end-of-file floor, so it is not just converter noise, but whether it is key-bed or
action thump, room, damper leakage or longitudinal string motion cannot be told from these
files. The shared board moves energy between bands (late 500-1000 Hz share of A2 and C4 falls 7-9 dB;
late C6 2-4 kHz falls 24 dB and 4-8 kHz 12 dB, overshooting the reference) and the 4-8 kHz attack
excess is not removed (A2 +24, C4 +12, C6 +9.5 at velocity 60).

Chopin Op.28 No.4 excerpt, same fixed +12.04 dB listening gain: `chopin-prelude-0.5.1.wav` (+
`-raw.wav`, `-audit.json`, copied to `chopin-prelude-latest.wav`) next to the re-rendered
`chopin-prelude-0.5.0.wav`. RMS 0.013739 vs 0.013695 (+0.03 dB), peak 0.156 vs 0.145,
no voice steals, 0 numerical faults. Octave-band level change (0.5.1 minus 0.5.0, 31 Hz to 8 kHz
octaves): +9.2, +2.1, -0.8, +1.8, +0.9, -2.2, -1.5, 0.0, +1.5 dB; the lowest octave rises because
pedalled bass strings now ring with the board. Left/right balance +1.06 dB vs -0.47 dB.

`sympathetic-demo-0.5.1.wav` (and `-0.5.0.wav`, `-0.5.1-pan.wav`, 14 s, `piano-sympathetic-demo`,
fixed +12.04 dB gain): 0-6.5 s C3+E3+G3 have their dampers lifted but are never struck; staccato
C4 at 1 s and C5 at 3 s. 7-14 s pedal down and one loud A1.

| Window | 0.5.0 | 0.5.1-pan | 0.5.1 |
| --- | --- | --- | --- |
| C4 staccato 1.0-1.3 s | -23.7 dBFS | -24.5 | -20.5 |
| after C4, 1.5-2.9 s | **-60.2** | -38.0 | **-39.7** |
| after C5, 3.5-6.4 s | -62.8 | -45.0 | -48.9 |
| A1 with pedal, 8-10 s | -24.6 | -25.0 | -24.5 |

In (a) the unstruck C3-E3-G3 chord rings about 20 dB above the 0.5.0 silence after a staccato note.
In (b) the total level hardly changes (the pedalled A1 already rings by itself); above 1 kHz
the 0.5.1 tail is 2.7 dB lower, below 200 Hz 1.1 dB higher.

### How to listen, and what to listen for

Play `sympathetic-demo-0.5.0.wav` then `-0.5.1.wav`: after each staccato note the chord should
bloom softly (octave, fifth and third partials) instead of stopping dead. Then the Chopin pair
(`chopin-prelude-0.5.0.wav` vs `-0.5.1.wav`, same gain): look for a fuller low register with
the pedal, less transparent sustain, and key-to-key tone differences from the bridge geometry.
`--shared-pan` isolates the board coupling from the geometry. What it cannot
yet do: a wood-like knock, a grand outline, damped-string leakage, or any nonlinearity.

Known limits: the wake threshold, the 2 kHz cut, the treble undamped boundary, the
bridge curve, the board outline and all board numbers are assumptions; the radiating low modes
duplicate the coupled ones; sympathetic strings above 1.87 kHz are driven by nothing; the rank
heuristic is not physics; the sympathetic forces are large (up to 8 N peak at a bass string
after a loud A1, 10-20 dB below the struck force) and nothing here is tuned to a measurement of a real piano.

### Notes for 0.5.2 (string tension modulation)

The core has the A7 code, not exercised here: `pme::nl::TensionModulationPotential<S>`
(`engine/src/nonlinear/include/pme/nonlinear/tension.hpp`; `configure(k2, c)` with
`c = (EA - T)/(2 L)`, `deltaT(z)`, `value`, `gradient`, shares the segment's psi as a rank-one
term) and `pme::string::GeometricSegment<S>` (`engine/src/string/include/pme/string/longitudinal_segment.hpp`,
tension modulation and longitudinal modes with their bridge coupling, `BridgeCoupling`, per-segment
flags). Cost of the potential is O(2N) per sample per string group (one sum of `k_n^2 q_n^2`
over both polarisations, then the gradient), small against a 128-mode board port. Obstacles:
the potential expects the layout V modes, H modes, lumped coordinates, whereas `StruckNote`'s
SAV segment carries the vertical modes and the hammer only, so using it means adapting
`StruckNote` (engine, read-only here) or composing a note from `GeometricSegment`; the light
unstruck strings of this iteration would need the potential too to keep pitch glide consistent;
the horizontal modes become driven by the nonlinear coupling, so the bridge point would need
the perpendicular polarisation (conductance or board port in that direction), which this board
does not have.

## Update 0.5.0: full pedal set (polyphony, una corda, undamped treble)

The listener found 0.4.9 "small". Three cheap changes with no new physics; each has its
own setter (before `prepare`) and is on by default. `--previous` on `piano-midi-render`
and `piano-render` reproduces **0.4.9 bit-for-bit** (verified: the re-rendered
`chopin-prelude-0.4.9-raw.wav` and listening WAV are byte-identical to the committed
files); `--v048` is the old 0.4.8 mode; `--v046` and `--legacy` are unchanged.

1. **Polyphony.** `setVoiceLimit(1..88)`; class and real-time plug-ins (CLAP, LV2) stay at
   12 voices; offline renderers use 88 (one voice per key, no stealing). Pinned Chopin
   excerpt: **13 voice steals at 12, 0 at 88**; RMS 0.013695 vs 0.013695 (peak identical
   0.145065). Offline speed at 88: 66 s simulated (40 s pre-roll + 26 s) in 33.0 s, i.e.
   **2.0x realtime single-thread** (0.79x the 26 s excerpt length; 12 voices: 32.4 s).
2. **Una corda (CC67 >= 64).** `setUnaCorda`, `softPedal(bool)`; wired in `dispatchMidi`
   (CLAP, LV2, MIDI renderer; the event validator now also accepts CC67). Uses the engine's
   grand una-corda map (SPEC-HAM Una corda) through `StruckNote::launch(nu, shift=1)`:
   stiffness `K(1-s(1-kappa_uc))`, exponent `p + s*dp`, and the string at index 0 leaves the
   hammer with weight `1-smoothstep(s)`. The shift is read at launch only, so a hammer in
   flight is never moved (tested). The preset has no una-corda data, so the **engine
   defaults are assumed**: s0 0.3, s1 0.9, kappa_uc 0.6, dp 0, leaving string 0 (not
   measured). Effect: 3- and 2-string keys strike 2 and 1 strings with 0.6x felt stiffness;
   1-string keys (bass, key <= 32) keep their one string and only get the softer felt
   (the engine has no leaving string for a single string). Chopin fixture: no CC67 events
   (only CC64, 7, 10, 91), so the pinned excerpt is unchanged by this item.
3. **Undamped treble.** Keys >= 90 (F#6) ignore note-off damping, with or without sustain;
   they ring until natural decay or voice sleep. Boundary is an **assumption**
   (`firstUndampedKey`, no measurement): grands commonly stop the dampers around F#6/G6,
   about the top 1.5 octaves. The pinned excerpt uses keys 35-84, so it is unchanged here.
   The released radiating board still rings after a damped release, so tests compare the
   0.1-0.2 s window and whether the string voice survives.

Tests (`piano-check`): voice-limit range/steals/default, una corda on keys 24/40/60
(measurable, softer for multi-string, pedal after strike harmless, CC67 release, disabled
pedal), undamped boundary and damped-key invariance. Isolated single notes below key 90
without the pedal are unchanged, so the reference calibration was not rerun
(`reference-calibration-0.4.9.json` still applies).

## Update 0.4.9: calmer highs (bridge mass and saturating string loss)

Listener feedback on 0.4.8: "still rather bright and small, like a harpsichord and a
plastic upright; lacks wood and dimension". Octave-band shares against the Salamander
C5 recordings (see [reference](reference/README.md)) showed 4-8 kHz energy 10-30 dB
too high in the first 250 ms. This iteration changes only the high-frequency path;
the **low-frequency body deficit (knock, low modes) is deliberately untouched** (0.5.0).

### Diagnosis (measured, not assumed)

Bridge force (string+hammer, before the board) and radiated pressure were rendered
separately for A2, C4 and C6 and compared with the reference shares:

- **Board damping (sigma in `kSoundboardModes`) is not the cause.** The 512 modes have a
  constant loss factor 0.02 and overlap strongly above ~1 kHz (bandwidth 0.02 f vs mean
  spacing ~14 Hz), so the mean response there is set by modal density, not by damping.
- **Hammer spectrum is smooth** (A2 force envelope: -56 dB at 4 kHz, -82 dB at 8 kHz re
  peak), but felt stiffness and Hunt-Crossley mu (0.5 s/m, a declared MVP assumption)
  both move the attack 4 kHz band strongly (K x0.3: -14 dB; mu 0.5 to 0.2: -5 dB).
  Not changed here: that was the 0.4.8 voicing lever and softening further worsens the
  decay mismatch below.
- **The board adds +7 dB (2-4 kHz) and +12 dB (4-8 kHz) to the A2 attack share**: white
  noise through the radiation path rises about 8 dB from 500 Hz to 4 kHz. Radiation is
  proportional to modal acceleration (+6 dB/octave for a flat point mobility), and the
  board is driven directly by the bridge force with no bridge-top inertia.
- **String HF decays too fast.** Attack-to-late (0.5-1.5 s) change of the 4-8 kHz band:
  A2 -12.5 dB model vs +0.5 reference; C4 -14.7 vs -3.7. The report's `eta*T/(rho*A)*k^2`
  viscosity (`preset.cpp`) gives about 1.6 1/s at 4 kHz and 6.6 1/s at 8 kHz for C4.
  A static low-pass alone fixes the attack but leaves the late window 15 dB too dark.
- **The 8-16 kHz band in 16-bit WAVs is quantisation noise**, not model output
  (unquantised A2 8 kHz attack is -5 dB, quantised +17 dB). Calibration renders are now
  24-bit (`piano-render ... --24bit`); listening WAVs are unchanged (16-bit).

### Change (two linked mechanisms, each with its own switch)

1. **`setBridgeMassLoading`**: a series bridge-top mass m on a board point of real
   mobility Y passes `F/(1 + i omega m Y)` to the board (one-pole low pass, corner
   `1/(2 pi m Y)`). Y = 1.47e-3 m/(N s) is the infinite orthotropic plate value
   `1/(8 sqrt(sqrt(Dx Dy) mu))` of the MVP 3 board; **m = 55 g effective is an
   assumption** (corner 1.97 kHz; 1.4-2.0 kHz tested). It is applied to the summed
   bridge force entering the soundboard, not to the output. The string still sees the
   128-mode bridge motion unchanged (the string-side reaction is not mass-loaded).
   Rationale: bridge inertia is a recognised influence on piano bridge admittance
   (see Conklin, "Design and tone in the mechanoacoustic piano", JASA 1996; cited for
   context, the numeric mass is not taken from it or from any measurement).
2. **`setSaturatingStringLoss`**: the report's viscous term follows k^2 up to
   `kStringLossCornerHz` = 2.5 kHz, then keeps a constant loss factor (sigma proportional
   to omega, matched at the corner). Rationale: the k^2 law of the report/JASA 2013 is fitted
   to low partials; measured string loss factors grow far more slowly at high frequency.
   The corner is an assumption (1.0-3.5 kHz tested); Ru, bridge and residual losses unchanged.

Without (2), (1) makes the late window 4-8 kHz 7-16 dB too dark; without (1), (2) leaves
the attack unchanged. Mono-mix 4-8 kHz band, model minus reference, dB (attack / late):

| note | 0.4.8 | string loss only | bridge mass only | both (0.4.9) |
| --- | --- | --- | --- | --- |
| A2 | +18.2 / +1.3 | +18.9 / +7.3 | +10.3 / -6.6 | +10.9 / -1.0 |
| C4 | +11.6 / -7.4 | +12.6 / +1.7 | +2.9 / -15.8 | +3.9 / -7.0 |
| C6 | +13.8 / +3.1 | +15.2 / +17.8 | +5.9 / -4.7 | +7.3 / +9.2 |

### Result (24-bit renders, stereo power, shares re total, model minus reference in dB)

Mean attack >2 kHz energy-share error (nine points) falls from **9.44 to 5.68 dB**
(`reference-calibration-0.4.9.json` holds all windows and bands). 4 kHz band, 10-250 ms:

| note, layer velocity | 0.4.8 | 0.4.9 |
| --- | --- | --- |
| A2 40 / 60 / 92 | +19.8 / +27.4 / +33.0 | +12.9 / +20.5 / +26.0 |
| C4 40 / 60 / 92 | +24.6 / +17.3 / +11.1 | +17.9 / +10.5 / +4.1 |
| C6 40 / 60 / 92 | +27.7 / +23.1 / +19.5 | +22.5 / +17.5 / +15.0 |

Full octave table, velocity 60 (band lower edges 62...8000 Hz):

```
A2 attack 0.4.8  +0.4 +0.8 -4.0 -0.4 -5.4  +8.3 +27.4  -7.5    0.4.9 +0.5 +0.8 -4.1 -0.7 -7.0  +4.5 +20.5 -17.5
A2 late   0.4.8  -0.1 +4.7 -6.3 -10.4 -8.6 +2.2 +11.8 -34.6    0.4.9 -0.1 +4.7 -6.5 -10.9 -10.6 -1.5 +8.9 -30.0
C4 attack 0.4.8  -5.3 +10.2 +2.1 -4.0 +1.0 +9.2 +17.3  +3.6    0.4.9 -5.1 +10.4 +2.2 -4.2 -0.5  +4.6 +10.5  -5.1
C4 late   0.4.8 -25.4 -31.9 +4.8 -8.4 -7.3 +0.6  +0.0 -30.0    0.4.9 -25.3 -31.8 +4.9 -8.8 -8.7 -3.1  -1.3 -19.1
C6 attack 0.4.8 -10.0  -0.3 -11.5 -7.6 -1.0 +8.4 +23.1  -3.4    0.4.9 -8.3  +1.4 -10.0 -6.6 -0.3  +5.3 +17.5  -8.6
C6 late   0.4.8 -16.0 -37.5 -58.6 -48.6 -0.4 +13.5 +9.7 -19.8    0.4.9 -14.6 -36.2 -57.3 -47.2 -0.1 +11.5 +13.2 -11.5
```

The +-6 dB target at 4 kHz is **not met** in the attack (only C4 at velocity 92, +4.1, is within
+-6; the others are +10 to +26 dB), and it cannot be judged at 8 kHz: the reference there is within ~10 dB of
its own noise floor and the model's 8-16 kHz content comes only from partials above the
last board mode (7.2 kHz). The 1-2 kHz attack stays within -7.5 dB (A2 -7.0 at velocity 60,
-7.5 at 92; C4/C6 within -1.7); that band lost 1-2 dB. Late 4 kHz is within +-6 dB for
C4 (all velocities) and A2 at velocity 40, but A2 at 60/92 is +8.9/+15.9 and C6 +9 to +17 dB. Shares are relative: the
C4 and C6 low bands (62-250 Hz and 250-500 Hz) remain 25-60 dB low, so every other band
looks high. Reference late-window HF is also contaminated by the recording's noise
floor above ~4 kHz for C6. These are signal measurements, not a listening endorsement.

Compare `out/piano-mvp-v4/chopin-prelude-0.4.8.wav` with `chopin-prelude-0.4.9.wav`
(identical source, tempo, velocities, pedal, excerpt and +12.04 dB gain; 13 voice
steals at the unchanged 12-key limit). `chopin-prelude-latest.wav` is 0.4.9. RMS is
0.0137 vs 0.0142 (-0.3 dB). `C4-reference-0.4.8-0.4.9.wav` replaces the old
diagnostic clip. `piano-midi-render ... --previous` reproduces 0.4.8 bit-for-bit
(checked against the committed 0.4.8 WAV), `--v046` reproduces 0.4.6, `--legacy`
0.4.4; `piano-render ... previous|v046`, `piano-hammer-audit --previous|--v046` match.
The test `piano-behaviour` now checks the saturated viscosity law (identical below the
corner, constant loss factor above it, passive, monotone), the bridge-mass DC gain and
corner, and that the 0.4.9 attack has at least 25% less fourth-difference
(high-frequency) energy than 0.4.8 for A2 and C4. LV2 URI unchanged; name is now
**Piano MVP 4 - Calmer highs**.

## Update 0.4.8: reference-guided felt voicing

The listener finds 0.4.6 closer, but still too metallic and lacking wooden body
character. We now compare against nine pinned original lossless recordings from
Alexander Holm's **Salamander Grand Piano** (Yamaha C5), covering A2/C4/C6 at three
strengths. The [reference instructions, attribution and limits](reference/README.md)
are committed, along with per-file source hashes. Audio is a calibration reference;
the instrument still synthesises its output without playing samples.

This iteration reduces total contact stiffness to **35%** in the bass/mids through
C5, returning smoothly to the original value between C5 and C6. It keeps the
hammer mass, nonlinear exponent, velocity map, string geometry, board response
and output gain fixed. The first full-range soft-felt candidate was rejected in
the treble because it moved the attack spectrum further from the reference.

Middle C's medium-strike contact grows from about 2.01 ms to 2.21 ms (audit velocity
72); softer felt reduces high-frequency excitation at the source. This is a voicing
choice, not a claim that the tabulated source K was wrong. Contact duration's role
in spectral balance is described by
[Askenfelt and Jansson](https://www.speech.kth.se/music/5_lectures/askenflt/stricont.html).
Adding a darker tone alone does not establish the missing wooden body sound.
The soundboard remains nominal and its response still needs independent calibration.

Listen to `out/piano-mvp-v4/chopin-prelude-latest.wav` (26 seconds), comparing
`chopin-prelude-0.4.6.wav` with `chopin-prelude-0.4.8.wav`. The source/event checksums,
tempo, velocities, pedal, excerpt and fixed +12.04 dB listening gain are unchanged.
The separate 24.75-second `C4-reference-previous-soft-felt.wav` compares actual piano,
old model and candidate at each strength; it matches initial levels only for timbre
comparison. The original FLAC source is not altered.

`piano-hammer-audit` measures contact duration and force across six registers and
three strengths. A new test requires shorter contacts and larger peak forces for
harder strikes. `piano-midi-render ... --previous` reproduces 0.4.6;
`--legacy` reproduces 0.4.4. The LV2 URI is unchanged; its name is now
**Piano MVP 4 - Soft felt**. Historical outputs below retain their version labels.

## Update 0.4.6: omitted bridge-mode losses and a fixed classical excerpt

The radiation path already contained 512 soundboard modes, while the real-time
mechanical bridge retained only the lowest 128 (through 1868.6 Hz). This truncation
underrepresented the high-frequency energy escaping the strings. This iteration
adds the **real mechanical admittance of modes 129–512** as a passive, diagonal
string-loss approximation, calculated at each partial frequency during prepare.
There is no extra audio EQ, modulation, reverb or playback-time modal work.

For a mass-normalised board mode with drive phi, natural frequency Omega and
decay sigma, the residual conductance at string frequency omega is:

```
Re Y = sum(phi^2 * 2*sigma*omega^2 /
           ((Omega^2 - omega^2)^2 + (2*sigma*omega)^2))
extra string sigma_n = rhoA * omega_n^2 * g_n^2 * Re Y / 2
```

The first expression is the dissipative part of the omitted modal mobility. The
second equates average bridge power to the string mode's energy loss. It is a
weak-coupling approximation: it does not reproduce the omitted modes' reactive
pitch shift or cross-string correlations. The explicit low 128 modes still couple
the strings. The underlying board is still nominal, not a measured piano; this
is a test of the metallic-ring hypothesis, not a claim of calibrated realism.
The modal-mobility approach is discussed by
[Boutillon and Ege (2013)](https://ppi-lms.polytechnique.fr/xavier.boutillon/Annexes/Boutillon2013.pdf).

For the same middle-C strike, energy above 2 kHz in the 0.5–1 s window falls from
13.81% to 3.22% of that window's spectral energy. Initial 50–250 ms RMS changes by
only −0.05 dB. These are signal measurements, not a listening assessment.

New listening outputs use **Chopin, Prelude Op. 28 No. 4**, MIDI performance by
Bernd Krueger. The [fixed source, licence and validation](listening/README.md)
are committed. Listen to `out/piano-mvp-v4/chopin-prelude-latest.wav` (26 seconds),
or compare `chopin-prelude-0.4.4.wav` and `chopin-prelude-0.4.6.wav`. Both use the
same source notes, velocities 18–92, tempo map, pedal, excerpt, and fixed +12.04 dB
listening gain. Source time 40–64 s is followed by a two-second release.
`chopin-prelude-0.4.6-audit.json` records the source/event/WAV checksums and render
reports. There are 13 voice steals under sustain in this excerpt at the unchanged
12-key limit. This limitation is shared by both comparison versions.

Reproduce with `render_classical.py` as described in the fixture README.
`piano-midi-render ... --legacy` and the single-note renderer's `unisons` mode
disable only this new residual loss, preserving the 0.4.4 model.
The installed LV2 keeps its URI and is named **Piano MVP 4 - Bridge loss**.
Previous demos and comparison files below remain labelled historical baselines.

## Update 0.4.4: coupled unison strings

This update supersedes the single-string description below. MIDI keys 21–32 use
one string, 33–51 use two, and 52–108 use three. These register boundaries are
explicit prototype assumptions, not measurements of the source piano. Each key
uses one shared full-mass hammer and one shared moving bridge; every string has
its own compiled modes, tension, loss terms and state. Felt stiffness is divided
between the contacts, treating the published K as the whole contact patch. That
interpretation still needs experimental calibration. The tuning offsets are
±0.4 cents for pairs and −0.8/0/+0.8 cents for triples, also declared assumptions.

All strings contribute to radiation and all are damped on key release. Cross-key
sympathetic ringing and calibrated soundboard geometry remain unfinished. The
soundboard, output gain and velocity curve are unchanged. Polyphony is now 12
active keys to control the additional CPU cost; oldest keys are stolen at the limit.

Listen to `out/piano-mvp-v4/coupled-unisons-demo.wav`, then
`single-vs-unisons-C4.wav` (single at 0 s, new unisons at 5.5 s). Only the initial
50–250 ms RMS is matched in that comparison; later decay is preserved. The
early/late energy change is −9.92 dB for the single versus −14.25 dB for the group.
This demonstrates changed decay, not verified acoustic-piano realism.
`velocity-soft-medium-hard.wav` plays velocities 32/72/112 at 0/6.5/13 s using
one common gain; the updated soft/hard RMS difference is 14.28 dB.

Reproduce with `python3 mvp/piano-v4/unison_compare.py out/piano-mvp-v4` and
`python3 mvp/piano-v4/velocity_check.py out/piano-mvp-v4`.
The renderer's `single` mode retains the previous coupled single-string control.
The installed LV2 keeps its existing URI and is named **Piano MVP 4 - Coupled unisons**.
All five CTest checks pass, including 88 keys at 44.1/48/96 kHz and sample-exact
LV2/core playback. Actual Carla peaks at velocities 32/72/112 were
0.012155/0.042394/0.082726. At the 12-key limit, the offline plug-in stress test
rendered 10 seconds in 9.38 seconds on this machine: headroom remains tight and
this is not a guarantee of dropout-free live playback.

## Original 0.4 baseline

This iteration changes the mechanical interaction between the string and bridge.
The bridge can now move under the string's force, and its acceleration feeds back
into the string. Harmonics can therefore decay at different rates as energy enters
the damped body. It is not a new reverb, global envelope or output EQ.

The published string/hammer inputs, one string per key, tuning, soundboard radiation
filter, stereo positions, output gain, velocity curve and MIDI performance remain
those of MVP 3. A fixed-bridge control is reproduced by the same executable.

## How to listen

Built outputs are committed under `out/piano-mvp-v4/` on branch `mvp/piano`.

Start with **`C4-bridge-AB.wav`** (10.5 seconds):

- **A, 0–5 seconds:** fixed bridge, reproducing MVP 3.
- **B, 5.5–10.5 seconds:** moving bridge and feedback, the new version.
- A half-second silent gap separates them. Each note is held for four seconds,
  then released, followed by one second of tail.

Initial stereo RMS over 0.05–0.25 s is matched to -24 dBFS. We deliberately do not
match the whole tail: that would obscure the decay change being tested. Keep your
listening volume constant within each pair. RMS matching approximates equal initial
level, not exact perceived loudness.

Then try **`registers-bridge-AB.wav`** (32.5 seconds):

| Register | Previous version A | New version B |
| --- | --- | --- |
| Low A2 | 0–5 s | 5.5–10.5 s |
| Middle C4 | 11–16 s | 16.5–21.5 s |
| High C6 | 22–27 s | 27.5–32.5 s |

Individual `A2-bridge-AB.wav` and `C6-bridge-AB.wav` pairs are also included.
`piano-v4-demo.wav` uses the same melody and sustain-pedal chord as earlier demos,
at the playable instrument's fixed gain. Raw `*-fixed-raw.wav` and
`*-coupled-raw.wav` files preserve unnormalised output for analysis.

## Play it

Load **`piano-mvp-v4.clap`** (Linux x86-64) in a CLAP host. It appears as
**Piano MVP 4 — Bridge feedback** and can coexist with earlier versions.
Add the output directory to the host's plug-in search path and rescan.

Use MIDI keys 21–108, velocity, CC64 sustain. CC120 clears sound and CC123 releases
keys while respecting sustain. There is one fixed preset, no custom GUI or pitch
bend. Start testing at a 512- or 1024-sample buffer; try individual notes first,
then chords, then chords with sustain. Solo one version at a time when comparing
in a DAW. The instrument still has a 16-key limit and immediate oldest-key stealing.

## Feedback that will guide the next change

Please identify **A or B**, then separate these aspects:

1. **Attack:** does it sound hammered, plucked, metallic, clicky or too soft?
2. **Sustain and decay:** does the new note ring naturally, still sound electric,
   become hollow/buzzy, or die too quickly? Pay particular attention to the upper
   note and to 0.5–3 s after the strike.
3. **Register:** which of low A2, middle C4 and high C6 improved or worsened?
4. **Playing behaviour:** for the plug-in, report clicks, dropouts, missing notes,
   stuck sustain, host name, sample rate, buffer size and approximate chord size.

An example of useful feedback: "Middle C: B is closer to piano, but the attack is
still plucked; high C: B dies too fast; bass: little difference." No measurements
or technical vocabulary are required. If decay is too short, tune bridge/body
mobility before adding more strings. If decay improves but the sound remains thin,
the next planned experiment is coupled unison strings.

## What the checks show

All three suites pass: source/keyboard and note behaviour at three sample rates,
coupled-energy tests, and dynamic CLAP-host playback. Full-velocity chords and
numerical-fault counters are checked. Undamped linear string/bridge systems remain
within about 1.2e-14 of initial energy in the tested cases; damped cases lose
energy. The fixed middle-C control WAV is byte-identical to MVP 3.

Late/early level change, using 2–2.5 s versus 0.25–0.75 s:

| Note | Fixed bridge | Moving bridge |
| --- | --- | --- |
| A2 | -1.86 dB | -2.71 dB |
| C4 | -3.08 dB | -9.92 dB |
| C6 | -9.28 dB | -26.04 dB |

These measurements describe changes in decay, not a realism score or measured
piano targets. `comparison-metrics.json` also records energy near the first three
partials; those are broad spectral bands, not isolated-mode fits.

On the development machine the final offline host check processed 10 seconds of
16-key audio in about 7.25 seconds. An initial 512-mode mechanical bridge took
about 15 seconds, so it was reduced to **128 modes, up to 1868.6 Hz**, while the
sound-radiation stage retains **512 modes, up to 7.22 kHz**. Offline throughput is
not a DAW dropout guarantee. The reduction omits high-frequency bridge resonances;
no fitted residual mobility has been added.

## Physical scope

The existing core `BridgePoint` and `StruckNote` run the explicit A4/A5 per-sample
order, with moving-support inertial forces and the static `-(T/L)*w` force term.
The local mode shapes are the same weighted blend of the two nominal bridge
positions used for radiated sound. Frequencies and damping come from MVP 3's
analytical plate, not a measured grand piano.

Each key has its own local mechanical model. The global radiation stage sums their
forces, but motion caused by one key is not yet fed into another key. This is local
self-feedback, not full inter-key sympathetic resonance. Strings within a unison
group and two-stage decay are not implemented yet.

The conservative-energy test isolates the linear coupling after excitation; it does
not prove exact energy behaviour of the whole struck/damped instrument during
hammer contact. The existing core's modal-only hammer contact omits the moving
support's lifting displacement at the strike point (SI-0028), and the approximation
to felt damping remains from MVP 2. Key-release damping, the nominal soundboard
and immediate voice stealing also remain approximations.

## Reproduce

From the worktree root:

```bash
cmake -S mvp/piano-v4 -B out/piano-mvp-v4 -DCMAKE_BUILD_TYPE=Release
cmake --build out/piano-mvp-v4 -j2
ctest --test-dir out/piano-mvp-v4 --output-on-failure
python3 mvp/piano-v4/compare.py out/piano-mvp-v4
out/piano-mvp-v4/piano-render out/piano-mvp-v4/piano-v4-demo.wav
```

The renderer takes `[output.wav] [MIDI-key or -1 for demo] [fixed|coupled] [velocity]`.
Example soft middle C: `piano-render soft.wav 60 coupled 0.25`.
Provide `-DCLAP_INCLUDE_DIR=/path/to/clap/include` at configure time for the
plug-in, using the same pinned CLAP 1.2.10 headers as MVP 2/3.

The body and preset implementations are shared with MVPs 3 and 2 respectively.
Existing versions and audio are unchanged. CMake caches and object files are not
committed; Linux test executables, plug-in and listening files are included.

## LV2 version

`out/piano-mvp-v4/piano-mvp-v4-lv2-linux-x86_64.zip` contains a complete Linux
LV2 bundle of the same instrument. Extract it into `~/.lv2/` and restart/rescan
Carla or Ardour. The directory must be `~/.lv2/piano-mvp-v4.lv2/`, containing
the `.so` binary and both `.ttl` files together. Do not install just the `.so`.
It is listed as **Piano MVP 4 - Bridge feedback**. Route MIDI into it and connect
the two audio outputs to playback. There is no custom GUI; use the host's keyboard
or a MIDI controller. The format change does not alter the sound model.

The LV2 bundle has one MIDI Atom Sequence input and two float audio outputs, and
requires the host's standard `urid:map` feature. It accepts default or explicit
frame-based event timestamps; MIDI events are applied at their sample offsets.
Explicit beat timestamps and truncated or malformed MIDI events are ignored safely.
In the audio callback, otherwise unspecified time units use the frame-time default.
Activation resets the instrument. CLAP and LV2 share the MIDI dispatcher and core.

The LV2 callback is tested against the core sample by sample, with identical
float output at 44.1/48/96 kHz for velocity, timed notes, sustain, panic and reset.
The test also checks malformed events and C++ operator-new allocations during
playback. Metadata passes `lv2_validate`. Carla's installed native discovery tool
recognises the bundle as a synthesizer with one MIDI input and two audio outputs;
this is discovery verification, not a claim of interactive GUI/audio-device testing.

The existing standalone build now requires LV2 development headers on Linux by
default. Set `-DPME_MVP_BUILD_LV2=OFF` to build only the renderer/CLAP without them.
Use `-DLV2_INCLUDE_DIR=/path/to/include` for headers in a nonstandard location.
Build and package with:

```bash
cmake --build out/piano-mvp-v4 --target piano-lv2-package -j2
ctest --test-dir out/piano-mvp-v4 -R piano-lv2 --output-on-failure
```

### Carla silence compatibility fix (LV2 version 0.4.2)

The original callback accepted only a zero or explicit frame-time unit in the Atom
Sequence header. Carla 2.5.10's `lv2_atom_buffer_new/reset` initialises the sequence
size/type but leaves `Sequence_Body::unit` uninitialised. This caused valid MIDI
to be discarded and silence in actual playback, despite discovery and the synthetic
host tests passing. See the host's [atom buffer helper](https://github.com/falkTX/Carla/blob/v2.5.10/source/includes/lv2/atom-helpers.h).

The audio callback now accepts the usual integer frame timestamps even when that
header field contains otherwise unspecified data, while rejecting explicitly
beat-timed sequences and retaining all size, type and frame-range checks. A
regression poisons the unit field with `0xa5a5a5a5`: it fails against the old binary
and passes against the fixed binary, with sample-exact expected audio. The sound
engine and gain are unchanged.

The issue was also reproduced with the installed Carla engine: peak 0 before,
about 0.03 after the fix for MIDI C4 at velocity 90. The optional test below
creates its own diagnostic client and leaves the user's Carla session alone:

```bash
LD_LIBRARY_PATH=/usr/lib64/pipewire-0.3/jack \
  python3 mvp/piano-v4/carla_check.py out/piano-mvp-v4/piano-mvp-v4.lv2
```

This needs an existing JACK/PipeWire server and Carla's Python backend/library;
paths can be overridden with `--backend-dir` and `--library`. It is a local host
integration check, not part of unattended CTest. After installing the updated
bundle, reload the instrument or restart Carla to unload the old binary from memory.

### Velocity audit

`python3 mvp/piano-v4/velocity_check.py out/piano-mvp-v4` renders middle C at MIDI
velocities 32, 72 and 112. `velocity-soft-medium-hard.wav` plays those strengths
at 0, 6.5 and 13 seconds, using one common gain to preserve their level differences.
Raw renders and `velocity-metrics.json` are retained alongside it. The measured
soft/hard RMS difference is 14.41 dB; harder strikes also have more high-frequency
attack energy. The actual Carla test now checks all three velocities too.
This verifies the synthesis/host path, not the physical keyboard's transmitted
velocities. No timbre or installed instrument binary changed in this audit.
