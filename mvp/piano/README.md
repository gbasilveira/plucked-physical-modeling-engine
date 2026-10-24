# Experimental piano MVP

A playable vertical slice built from the existing A1/A2/A3/A4/A5 oscillator,
stiff-string, SAV, bridge-force and felt-hammer components. The prototype lives
entirely in this directory; the production engine API still has its original stubs.

## Try the local build (Linux)

The initial build is in `out/piano-mvp/` at the worktree root:

- Listen to `out/piano-mvp/piano-demo.wav` (14 seconds: melody, then a sustained chord).
- Load `out/piano-mvp/piano-mvp.clap` in a CLAP-compatible host. Add its directory
  to the host's plug-in search paths and rescan. Select **Piano MVP**, route a MIDI
  keyboard to it, and enable monitoring. There is no custom window or parameter panel.
- Play MIDI keys 21–108 (A0–C8). Velocity controls the hammer strike;
  CC64 controls sustain. MIDI channels are merged (omni).
- CC120 clears all sound; CC123 releases keys while respecting sustain.

No plug-in is installed into your system automatically. Binary and WAV outputs
are local build artifacts, excluded from Git. Build them with the instructions below
on another machine.

## Build

Requires CMake 3.25+ and a C++20 compiler. The renderer and behaviour test need
no downloaded dependencies:

```bash
cmake -S mvp/piano -B out/piano-mvp -DCMAKE_BUILD_TYPE=Release
cmake --build out/piano-mvp -j2
ctest --test-dir out/piano-mvp --output-on-failure
out/piano-mvp/piano-render out/piano-mvp/piano-demo.wav
```

For the CLAP plug-in, supply the SDK headers at the same pinned revision as the
main project (CLAP 1.2.10):

```bash
git clone https://github.com/free-audio/clap.git out/clap-sdk
git -C out/clap-sdk checkout --detach 195b42a004144fab0b3cf95e9c067187d15365b7
cmake -S mvp/piano -B out/piano-mvp -DCMAKE_BUILD_TYPE=Release \
  -DCLAP_INCLUDE_DIR="$PWD/out/clap-sdk/include"
cmake --build out/piano-mvp -j2
ctest --test-dir out/piano-mvp --output-on-failure
```

Render one key instead of the demo with, for example:

```bash
out/piano-mvp/piano-render out/piano-mvp/middle-c.wav 60
```

## Scope and limits

- All 88 keys, maximum 16 sounding keys, one string per key. Repeating a key
  re-strikes its existing string. When full, the oldest key is stolen immediately;
  this can click and needs refinement.
- Fixed synthetic piano dimensions, wound bass strings and physical hammer strikes.
  This preset is not fitted to a measured concert grand.
- The strings have fixed bridge endpoints. Their calculated bridge forces feed a
  simple four-resonance body filter in one direction. There is no body-to-string
  feedback, shared soundboard model, unison beating or sympathetic resonance yet.
- Key release uses a simple energy-removing diagonal damping approximation.
  Sustain holds released strings. No half-pedal, sostenuto or una corda.
- Stereo uses keyboard-position panning. A fixed soft saturation bounds the output.
  Loud chords may sound compressed; gain and tone still need listening feedback.
- No GUI, editable presets, pitch bend, MPE, native CLAP note dialect, VST3/AU,
  standalone application or session state. The instrument has one fixed preset.
- Sample rates 22.05–192 kHz are accepted; 44.1, 48 and 96 kHz are covered by tests.
  The initial plug-in artifact and dynamic host check were built on Linux only.
- Allocations occur at preparation, not by design in the sample loop. Real-time
  allocation instrumentation and actual DAW dropout/compatibility testing remain
  to be done. This prototype does not claim completion of production phase gates.

## Verification

`piano-check` checks audio on every key at three sample rates, velocity response,
sustain, released-note retirement, the voice cap and reset silence.
When CLAP headers are provided on Linux, `piano-host-check` dynamically loads the
actual plug-in and checks ports, MIDI sample offsets, float/double stereo audio,
16-note playback and reset. Its timing is offline throughput, not a real-time
deadline guarantee. A DAW and subjective listening review are the next tests.
