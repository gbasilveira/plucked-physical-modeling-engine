# MVP 3: controlled soundboard experiment

The first experiment in [the iteration plan](../ITERATION-PLAN.md). String/hammer
parameters and the MIDI performance remain those of MVP 2. The playable default
replaces the raw-force/four-resonance/tanh output with a linear soundboard-only
force-to-pressure path. This is an analytical surrogate, **not a measured piano IR**.

## Start listening here

Outputs are committed in `out/piano-mvp-v3/` on branch `mvp/piano`:

- **`middle-c-ABC.wav`**: RMS-matched comparison of the same middle-C strike:
  original MVP 2 at **0–5 s**, linear baseline at **5.5–10.5 s**, soundboard-only
  at **11–16 s**. Half-second silent separators. Each segment includes key release.
- `middle-c-original-matched.wav`, `middle-c-linear-matched.wav`,
  `middle-c-soundboard-matched.wav`: individual matched comparisons.
- `middle-c-*-raw.wav`: raw fixed-gain versions, before offline level matching.
- `piano-v3-demo.wav`: the same melody/sustain chord as earlier versions, using
  the soundboard-only path and the plug-in's fixed gain.
- `piano-mvp-v3.clap`: Linux x86-64, **Piano MVP 3 — Soundboard**. Add the output
  directory to your CLAP host's search paths and rescan. Distinct ID from MVPs 1/2.
- `comparison-metrics.json`: matching gains, peaks and simple spectral measurements.

The default plug-in gain is deliberately lower than the earlier version to preserve
linear headroom for full-velocity chords. The single-note comparison raises the
soundboard clip's gain offline; it is **not** the plug-in's gain or an automatic
normalisation per played note. Match listening volume before judging tone.

## The three paths

1. **Original:** force + four resonances + tanh. Its rendered middle-C WAV is
   byte-identical to the MVP 2 baseline.
2. **Linear baseline:** the same force/four-resonance path, without tanh.
3. **Soundboard only:** forces excite a shared 512-mode orthotropic plate with
   homogenised periodic ribs. An approximate baffled Rayleigh radiation integral
   maps its modal accelerations and velocities to two microphone pressure signals.
   The output has a fixed linear gain and no direct string-force audio bypass.

All paths use one string per key, fixed bridge endpoints, the same hammer model,
16 active keys, velocity, sustain and the MVP's simple key-release damping.
No unison, sympathetic resonance or bridge-to-string feedback has been added yet.
The existing core audio-thread denormal guard prevents extreme tail slowdowns and
restores the host thread's floating-point control state at the end of processing.

## Model provenance and approximations

`soundboard-model.json` records every board assumption. Material values are from
[Chabassier & Duruflé RT-0425 v2, section 2.1](https://inria.hal.science/hal-00688679v2/document):
spruce density 390 kg/m³, Ex 11.6 GPa, Ey 0.9 GPa, Gxy 0.75 GPa, nu_xy 0.37.
The **1.4 x 1.6 m rectangle, 8 mm panel, 25 x 25 mm ribs at 140 mm pitch,
simply supported edges, loss factor 0.02, bridge and microphone positions are
nominal choices**, not a measured instrument. Ribs are homogenised into bending
stiffness and areal mass using a composite neutral axis. No crown, bridges as beams,
cut-off corners, rim dynamics or measured prestress are represented.

The first nominal mode is about **20.67 Hz**, and the 512 retained modes extend
to about **7.22 kHz**. These are properties of the chosen surrogate, not claims
about a real grand piano. Low-frequency response may be too compliant, and the
truncated/narrowband radiation approximation limits accuracy away from resonances.
Microphone positions are 1.5 m above the panel. The phase-integrated spatial
weights use a 96 x 112 midpoint grid and a common 4.37 ms propagation delay,
implemented with fractional interpolation. This is acoustic travel, not extra reverb.

The [AudioLabs Yamaha C3 page](https://audiolabs-erlangen.de/resources/MIR/2023-ISMIR-PianoSynth/)
documents measurements but provides no linked downloadable IR. Its audio examples
were not treated as responses or redistributed. A properly licensed, characterised
force-to-pressure measurement would be a stronger calibration target.

## Results so far

Tests pass for source parameters, all keys at 44.1/48/96 kHz, note/pedal behaviour,
full-velocity chords, finite output, reset and plug-in MIDI/sample formats.
Body-only superposition error is below 2e-15 relative in the impulse tests;
late impulse energy is about 1e-7 of the early window. This verifies the implemented
linear transfer, not the accuracy of the assumed physical board.

Middle-C matching uses joint stereo RMS from 0.05–3 s at -24 dBFS. No compression
or peak normalisation is applied; raw versions remain available. RMS matching is
only an approximation to equal perceived loudness.

- Removing tanh changes the matched waveform by **0.422% RMS** for this note.
  It is unlikely to explain most of the reported character here by itself.
- The body path changes the attack spectrum: energy above 2 kHz in the first
  100 ms drops from **1.91% to 1.65%**, with a changed stereo response.
- No claim of improved perceived realism is made before listening feedback.
  The next planned experiment is an energy-checked mobile bridge and its effect
  on partial decay, followed by coupled unison strings.

## Reproduce

From the worktree root (CMake 3.25+, C++20; NumPy for generating/analyzing only):

```bash
cmake -S mvp/piano-v3 -B out/piano-mvp-v3 -DCMAKE_BUILD_TYPE=Release
cmake --build out/piano-mvp-v3 -j2
ctest --test-dir out/piano-mvp-v3 --output-on-failure
python3 mvp/piano-v3/compare.py out/piano-mvp-v3
out/piano-mvp-v3/piano-render out/piano-mvp-v3/piano-v3-demo.wav
```

Optional renderer arguments: `[output.wav] [MIDI-key or -1 for demo] [original|linear|soundboard]`.
The renderer rejects over-full-scale output instead of silently clipping PCM.

To build the plug-in, configure with `-DCLAP_INCLUDE_DIR=/path/to/clap/include`
using the same CLAP 1.2.10 pin as MVP 2. To regenerate the analytical board:

```bash
python3 mvp/piano-v3/generate_soundboard.py
```

The source parameter implementation is shared with `mvp/piano-v2/preset.cpp`.
Build outputs contain Linux executables and audio; CMake caches/objects stay excluded.
