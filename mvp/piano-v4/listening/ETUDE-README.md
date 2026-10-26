<!-- SPDX-License-Identifier: MIT -->
# Second classical fixture (0.6)

**Frédéric Chopin — Etude in C minor, Op. 10 No. 12 ("Revolutionary").**
MIDI performance/sequencing by **Bernd Krueger**, from piano-midi.de, downloaded unchanged from
[Wikimedia Commons](https://commons.wikimedia.org/wiki/File:Chopin_-_Revolutionary_Etude.mid)
(licence shown on that page: CC BY-SA 3.0 de). Direct source:
https://upload.wikimedia.org/wikipedia/commons/0/02/Chopin_-_Revolutionary_Etude.mid .
Retrieved 2026-10-26. SHA-256 `a376584d5b8c7fb3983c46e4ddef3e5866c2c7ec11a3dd7b59fdecea9ba3914f`
(its Git SHA-1 `af54f06d33e0f07196c21e68125e6bfee39d510c` equals the value Commons lists).

The MIDI and the audio renderings of this performance are distributed under
**[Creative Commons Attribution-ShareAlike 3.0 Germany](https://creativecommons.org/licenses/by-sa/3.0/de/deed.en)**.
Changes: synthesised with the experimental PME instrument; complete piece followed by 4 s of release; last 50 ms
faded; listening copies use one common gain per piece. This is not an acoustic recording and implies no
endorsement by the MIDI author. Engine and scripts remain MIT.

Why this piece: 2088 note attacks in 139 s (up to 7 keys held, keys 24-92 = C1-G#6), a fast left hand over the
whole bass register, velocities 28-116 and 345 CC64 sustain messages (141 down, 204 up), so the source already
carries its pedalling and none is added. It stresses exactly what the model finds expensive: low strings with
tension modulation and long, pedalled decays.

`prepare_etude.py --fetch DIR` downloads the pinned file into a new directory and verifies the SHA-256;
`prepare_etude.py [--check]` validates it (mido==1.3.3) and writes or compares the 48 kHz event schedule and
audit. Ignored controllers (CC7 volume, CC10 pan, CC91 reverb) occur only at time zero. Three same-key overlaps
exist; the full-piece converter handles them with the per-key note count (`render_full_piece.convert`).
