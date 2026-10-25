# MVP 4: bridge feedback

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
