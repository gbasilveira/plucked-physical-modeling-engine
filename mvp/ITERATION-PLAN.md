# Piano listening iterations

Goal: improve the acoustic-piano character, with one change at a time and audible,
reproducible comparisons. A passing numerical test is not a listening endorsement.
Keep MVP 1 and MVP 2 and their artifacts as baselines. Work on branch `mvp/piano`.

| Step | Experiment | Evidence before proceeding | State |
| --- | --- | --- | --- |
| 1 | Published string/hammer inputs | Source provenance, compiled tensions and loss projection, 88-key tests | Implemented in MVP 2; listener still reports electric-guitar character |
| 2 | Remove output saturation and direct force mix; add explicit force-to-pressure soundboard path | Same middle-C excitation through old, linear-baseline and body-only paths; matched-level WAVs and metrics; linearity/impulse/stability tests | Implemented in MVP 3; listening feedback pending |
| 3 | Bridge mobility and feedback | Compare fixed-end/body-filter experiment with an energy-checked coupled bridge; measure partial decay changes | Planned; do not just add reverb or global decay |
| 4 | Appropriate unison string groups | Coupled two-/three-string notes versus single string; beat frequencies and decay envelopes; coherent hammer/string mass treatment | Planned |
| 5 | Hammer/attack calibration | Soft/medium/hard strikes; contact duration, force peak and initial partial distribution versus a cited reference | Planned |
| 6 | Sympathetic strings and pedal behaviour | Pedal up/down, released keys, repeated notes, chords; CPU/voice accounting | Planned |

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
The next outstanding issues remain the nominal board response and absent bridge
feedback; a full piano identity cannot be inferred from this first experiment.

Publish each completed experiment with a short change note, a single-note A/B,
the same melody/chord demo, reproducible commands and explicit approximations.
The listener's feedback selects the next refinement; independent engineering work
can continue, but do not stack unvalidated timbre changes into an opaque preset.

Sources:

- Chabassier & Duruflé, RT-0425 revised: https://inria.hal.science/hal-00688679v2/document
- AudioLabs measurement description: https://audiolabs-erlangen.de/resources/MIR/2023-ISMIR-PianoSynth/
- Bank et al., radiation versus mechanical response: https://home.mit.bme.hu/~bank/publist/jasp03.pdf
