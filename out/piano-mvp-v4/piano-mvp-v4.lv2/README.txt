Piano MVP 4 - Bridge loss (LV2 0.4.6, Linux)

Extract the entire piano-mvp-v4.lv2 folder into ~/.lv2/.
Keep manifest.ttl, piano.ttl and piano-mvp-v4.so together.
Restart/rescan your LV2 host (for example Carla or Ardour).
Look for "Piano MVP 4 - Bridge loss" under instruments/synthesizers.

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

Source, listening comparisons and licence:
https://github.com/gbasilveira/plucked-physical-modeling-engine/tree/mvp/piano
