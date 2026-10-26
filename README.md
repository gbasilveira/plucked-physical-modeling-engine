# Plucked Physical Modelling Engine

An open-source, real-time physical-modelling engine for the piano and plucked strings — concert grand and upright piano first, then harpsichord, guitars, tanpura, sitar, kantele, shamisen and tanbur — delivered as CLAP, VST3, AUv2 and LV2 plug-ins and a standalone app for Windows, macOS and Linux.

Every sound comes from simulated physics: modal stiff strings with two polarisations and longitudinal waves, a nonlinear felt hammer with una corda, dampers and pedals, a soundboard computed from its wood, thickness and ribs, and explicit energy-stable contact for frets, jawari and sawari bridges. Materials and dimensions are user-editable parameters.

## Status

Planning is complete. The production engine has not started: phase P0.1 is ready. An experimental piano MVP lives under `mvp/` on branch `mvp/piano` (see [Experimental piano MVP](#experimental-piano-mvp)). Work on the production engine proceeds phase by phase under an AI orchestrator: planning documentation is written by Opus agents and code by Sonnet agents.

| Start here | What it is |
| --- | --- |
| [CLAUDE.md](CLAUDE.md) | Orchestrator instructions and model policy |
| [docs/plan/phasing-plan.md](docs/plan/phasing-plan.md) | Phases, gates, guidelines, decisions |
| [docs/plan/engine-specification.md](docs/plan/engine-specification.md) | Normative engine specification |
| [docs/plan/implementation-plan.md](docs/plan/implementation-plan.md) | Architecture and technology background |
| [docs/plan/literature-survey.md](docs/plan/literature-survey.md) | Research survey |
| [docs/status/phase-status.md](docs/status/phase-status.md) | Current progress |

## Experimental piano MVP

A modal physical piano, built to test the modelling approach before the production engine. It is not a phasing-plan phase and shares no code with the planned engine. Stiff strings with one to three unisons, a felt hammer with tension modulation, sympathetic strings and leaky dampers, one shared soundboard, and per-key imperfections. Each version was checked by ear and against Salamander (Yamaha C5, CC BY 3.0) recordings. Full write-ups: [mvp/piano-v4/README.md](mvp/piano-v4/README.md), [mvp/piano-grand/README.md](mvp/piano-grand/README.md), [mvp/ITERATION-PLAN.md](mvp/ITERATION-PLAN.md).

| Version | What changed |
| --- | --- |
| 0.4.x | Bridge feedback and coupling, voicing against Salamander (softer felt, less energy above 3 kHz) |
| 0.5.0 to 0.5.3 | Una corda and undamped top keys; one shared soundboard; tension modulation; finite damper loss |
| 0.5.4 | Real time: faster kernels and a radiation helper thread, Live/Balanced/Full qualities |
| 0.5.4.1 | Hotfix: 64-frame blocks and adaptive load shedding |
| 0.5.5 | Per-key imperfections (detuned unisons, uneven hammer) and a Voices setting |
| 0.6 | Baked soundboard radiation (Radiation setting); string-fit experiment, not in the plug-in |
| 0.7a | Offline grand: FEM soundboard plus 3-D air near field and microphones; not in the plug-ins. High frequencies are still far too low |
| 0.7b | In progress: statistical high-frequency board, string radiation, diffraction, admittance |

### Install and play (Linux)

The LV2 and CLAP builds are in `out/piano-mvp-v4/`: `piano-mvp-v4-lv2-linux-x86_64.zip`, the unpacked `piano-mvp-v4.lv2/` folder and `piano-mvp-v4.clap`.

1. Extract the whole `piano-mvp-v4.lv2` folder into `~/.lv2/`. Keep `manifest.ttl`, `piano.ttl` and `piano-mvp-v4.so` together.
2. Rescan plug-ins in your host (Carla, Ardour) and look for "Piano MVP 4 - Baked radiation" under instruments. Connect MIDI in and the Left/Right outputs.
3. MIDI notes 21-108, velocity, CC64 sustain, CC67 una corda, CC120 panic, CC123 note release.

LV2 control ports (the CLAP version has the same parameters):

| Port | Name | Values (default) |
| --- | --- | --- |
| 3 | Quality | Live (default), Balanced, Full |
| 4 | Worker threads | 0-3 (1); the output is identical for any value |
| 5 | Free-wheeling | 0/1; a host that bounces offline raises it and the plug-in uses Full |
| 6 | Adaptive load shedding | 0/1 (on); Live and Balanced only; switch off for exact, repeatable output |
| 7 | Imperfection | 0-2 (1); 0 is the perfectly even 0.5.4 strings |
| 8 | Voices | Auto, 16, 32, 48, 64 (Auto) |
| 9 | Radiation | Auto, Modal, Baked (Auto) |

Buffer size: set the host to 128 or 256 samples (2.7 or 5.3 ms at 48 kHz) on a modest CPU; 64 works from about E3 upward. With PipeWire, force the quantum and raise the maximum too:

```bash
pw-metadata -n settings 0 clock.force-quantum 256
pw-metadata -n settings 0 clock.max-quantum 256   # or higher
```

**Full is offline only** (88 voices, nothing capped); use it for bouncing. Live is for playing and is limited to 16 struck keys.

### Offline rendering and tests

```bash
cmake -S mvp/piano-v4 -B out/piano-mvp-v4 -DCMAKE_BUILD_TYPE=Release
cmake --build out/piano-mvp-v4 -j2
ctest --test-dir out/piano-mvp-v4 --output-on-failure
out/piano-mvp-v4/piano-midi-render EVENTS OUT.wav [--threads N] [--quality live|balanced|full] [--imperfection X] [--previous]
```

`EVENTS` is the event file made from a MIDI file by the scripts in `mvp/piano-v4/` (for example `prepare_etude.py`; see `mvp/piano-v4/listening/`). `--previous` renders 0.5.3 bit for bit. The 0.7a grand renderer is `piano-grand-render`; see `mvp/piano-grand/README.md`.

### Listening

Renders committed to git (24-bit WAV; open the link or download):

- Chopin Prelude Op. 28 No. 4, 0.6: [full](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/chopin-prelude-0.6-full.wav), [live](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/chopin-prelude-0.6-live.wav)
- Chopin Etude Op. 10 No. 12 "Revolutionary", 0.6: [full](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/chopin-etude-op10-12-0.6-full.wav), [live](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/chopin-etude-op10-12-0.6-live.wav)
- 0.5.5 imperfections: [imperfection-demo-0.5.5.wav](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/imperfection-demo-0.5.5.wav), [big-chords-0.5.5.wav](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/big-chords-0.5.5.wav)
- 0.6 baked against modal radiation: [baked-ab-0.6.wav](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/baked-ab-0.6.wav)
- 0.7a grand: [grand-ab-0.7a.wav](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/grand/grand-ab-0.7a.wav), [grand-mics-0.7a.wav](https://raw.githubusercontent.com/gbasilveira/plucked-physical-modeling-engine/mvp/piano/out/piano-mvp-v4/grand/grand-mics-0.7a.wav)

The Prelude and Etude renders are synthesised from MIDI by Bernd Krueger (piano-midi.de, via Wikimedia Commons), licensed CC BY-SA 3.0 Germany. Sources, hashes and changes: `mvp/piano-v4/listening/` and `out/piano-mvp-v4/CHOPIN-ATTRIBUTION.md`.

### Performance (Intel i3-8100, 4 cores, 48 kHz)

From the 0.6 whole-piece report in `mvp/piano-v4/README.md`: the Prelude is comfortable in Live (mean 0.20 of the block deadline, 5x real time). The Etude is not real time in Live without shedding (mean 0.44, 2.1 % late blocks at 64 frames); with adaptive shedding on it holds, but sits at the highest shed level most of the piece and steals many notes. Full runs at about 0.75x real time on the Prelude and 0.5x on the Etude.

### Limits

Experimental. Linux LV2 and CLAP only. One fixed preset, no GUI. Nominal geometry and assumed parameters, not measured from a real instrument. 0.6 Full still sounds like an upright; the grand work (0.7) is not in the plug-ins.

### Licence note

The code is MIT. The Salamander reference recordings (CC BY 3.0) and the renders derived from the Krueger MIDI files (CC BY-SA 3.0 DE) keep their own licences.

## Building

CMake presets and the Python toolchain are described in [docs/dev/build.md](docs/dev/build.md); contribution rules are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

MIT — see [LICENSE](LICENSE).
