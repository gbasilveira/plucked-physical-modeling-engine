# Piano listening iterations

Goal: improve the acoustic-piano character, with one change at a time and audible,
reproducible comparisons. A passing numerical test is not a listening endorsement.
Keep MVP 1 and MVP 2 and their artifacts as baselines. Work on branch `mvp/piano`.

## 0.6 plan: bake what is static, compute live only what interacts (2026-10-26)

**Why.** Full is the sound reference and cannot run live: all 88 keys pedalled cost 8.3x real time on average and 10.8x at the attack on the i3-8100
(4 cores, 3.6 GHz, AVX2; `piano-block-bench --keys 21..108 --pedal --quality full`). Live at 64-256 frame blocks sheds to 8 voices with no tension on dense
pedalled bass, and the listener hears the thinning. The idea, from the listener: compute the instrument in advance and build a function that
represents it, like baked shadow masks in games, finding the best approximation. **Full stays the reference model** and every baked part is judged
against it.

**Goals.** (1) Reproduce Full to a stated error with a fraction of the live work. (2) Keep the interaction that makes a piano (pedal, dampers,
sympathetic strings, voice stealing) live. (3) Every step is measured against Full, with the error and the cost reported, before it enters the plug-in;
the listener's ear decides the steps that change the sound.

| part | behaviour | treatment | step |
| --- | --- | --- | --- |
| board radiation to the mics (512 modes) | linear, fixed | baked: measure impulse responses (partitioned FFT convolution) against a fitted IIR (reduced modal bank); choose | 1 (done) |
| board feedback on the strings (128 coupled modes) | linear, interactive | model-order reduction to the modes that matter (balanced truncation or energy-based selection), exact small filter, measure the error | 4 |
| string partials, decay, two-stage decay, beating | nearly linear | cheap per-key string fitted to Full renders by optimisation (reduced modal set; waveguide with dispersion and loss filters as an alternative) | 2 (experiment), 3 (integration) |
| hammer felt | nonlinear, mostly key x velocity | tabulated force pulse per key x velocity layer; re-strike on a vibrating string approximate | 3 |
| tension glide | nonlinear, amplitude dependent | per-key pitch-glide curve against amplitude | 3 |
| pedal, dampers, sympathetic strings, voice interaction | performance dependent | stay live; cheap once the strings are cheap | live |

**Steps and acceptance criteria.**

1. *Baked radiation* (this iteration). Acceptance: rms error against Modal at most -80 dB re signal on the Chopin excerpt in Full and on single
   notes; IR length stated from the -100 dB point; cost against the helper-thread radiation at 64/128/256 frames; a setting `Radiation: Modal | Baked`
   (CLAP parameter, LV2 port after port 8, nothing renumbered), Baked the default for Live and Balanced, Full bit-exact with Modal; determinism
   (`sample()` = blocks = any thread count), no allocation in `process`, state round trip with old states loading the default.
2. *String-fit experiment* for C2 (36), C4 (60), C6 (84) at three velocity layers, no pedal, 8 s. Acceptance: a baker tool (numpy only); fit error per
   key (partial frequency in cents, decay in dB/s, octave-band envelope in dB over 0.1-8 s); cost per voice in cycles per sample against the current Live
   voice; an A/B file (Full, then the fitted string through the baked radiation, 5 s each, then a three-note chord both ways). Not integrated: step 3 is
   gated on the listener.
3. *Integrate the fitted strings* (gated on listening to the A/B): a bank of fits per key and velocity layer with interpolation, the force table of the
   hammer pulse, the glide curve, the fitted force driving the coupled board so that sympathetic and leaky strings stay live, re-strike and damping.
4. *Reduce the board feedback* (balanced truncation of the 128 coupled modes), if the strings are cheap enough for it to matter.

**What stays live:** the sustain and una corda pedals, the dampers (including the leaky ones), the sympathetic and leaky strings and their waking, the voice
limit and stealing, the imperfection plan, the adaptive load shedding.

**Mics, lid and floor (0.5.6).** The measurement says an impulse response per port per mic is the expensive way (convolution cost 5-17x the modal
bank, hundreds of MB per mic). The cheap way is the reduced modal bank: a mic position or a lid state is another set of per-mode output residues
(`reL, imL, reR, imR` per mode, a few KB), so an extra mic costs 2 multiply-adds per kept mode per sample (about 0.1 us), not a convolution per port, and the
drive of the modes is shared. Baking per mic is therefore a table of residues generated offline (`generate_soundboard.py` with the mic position or lid
state), not a set of IRs. Not implemented now.

### 0.6 results (2026-10-26)

**Step 1, radiation.** Impulse response length at -100 dB: 8.9 s (425,000 taps; the slowest radiating mode is 20.7 Hz with sigma 1.3 /s). An IR per port
(104+ IRs for struck and unstruck drives, two channels) is about 600 MB. FFT convolution with IRs truncated to 0.25 / 0.5 / 1 s on the dense Chopin drive
(85 ports per sample): error -24 / -31 / -35 dB, cost 37 / 69 / 120 us per sample against 7.0-8.9 us for the modal bank: **rejected**. The chosen
bake is a reduced modal bank: the 246 modes whose radiation residues are numerically zero are removed (exact), the 266 others all carry more than
1e-6 of the energy (energy-based selection removes nothing useful), the drive is accumulated in float with a chunk-major table and the mode update stays
in double. Error against Modal on the Chopin excerpt (Full): **-133.9 dB** rms (octave bands -108 ... -144 dB); single notes C2/C4/C6 at three
velocities -132.5 ... -136.6 dB. The stage costs 3.0 us against 7.0-8.9 us (2.3-2.9x). On the pedalled 12-key cluster in Live the no-helper penalty fell from
+0.25 to +0.12 of a deadline; on the Chopin excerpt from +0.20 to +0.09; in the 88-key Full case radiation is 3-5 % of the cost (8.47 -> 8.21
with the helper). The helper thread stays the default (it is still faster); `Worker threads 0` is workable now (Chopin no-helper p99.9 0.62-0.76 at 64-256
frames). Auto = Baked for Live and Balanced and Modal for Full (bit-exact with 0.5.5). Details and tables: `piano-v4/README.md`, "Update 0.6".

**Step 2, string fit.** The fitted string (force table of the first 0.12 s, then 29-460 damped complex rotors per layer) matches the Full bridge force in
frequency to 0.00-1.2 cents rms (3.7 cents worst partial), in decay to 0.00-0.6 dB/s rms (4.1 for the single scored partial of C6 mf), and in octave-band
envelope to 0.06-0.6 dB mean (0.13-2.7 max); the waveform error is -27.7 ... -47.7 dB (C2 forte worst: tension glide). Cost per voice: rotors 12-54 ns
(43-195 cycles), a whole fitted voice with its drive about 110 ns (400 cycles), against 0.32-3.8 us (1,150-13,700 cycles) marginal for the Live voice and
1.7-6.1 us for Full: 3x (C6) to 35x (C2). **The unexpected result:** the fitted string alone differs from complete Full by 1.5-7.5 dB (mean octave-band
envelope; 13-16 dB in the worst band for C4 and C6), because the sympathetic and leaky strings the note excites are a large part of a single note
in this model. A/B: `baked-ab-0.6.wav`, `baked-ab-0.6-isolated.wav` (adds Full without sympathetic and leaky strings).

**Recommendation for step 3.** Listen to the A/B first. If the isolated version of the fitted string is acceptable, integrate step 3 with the fitted force
driving the board coupling so the sympathetic and leaky strings stay live, starting with the bass (tension keys: 35x cheaper, C2 forte needs a glide curve) where
the gain is; the treble Live voice is already only 0.3-0.5 us. If the missing aftersound is audible, that is the expected cost of the force-only voice and the
sympathetic coupling must be proven before anything else.

## Imperfections and Voices (2026-10-25, 0.5.5)

Listener: "the string sounds too perfect... are the three strings all in tune? spread them a bit, like a unison"; and long overlapping pedalled
chords are thinned by the 16-voice cap. 0.5.5 adds, with an **Imperfection** amount (0 = 0.5.4 bit for bit, 1 default, 2 max; CLAP id 3, LV2
port 7; takes effect at a key's next note-on): per-key deterministic unison mistuning (0.3-2 cents spread, smaller in the treble, zero mean;
Weinreich 1977 for the double decay), uneven hammer contact (5-15 % strength, 30-90 us lag per string, passive, `contact.hpp`) and false-string
partial splitting (35 % of the multi-string keys, 2-4 partials, 0.3-2 Hz). All numbers are assumptions. Also a **Voices** setting (Auto / 16 / 32 /
48 / 64; CLAP id 4, LV2 port 8) capped by the adaptive shed levels of 0.5.4.1, and stealing the quietest voice with an 8 ms fade instead of
cutting the oldest. Measured: imperfections cost nothing; five-octave pedalled chords do not run in real time at any Voices value on the i3-8100
(Live mean 1.0-1.6 deadlines, bass tension and woken strings dominate), so the Auto cap stays 16 and adaptive shedding (default on) keeps the
plug-in alive at the cost of thinner chords; Full offline plays them complete (`big-chords-0.5.5.wav`). Listen: `imperfection-demo-0.5.5.wav`,
`chopin-prelude-0.5.5.wav`, `big-chords-0.5.5.wav`. Details: `piano-v4/README.md`, "Update 0.5.5". Next: bass chords at Live (tension and woken
strings), the radiation stereo width.

## Hotfix 0.5.4.1 (2026-10-25): 64-frame blocks, xruns, "delay per strike"

The user's PipeWire runs at 64 frames / 48 kHz (1.33 ms). Findings: the audio path adds no latency (onset = hammer flight, 4.5-8.4 ms,
constant over strikes); the reported delay is late blocks. Causes: bass chords under the pedal cost 0.8-1.8 deadlines per block at Live
(tension SAV, woken strings, hammer contact), and the audio thread waited for a descheduled radiation helper. Fixes: stall fallback
(radiation moves to the audio thread, bit-identical), shorter helper spin, adaptive load shedding (new parameter, default on; Live and
Balanced only), and a race in the idle/inline paths (stale ring record radiated at note-on). No clipping found (peak <= 0.84). Honest
limit: rolling four-note bass chords at 64 frames still miss about 0.5% of blocks on an i3-8100; use 128 there. Details: `piano-v4/README.md`.

## Real-time iteration (2026-10-25, 0.5.4)

0.5.4 is about response time, not sound. The model has no latency of its own (sample-accurate events), so response = host block
size: 128 samples at 48 kHz = 2.7 ms, 256 = 5.3 ms. The plug-ins were too slow for that (0.9x real time on the pedalled 12-key
cluster, one thread). Now: a **Quality** parameter (Live default / Balanced / Full), a radiation helper thread (bit-identical output
for any thread count), exact SIMD-friendly kernels, Live/Balanced savings (mode truncation, earlier sleep, tension floor, fast sums, a
fused hammer-flight step, idle skip) and a repeated-note fix (per-key held count). Live at block 128: pedalled cluster 3.96x, unpedalled
3.82x, Chopin excerpt 4.45x real time; Full (the offline model, selected automatically by CLAP offline render mode) is bit-identical to
0.5.3 (`--previous`; the old `--previous` is `--v052`). `chopin-prelude-0.5.4*.wav` equals the 0.5.3 render byte for byte (the offline
model is unchanged): listen to the Live/Balanced character in the plug-in. Residual risk: a descheduled helper can make a block late
(isolated misses in `realtime-report-0.5.4.json`); with 0 helpers the clusters pass at about 2x. Next (0.5.5): stereo width (per-key
radiation decorrelation / microphone model); the radiation stage has about 2x headroom. Details: `piano-v4/README.md`, "Update 0.5.4".

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
