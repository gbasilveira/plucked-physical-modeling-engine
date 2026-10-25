# Fixed classical listening fixture

**Frédéric Chopin — Prelude in E minor, Op. 28 No. 4.**
MIDI performance/sequencing by **Bernd Krueger**, from piano-midi.de, downloaded
unchanged from [Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Chopin_-_Prelude_in_E_Minor.mid).
Direct source: https://upload.wikimedia.org/wikipedia/commons/5/58/Chopin_-_Prelude_in_E_Minor.mid .
Retrieved 2026-10-25. SHA-256:
`8b8984ae232642ff1979b84af642d5d5db60f7f7f20b3745f2d4b4f17cdaf908`.

The MIDI and audio renderings of this performance are distributed under
**[Creative Commons Attribution-ShareAlike 3.0 Germany](https://creativecommons.org/licenses/by-sa/3.0/de/deed.en)**.
Changes: synthesised with the experimental PME instrument; excerpted source time
40–64 seconds, followed by two seconds of key/pedal release; last 50 ms faded.
Listening copies also fade in over 10 ms to avoid a click at the excerpt boundary.
Listening exports use a documented common gain. This is not an acoustic recording
and does not imply endorsement by the MIDI author. Engine/scripts remain MIT.

The 26-second excerpt contains 174 note attacks, MIDI velocities 18–92, keys
35–84, rubato/tempo changes and sustain. Simulating the complete source from zero
before exporting at 40 seconds preserves the preceding pedal and string state.
All note, velocity, tempo and CC64 data are retained. The fixed instrument ignores
source mixer volume, centre pan and external reverb sends; these appear only at
time zero and are listed in the audit. No artificial humanisation is added.

`prepare_classical.py` validates the source SHA, MIDI format, tempo map, note
pairing, ranges and controllers. The source contains three same-key overlaps after
64 seconds, outside the simulated passage. `--check` also verifies the exact event
schedule and audit against the committed files: any change fails before rendering.
The full file has 604 note attacks, 335 tempo events and 133 sustain messages.

```bash
python3 -m venv /tmp/piano-midi-venv
/tmp/piano-midi-venv/bin/pip install -r mvp/piano-v4/listening/requirements.txt
/tmp/piano-midi-venv/bin/python mvp/piano-v4/prepare_classical.py --check
cmake --build out/piano-mvp-v4 --target piano-midi-render -j2
/tmp/piano-midi-venv/bin/python mvp/piano-v4/render_classical.py out/piano-mvp-v4
```

Keep this performance, timing, output gain and excerpt fixed across future
iterations. Update the version/output name when changing the instrument. Preserve
the previous WAV and the new per-render audit. `chopin-prelude-latest.wav` is the
current portable listening copy; versioned WAVs allow comparison on the same device.

## Full-piece render (0.5.3)

`render_full_piece.py` renders the complete source performance (first event to last note-off plus a
4 s release tail; last 50 ms faded; the same fixed gain as the excerpt, no start fade). The MIDI file,
author credit, licence (CC BY-SA 3.0 Germany), source (Bernd Krueger, piano-midi.de, via Wikimedia
Commons) and SHA-256 above are unchanged. Changes to the work: synthesised with the experimental PME
instrument. The three same-key overlaps (a note-on for a still-sounding key) are re-strikes; the note-off
that would cut the second note is dropped (see the audit and the README). All note, velocity, tempo and
CC64 data are otherwise retained; mixer controls CC7/CC10/CC91 are ignored as in the excerpt.
