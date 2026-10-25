# Lossless acoustic-piano reference

Reference: **Salamander Grand Piano v3**, Yamaha C5, recorded by **Alexander Holm**.
The [source repository](https://github.com/sfzinstruments/SalamanderGrandPiano)
documents 48 kHz/24-bit recordings, 16 velocity layers, and two AKG C414 microphones
in AB configuration approximately 12 cm above the strings. The original archive is
https://archive.org/details/SalamanderGrandPianoV3 .

Samples and excerpts: **CC BY 3.0**, Alexander Holm; see the accompanying original
`SALAMANDER-LICENSE.txt` and https://creativecommons.org/licenses/by/3.0/ .
Code remains MIT. No sample audio is used by the synthesizer during playback.

`salamander-source.json` pins the source commit and nine original Git blob hashes:
A2, C4, C6, layers 4, 8, 12. The source SFZ maps those layers to velocities 37–43,
57–64, 89–96; we use 40, 60, 92 to select matching model strengths. These numbers
are sampler mappings, **not measured hammer velocities**. Raw layer volume is not
treated as a calibrated sound-pressure measurement or a measured velocity curve.

```bash
python3 mvp/piano-v4/fetch_reference.py
python3 mvp/piano-v4/calibrate_reference.py out/piano-mvp-v4 out/piano-reference-cache
out/piano-mvp-v4/piano-hammer-audit --previous
out/piano-mvp-v4/piano-hammer-audit
```

Requires NumPy, ffmpeg, and the compiled renderers. The downloader uses Python's
standard library. It caches approximately 13 MB of untouched FLACs in ignored
`out/piano-reference-cache`, checks their bytes against the pinned Git hashes,
and records SHA-256 hashes. These recordings are not converted through MP3/OGG.

The comparison aligns onset using the first 1 ms RMS block above 3% of the maximum
in the first second. It measures DC-removed stereo power with a Hann window in
10–100 ms, 50–250 ms, 0.5–1 s and 2–2.5 s windows. The JSON preserves centroid,
energy share above 2 kHz, levels and alignment positions. The high-band error is
the absolute dB ratio of the model/reference energy shares, averaged across the
two attack windows. This is one diagnostic, not an overall perceptual quality score.

`C4-reference-0.4.8-0.4.9.wav` (0.4.8 version: `C4-reference-previous-soft-felt.wav`; calibration renders from 0.4.9 are 24-bit and `reference-calibration-0.4.9.json` adds octave-band tables) contains three groups (velocities 40, 60, 92).
Each group plays **reference → previous model → candidate**, with 2.5-second notes
and 0.25-second gaps. Groups start at 0, 8.25 and 16.5 seconds; total 24.75 seconds.
Changes to reference audio in this diagnostic: onset crop, excerpt, initial-RMS
matching to −24 dBFS, short boundary fades, and 16-bit WAV export. Gains and exact
order are recorded in `reference-calibration-0.4.8.json`. This level matching is
for diagnostic timbre listening only: Chopin exports retain the same fixed gain
and the complete original MIDI dynamics.

The first comparison rejected softening the whole keyboard: it improved the bass
and middle-register attack high-band ratios but worsened the treble. The candidate
therefore uses K×0.35 through C5, blending smoothly to K×1 by C6. This is a declared
voicing experiment selected using these recordings, not a measured stiffness curve.

Limits: close-mic recordings include their microphones, placement, piano and room;
our output is nominal far-field pressure. Matching these WAVs cannot uniquely
identify hammer stiffness, string losses or body geometry. Decay and spectral
balance still differ. A measured bridge/soundboard transfer would be needed to
separate excitation from body response. The nine selected examples are a small
development reference set, not an independent validation corpus.
