# Piano MVP 2: published parameters

A second instrument alongside `mvp/piano`, using Chabassier & Duruflé's revised
2012 **Physical parameters for piano modeling**, HAL **hal-00688679v2**, Appendix A
(wrapped strings). The original MVP and its audio stay available for comparison.
This is a parameter experiment, not a replication of the paper's complete piano.

## Listen and play

Committed Linux x86-64 outputs are in `out/piano-mvp-v2/`:

- `piano-v2-demo.wav`: the same 14-second melody and sustain-pedal chord as MVP 1.
- `piano-v2-middle-c.wav`: one middle-C strike, velocity 0.75, released at 4 seconds.
- `piano-mvp-v2.clap`: load **Piano MVP 2 — Published parameters** in a CLAP host.
  Its distinct plug-in ID allows both MVP versions to coexist.
- `piano-render`, `piano-check`, `piano-host-check`: renderer and local test executables.

MIDI keys 21–108, 16 sounding keys, velocity, CC64 sustain, CC120 panic and CC123
key release. MIDI channels are merged. No custom GUI or editable preset.

## Sources and data provenance

- [Parameter report, revised version](https://inria.hal.science/hal-00688679v2/document)
  (Appendix A, printed pages 11–15; material modulus and loss laws in section 3.2).
- [Model and damping equations, JASA 2013](https://perso.ensta.fr/~touze/PDF/Batwoman/chabassier-jasa.pdf),
  **Modeling and simulation of a grand piano**, equation (8).

`report-appendix-a.csv` contains the 84 source rows C1–B7 (MIDI 24–107).
`report_parameters.hpp` embeds the same numbers, so playback requires no file I/O.
`extract_parameters.py` regenerates both from the original PDF using Poppler's
`pdftotext`. The source PDF's SHA-256 is recorded in the generated header.
The source paper itself is not redistributed. Earlier report versions have different
string data: use **v2**, not a mix of versions.

For middle C, the inputs are length **0.657 m**, diameter **1.006 mm**, effective
density **7850 kg/m³**, tension **741 N**, hammer mass **8.71 g**, stiffness
**5.840e9 N/m^p**, exponent **2.418**, strike position **0.079 m**.
These are published model inputs; the report includes empirical design and damping
choices and does not claim that every number was directly measured.

## Mapping to this engine

- Use the report's effective homogeneous bass strings: `dCore == dOuter` and the
  table's effective density. Values above steel density represent winding mass;
  they do not describe a real material. Do not add a second winding on top.
- Set Young's modulus to **2.02e11 Pa** (report section 3.2).
- Preserve each published tension exactly by inverting the engine's
  tension-for-pitch equation. The engine uses Euler–Bernoulli modes, while the
  report's complete model uses Timoshenko strings. The resulting pitch can differ
  from the rounded frequency column and standard A440 tuning. Middle C is about
  **262.3035 Hz**, versus **262.22 Hz** in the table.
- Project the paper's transverse damping term (JASA eq. 8) onto pinned modes:
  `sigma[n] = Ru + eta_u * T / (rho*A) * k[n]^2`. Populate `sigmaOverride` with
  these values. Shear and longitudinal damping are not part of this MVP.
- Convert hammer mass from grams to kg and strike position to a length ratio;
  use the table's coupled stiffness and exponent values together.
- **A0, A#0, B0 and C8 are extrapolated**, not tabulated measurements. Extend
  endpoint length, diameter, effective density and tension by their log slopes.
  For these four notes use report equations (2)–(4) for hammer mass, exponent and
  stiffness, equation (6) for losses, and a strike ratio of 0.12. The preset API
  exposes an `extrapolated` flag.

## Deliberate limits

The existing single-string-per-key arrangement, fixed bridge endpoints, four-mode
one-way body filter, diagonal key-release damper, stereo panning and fixed output
soft saturation are retained to make the parameter comparison useful. This version
does **not** incorporate a measured Yamaha soundboard impulse response or the
report's ribbed FEM soundboard, multi-string coupling or sympathetic resonance.

The report's felt dissipation coefficient `RH` multiplies `d(delta^p)/dt`.
The current engine's Hunt–Crossley dissipator uses `K*mu*delta^p*delta_dot`;
there is no constant conversion between these laws. `RH` is recorded for provenance
but **not applied**. `muHC = 0.5`, the contact window, hammer velocity curve and
release distance remain explicit MVP assumptions. A complete implementation of
the report's felt law is separate work. A more realistic timbre is not guaranteed
by replacing these parameters alone.

## Build and reproduce

From the worktree root, with CMake 3.25+ and a C++20 compiler:

```bash
cmake -S mvp/piano-v2 -B out/piano-mvp-v2 -DCMAKE_BUILD_TYPE=Release
cmake --build out/piano-mvp-v2 -j2
ctest --test-dir out/piano-mvp-v2 --output-on-failure
out/piano-mvp-v2/piano-render out/piano-mvp-v2/piano-v2-demo.wav
out/piano-mvp-v2/piano-render out/piano-mvp-v2/piano-v2-middle-c.wav 60
```

For the plug-in, supply CLAP 1.2.10 headers, as with MVP 1:

```bash
git clone https://github.com/free-audio/clap.git out/clap-sdk
git -C out/clap-sdk checkout --detach 195b42a004144fab0b3cf95e9c067187d15365b7
cmake -S mvp/piano-v2 -B out/piano-mvp-v2 -DCMAKE_BUILD_TYPE=Release \
  -DCLAP_INCLUDE_DIR="$PWD/out/clap-sdk/include"
cmake --build out/piano-mvp-v2 -j2
ctest --test-dir out/piano-mvp-v2 --output-on-failure
```

To regenerate source data (not needed for building):

```bash
python3 mvp/piano-v2/extract_parameters.py /path/to/hal-00688679v2.pdf
```

The checks cover source anchors, exact tension preservation for all 88 keys,
loss projection, extrapolation flags, audio on all keys at 44.1/48/96 kHz,
numerical fault counters, velocity, sustain/release, polyphony and reset.
The Linux host check loads the actual CLAP binary and tests MIDI offsets,
float/double stereo output and sustained 16-note playback. Actual DAW/dropout
testing and subjective listening evaluation remain to be done.
