Piano MVP 4 - String tension (LV2 0.5.2, Linux)

Extract the entire piano-mvp-v4.lv2 folder into ~/.lv2/.
Keep manifest.ttl, piano.ttl and piano-mvp-v4.so together.
Restart/rescan your LV2 host (for example Carla or Ardour).
Look for "Piano MVP 4 - String tension" under instruments/synthesizers.

Connect MIDI to MIDI input and Left/Right audio to your playback outputs.
MIDI notes 21-108, velocity, CC64 sustain, CC67 una corda (soft pedal), CC120 panic, CC123 note release.
One fixed preset; no custom GUI. Start at a 512- or 1024-sample buffer.

This is the same experimental piano model as the CLAP version; format conversion
does not change its timbre. See mvp/piano-v4/README.md in the source repository.
The instrument is limited to 12 active keys. Bass uses one or two strings;
the middle and upper registers use three, sharing one hammer and bridge per key.
Version 0.4.6 adds a passive diagonal approximation to losses into soundboard
modes omitted from the real-time mechanical bridge. The radiation model is unchanged.
Version 0.4.8 adds softer felt in the bass/middle registers, guided by comparisons
with lossless Salamander recordings. Treble voicing returns to the original setting.
Version 0.4.9 reduces excess energy above ~3 kHz with two linked changes: a
bridge-top mass that low-passes the force entering the board (about 2 kHz corner,
assumed 55 g) and string viscosity that stops growing as k^2 above 2.5 kHz.
No output EQ. Measured against the Salamander reference; low-frequency body
response (knock, low modes, sympathetic ringing) is unchanged and still lacking.

Version 0.5.0 adds CC67 una corda (applies to notes struck while down), and keys from
F#6 (MIDI 90) upward have no dampers (ring on after note-off). Voice limit stays 12.

Version 0.5.1 couples all strings to one shared soundboard (the lowest 128 board modes
are shared by every string and act in both directions), with a different bridge position
for each key, and simulates strings whose dampers are lifted (sustain pedal down, keys
from F#6 up) even when not struck, so they ring sympathetically. CPU: at most 8
sympathetic strings are simulated in this plug-in (plus the 12 struck keys); a pedalled
chord is about 6 percent slower than 0.5.0. Strings start from rest when they wake. The
board is a nominal rectangular plate with an assumed bridge curve; none of this is
measured. See mvp/piano-v4/README.md ("Update 0.5.1").

Version 0.5.2 adds geometric (tension-modulation) nonlinearity to the struck strings:
large transverse motion raises the string tension, giving a small pitch rise at the strike
and extra, slightly inharmonic partials (strongest in the bass at fortissimo). In this plug-in
it is enabled only for keys A0 to B2 (MIDI 21-47) to stay near real time; the offline renders
use all keys. Sympathetic (unstruck) strings stay linear. Effects are small by design
(about 0.5-1.5 cents of pitch at velocity 92). See mvp/piano-v4/README.md ("Update 0.5.2").

Source, listening comparisons and licence:
https://github.com/gbasilveira/plucked-physical-modeling-engine/tree/mvp/piano
