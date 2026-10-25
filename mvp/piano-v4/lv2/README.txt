Piano MVP 4 - Calmer highs (LV2 0.4.9, Linux)

Extract the entire piano-mvp-v4.lv2 folder into ~/.lv2/.
Keep manifest.ttl, piano.ttl and piano-mvp-v4.so together.
Restart/rescan your LV2 host (for example Carla or Ardour).
Look for "Piano MVP 4 - Calmer highs" under instruments/synthesizers.

Connect MIDI to MIDI input and Left/Right audio to your playback outputs.
MIDI notes 21-108, velocity, CC64 sustain, CC120 panic, CC123 note release.
One fixed preset; no custom GUI. Start at a 512- or 1024-sample buffer.

This is the same experimental piano model as the CLAP version; format conversion
does not change its timbre. See mvp/piano-v4/README.md in the source repository.
The instrument is limited to 12 active keys. Bass uses one or two strings;
the middle and upper registers use three, sharing one hammer and bridge per key.
Cross-key sympathetic ringing is still deferred.
Version 0.4.6 adds a passive diagonal approximation to losses into soundboard
modes omitted from the real-time mechanical bridge. The radiation model is unchanged.
Version 0.4.8 adds softer felt in the bass/middle registers, guided by comparisons
with lossless Salamander recordings. Treble voicing returns to the original setting.
Version 0.4.9 reduces excess energy above ~3 kHz with two linked changes: a
bridge-top mass that low-passes the force entering the board (about 2 kHz corner,
assumed 55 g) and string viscosity that stops growing as k^2 above 2.5 kHz.
No output EQ. Measured against the Salamander reference; low-frequency body
response (knock, low modes, sympathetic ringing) is unchanged and still lacking.

Source, listening comparisons and licence:
https://github.com/gbasilveira/plucked-physical-modeling-engine/tree/mvp/piano
