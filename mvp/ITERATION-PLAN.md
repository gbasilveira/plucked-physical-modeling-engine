# Piano listening iterations

Goal: improve the acoustic-piano character, with one change at a time and audible,
reproducible comparisons. A passing numerical test is not a listening endorsement.
Keep MVP 1 and MVP 2 and their artifacts as baselines. Work on branch `mvp/piano`.

## Current listening diagnosis (2026-10-25, 0.5.3)

Listener on 0.5.2: almost perfect; unpedalled notes muffled, box feels small; pedalled almost
perfect. 0.5.3 adds **leaky dampers**: damped, unstruck strings are simulated like sympathetic
strings with an added modal damper loss (`setLeakyDampers`, default on; flat law, fitted
2.0 /s to Salamander late 62-500 Hz shares: rms 5.9 dB over nine notes vs 29 dB for 0.5.2; the
value is an effective fit, far weaker than a physical felt damper, and the response has a cliff
between 2 and 10 /s). `--previous` is 0.5.2 bit-for-bit, old `--previous` is `--v051`. Released-note
law is a separate switch (`setLeakyRelease`, default off). Side effects to judge by ear: released
bass/middle notes stop 14-20 dB less abruptly in the first 0.25 s; the attack high-band error rose
from 3.83 to 4.94 dB. Plug-ins cap damped strings at 4 (unpedalled cluster 0.86x, Chopin 1.58x
real time). `unpedalled-demo-0.5.3.wav` plays 0.5.2, 0.5.3 and 0.5.3 without the bridge-top
mass (diagnostic; the share metric says the mass is not over-darkening). "Box feels small":
L/R coherence of the model is 0.85-0.9 vs 0.4-0.6 for the reference, so stereo width is the next
target (`piano-v4/README.md`, "Update 0.5.3").

## Previous diagnosis (2026-10-25, 0.5.2)

0.5.2 adds Kirchhoff-Carrier tension modulation to the **struck** strings (`TensionStruckNote`,
an MVP-local adaptation of the engine's `StruckNote`: the SAV segment carries the tension
potential next to the hammer potential, one shared psi; vertical polarisation; setter
`setTensionModulation`, default on; `--previous` is 0.5.1 bit-for-bit, old `--previous` is now
`--v050`). Modified energy: lossless 1e-12 relative drift, lossy never rises. The effect is
physical and small at the model's hammer amplitudes: +0.6 / +1.5 / +0.9 cents at 30-60 ms for
A1 / A2 / C4 at velocity 92 (0.07 / 0.16 / 0.08 at velocity 40), inharmonic sideband power +0.05
to +7 dB (A2 at velocity 127), band shares move <= 1 dB (C6 ff late up to 3 dB). Offline cost
+24% on the Chopin excerpt; plug-ins use tension for keys <= 47 only (pedalled 12-key cluster 0.97x
real time vs 0.50x with all keys). Longitudinal modes are not included. Listener confirmation
pending; the 62-500 Hz deficit of C4/C6 remains and the likeliest cause is damper leakage (see
`piano-v4/README.md`, "Update 0.5.2"). Next: a leaky-damper experiment, then the ribbed
grand-outline board.

## Previous diagnosis (2026-10-25, 0.5.1)

0.5.1 replaces the 88 private bridges by one shared, bidirectionally coupled soundboard
(the lowest 128 modes are mechanically coupled to every simulated string through the same
passive scheme as `BridgePoint`; the 512 modes still radiate), with key-dependent bridge points
on an assumed bass + treble bridge curve of the nominal plate, and simulates unstruck strings
whose dampers are lifted (pedal, held key, treble above F#6) so they ring sympathetically
(setters `setSharedBoard`, `setGrandBridgeGeometry`, `setSympatheticStrings/Limit`;
`--previous` is 0.5.0 bit-for-bit). Many-string energy tests pass (lossless drift 2e-14).
Offline cost is 2.8x higher on the pedal-heavy Chopin excerpt (93 s vs 33 s); plug-ins cap
sympathetic strings at 8. The reference low-band deficit (C4/C6, 125-500 Hz) is **not** fixed:
isolated Salamander notes have dampers down on all other keys, so only the undamped treble can
ring, and what supplies that low-frequency energy in the recordings is unidentified. Listener
confirmation pending. The board is still a rectangle, bridge curve and wake threshold are
assumptions. Next (0.5.2): string tension modulation (A7 code), then a ribbed grand-outline
board from the B2 FEM compiler and its radiation table. Details in `piano-v4/README.md`.

## Previous diagnosis (2026-10-25, 0.5.0)

0.5.0 (listener: 0.4.9 sounds "small") makes three no-new-physics changes: runtime voice
limit (offline 88, plug-ins 12; Chopin steals 13 -> 0), una corda on CC67 through the
engine's grand soft-pedal map (engine-default parameters assumed; the Chopin MIDI has no
CC67), and undamped keys from F#6 (assumed boundary). `--previous` reproduces 0.4.9
bit-for-bit. Next (0.5.1): one shared soundboard coupling all strings for sympathetic
resonance, with key-dependent bridge positions on a grand-shaped board.

0.4.9 targets the listener's "too bright, small, lacks wood" report (0.4.8) with a
measured octave-band comparison to the Salamander recordings. The >4 kHz attack
excess is not a board-damping problem (damping is irrelevant once board modes
overlap) and not only the hammer: bridge force plus a +6 dB/octave
acceleration-weighted, unloaded board raised 4-8 kHz by about 12 dB, while the report's
k^2 string viscosity made 4-8 kHz partials decay 11-13 dB/s faster than the reference.
Two linked changes (bridge-top mass transmission, saturating string viscosity; both
assumptions with cited form) cut the mean >2 kHz attack share error from 9.4 to 5.7 dB;
4 kHz attack is still +10 to +20 dB above the reference. See `piano-v4/README.md`.
Next (0.5.0): low-frequency body deficit (knock, low modes, sympathetic ringing), then
treble (C6 2-4 kHz) and voice stealing.

0.4.8 tests reference-guided softer felt: the listener finds 0.4.6 closer but too
metallic and lacking wood. Nine pinned lossless Salamander Grand Piano recordings
(Yamaha C5, Alexander Holm) now provide onset, spectrum and decay comparisons.
Uniform softening improved bass/mid attack high-band balance but worsened treble,
so K is scaled by 0.35 through C5 and smoothly restored by C6. This is a measured
comparison supporting a voicing experiment, not a fully calibrated physical model.
See `piano-v4/reference/README.md`. Next, evaluate the unchanged decay and body
response mismatch; do not equate darker output with authentic wood or fit a board
impulse response directly from a note recording.

0.4.6 now tests an identified mechanical/radiation mismatch: radiation used 512
board modes but only 128 participated in mechanical energy transfer. The omitted
modes' real admittance is projected into passive diagonal string loss; this leaves
the low-mode shared bridge in place. Middle-C late high-partial energy is reduced,
while initial RMS is almost unchanged. Listener confirmation is pending. No claim
is made that this is the particular resonance heard by the listener.

All following iterations should render the pinned expressive Chopin Op.28 No.4
fixture in `piano-v4/listening/`, source 40–64 s plus 2 s release. Validate source
and event-schedule SHA-256 with `prepare_classical.py --check` before rendering;
use `render_classical.py`, preserve the fixed common gain and versioned WAV/audit,
and publish the listening WAVs alongside changes. Next priorities are the listener's
assessment of ringing, measured body/attack calibration, and reducing voice stealing
without exhausting the real-time CPU budget.

The listener reported 0.4.4 was closer, with an electronic-sounding resonance.
0.4.4 implemented two-/three-string groups with a shared full-mass hammer and
bridge, distributing the total assumed felt stiffness over the contacts. Bank's
*Physics-Based Sound Synthesis of the Piano*, section 6.1.1, discusses the importance
of hammer/string mass balance for a single-string surrogate:
https://home.mit.bme.hu/~bank/thesis/pianomod.pdf .
Velocity is verified through the core and actual Carla/LV2; the 0.4.4 common-gain
soft/medium/hard clip has a 14.28 dB soft/hard RMS difference. Physical-controller
velocity remains unverified because no live keyboard notes were captured.

Then calibrate attack/contact and partial decay against a recorded acoustic-piano
reference, and calibrate bridge mobility/radiation. Existing string lengths are
published inputs; the board geometry remains nominal and unvalidated. Add cross-key
sympathetic coupling after the isolated note is convincing; case/lid/room are later
experiments. These are hypotheses, not a claim that one change guarantees realism.

| Step | Experiment | Evidence before proceeding | State |
| --- | --- | --- | --- |
| 1 | Published string/hammer inputs | Source provenance, compiled tensions and loss projection, 88-key tests | Implemented in MVP 2; listener still reports electric-guitar character |
| 2 | Remove output saturation and direct force mix; add explicit force-to-pressure soundboard path | Same middle-C excitation through old, linear-baseline and body-only paths; matched-level WAVs and metrics; linearity/impulse/stability tests | Implemented in MVP 3; listening feedback pending |
| 3 | Bridge mobility and feedback | Compare fixed-end/body-filter experiment with an energy-checked coupled bridge; measure partial decay changes | Implemented in MVP 4; listener reports more piano-like but still a weak synthetic harpsichord |
| 4 | Appropriate unison string groups | Coupled two-/three-string notes versus single string; beat frequencies and decay envelopes; coherent hammer/string mass treatment | Prototype implemented in 0.4.4; shared hammer/bridge, split total felt stiffness, declared tuning spread; listening and reference calibration pending |
| 5 | Hammer/attack calibration | Soft/medium/hard strikes; contact duration, force peak and initial partial distribution versus a cited reference | Planned |
| 6 | Sympathetic strings and pedal behaviour | Pedal up/down, released keys, repeated notes, chords; CPU/voice accounting | Implemented in 0.5.1 (shared board, unstruck strings with lifted dampers, key-dependent bridge points); listening pending. 0.5.2 adds tension modulation of struck strings (step 7 below); 0.5.3 adds leaky dampers for unpedalled notes |
| 7 | String tension modulation (pitch glide, phantom partials) | Energy test with hammer and tension; glide and sideband measurements A/B; plug-in CPU | Implemented in 0.5.2 (struck strings, vertical polarisation); effect small; listening pending |

For step 2, the AudioLabs Yamaha C3 measurement page was inspected. It documents
the procedure and offers audio demonstrations but no linked downloadable soundboard
IR. Do not relabel example tones as impulse responses or infer a calibrated
force-to-pressure transfer from them. The first available substitute is an explicitly
nominal analytical orthotropic plate using published spruce material constants;
this is not a measured Yamaha response or a finished grand-piano soundboard.

Step 2 separates three hypotheses:

1. **Original**: MVP 2 force + four resonances + tanh output.
2. **Linear baseline**: same force + four resonances, without tanh.
3. **Soundboard only**: mass-normalised plate modes, force excitation, approximate
   Rayleigh radiation to two listening points. No direct force bypass, no tanh.

Preserve raw audio and fixed gains. Also export copies matched by RMS over the
same active middle-C window, with the gain and peak recorded. RMS matching is an
approximation to equal perceived loudness, not a claim of perceptual equivalence.
Do not normalise every note separately in the playable instrument.

Automated acceptance: no hidden numerical resets; finite outputs and headroom;
source-value tests retained; modal frequencies match an independent plate formula;
body transfer obeys superposition and decays after an impulse; sample timing and
reset are correct. Compare attack (0–100 ms), early spectrum and 0.1–3 s decay.
Record the change without declaring it more realistic until someone listens.

First measured result: for middle C at velocity 0.75, removing saturation changes
the RMS-matched waveform by 0.422% RMS over 0.05–3 s. Saturation is therefore a
weak candidate for the main tonal problem in this particular test. The body-only
path reduces the first-100-ms energy share above 2 kHz from 1.91% to 1.65% and
changes the stereo response. These describe the signal, not perceived realism.
The reproduced original WAV is byte-identical to the published MVP 2 middle-C WAV.
At that stage the nominal board response and absent bridge feedback remained major
issues; a full piano identity could not be inferred from that first experiment.

MVP 4 now uses the core A4/A5 moving-support coupling. For performance, the local
mechanical model retains 128 board modes (up to 1.87 kHz); radiation still uses all
512. Private mechanical models do not yet send other notes' motion back into each
string. Tests of the underlying linear coupling conserve undamped energy to about
1.2e-14 relative in the tested bass/middle/treble/sample-rate configurations; damped
cases lose energy. This does not validate the full hammer-contact approximation.

The next A/B uses the same note in the fixed versus moving-bridge configurations,
matching only the initial 0.05–0.25 s so that the later decay difference remains.
Middle-C late/early RMS changes from -3.08 dB to -9.92 dB, and C6 from -9.28 dB
to -26.04 dB (0.25–0.75 s versus 2–2.5 s). Ask whether the quicker decay feels
natural or too dead, which register improves, and whether the attack is still
plucked/electric. If decay is wrong, calibrate the bridge/board before stacking
unison effects on top. If decay improves but the identity remains thin, step 4 is
the next controlled comparison.

New listener feedback: the sound is closer, but now resembles a synthesised
harpsichord with a weak/thin character. Treat this as a hypothesis about attack,
decay and fullness, not proof of one faulty parameter. Before adding unison,
prioritise a controlled attack/decay calibration:

1. Render the same middle note at soft/medium/strong velocities, keeping a fixed
   playback gain for dynamics comparisons. Check hammer contact duration, peak
   force and early partial distribution; felt dissipation is still an MVP assumption.
2. Compare fixed bridge and current feedback, plus an explicitly characterised
   alternative bridge mobility. Check whether energy is leaving the string too
   quickly, especially in the treble. Do not hide this with gain normalisation or
   sustain/reverb effects.
3. Use an appropriately licensed real-piano reference at the same pitch/dynamic
   when available. Separate numerical correctness from a perceptual match.
4. Once attack and decay are acceptable, compare physical two-/three-string unison
   groups sharing a hammer, accounting for the string/hammer mass ratio, to address
   fullness and aftersound. Avoid arbitrary chorus as a substitute for coupling.

An LV2 wrapper of MVP 4 is supplied for testing in the listener's installed host;
its MIDI/audio equivalence is checked independently. This is a format change and
does not count as a new timbre iteration.

Publish each completed experiment with a short change note, a single-note A/B,
the same melody/chord demo, reproducible commands and explicit approximations.
The listener's feedback selects the next refinement; independent engineering work
can continue, but do not stack unvalidated timbre changes into an opaque preset.

Sources:

- Chabassier & Duruflé, RT-0425 revised: https://inria.hal.science/hal-00688679v2/document
- AudioLabs measurement description: https://audiolabs-erlangen.de/resources/MIR/2023-ISMIR-PianoSynth/
- Bank et al., radiation versus mechanical response: https://home.mit.bme.hu/~bank/publist/jasp03.pdf
