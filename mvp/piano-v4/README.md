# MVP 4: bridge feedback

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
