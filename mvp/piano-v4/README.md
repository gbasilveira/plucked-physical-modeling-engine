# MVP 4: bridge feedback

## Update 0.7b: "Grand", highest offline accuracy (not in the plug-ins)

0.7b adds to the 0.7a reference: a statistical board from 3.6 to 16 kHz, direct string radiation and the iron plate, a fine FDTD grid and a diffraction model for the ray part, and the full air-loaded
bridge admittance (diagonal loading for all 144 low modes in the strings' board). Against Salamander (nine notes, no fitting) the 4 to 8 kHz hole of 0.7a is closed (-20.6 dB to +12.6 dB; +2.4 without the
ray-to-FDTD level matching) and 2 to 4 kHz improves (-7.9 to +2.5 dB), but the mean late-window error equals 0.6's (7.6 against 7.5 dB): honest numbers, ablation, limits and the diagnostic renders (body taps, stems, a solo string) in
`mvp/piano-grand/README.md` ("Update 0.7b"); plan: `mvp/ITERATION-PLAN.md`. Renders in `out/piano-mvp-v4/grand/`. The 0.6 plug-ins are unchanged (`piano-previous-bitexact`).

## Update 0.7a: "Grand", an offline reference of the grand's body and near field (not in the plug-ins)

Full sounded like a vertical piano. 0.7a builds, offline only, a Reissner-Mindlin FEM grand soundboard (explicit ribs and bridges, per-key string points), a 3-D FDTD of the
air with the board as a two-way coupled moving boundary (both faces, rim, cavity, lid open / half / closed, floor, absorbing layer; room excluded), a Rayleigh/lid-image model above
1.4 to 2 kHz, close-AB / player / audience microphones, a render path (`piano-grand-render`) that couples the unchanged 0.6 Full strings and hammer to the new board, and a validation
against Salamander. Result, honestly: the board and air loading are in place (first mode 49.9 Hz in vacuo, 44.8 Hz loaded), reciprocity and passivity are tested, but with physics alone
**the high frequencies are far too low (no board above 3.6 kHz) and the stereo coherence is not improved**. Details, limits, numbers and what 0.7b needs: `mvp/piano-grand/README.md`;
plan: `mvp/ITERATION-PLAN.md`. The 0.6 plug-ins are unchanged (`piano-previous-bitexact`).

## Update 0.6: baked radiation, and a string-fit experiment (display name "Piano MVP 4 - Baked radiation")

Why: Full is the sound reference but cannot run live (88 keys pedalled: 8.3x real time on average, 10.8x at the attack on the i3-8100), and
Live thins dense bass chords by shedding voices. The agreed direction is to bake what is static and compute live only what interacts
(plan, table and acceptance criteria: `mvp/ITERATION-PLAN.md`, "0.6"). This iteration implements step 1 (the radiation stage) and runs the
step 2 experiment (fitted strings for C2, C4, C6) without putting it in the plug-in. Full with Radiation Modal (the Full default) is bit
for bit 0.5.5 (`piano-previous-bitexact` unchanged).

### Step 1: the baked radiation (in the plug-ins)

The 512-mode radiation stage is linear and time invariant in the 88 port drives, but it is already an efficient IIR form: the drive is a
rank-512 sum of the ports, not an impulse response per port. What the "bake" does, measured and chosen:

1. **IR length (Modal -> impulse response).** The slowest radiating mode (20.7 Hz, sigma 1.30 /s) falls below -100 dB after 8.9 s = 425,000
   taps; 266 modes radiate at all, 66 are still above -100 dB after 0.1 s, 26 after 0.25 s, 12 after 0.5 s, 6 after 1 s, 3 after 2 s, 1 after 8 s.
   An IR per port (and one more for an unstruck string, which drives only the 176 lowest modes) would be 104+ IRs of 425,000 taps x 2 channels
   (about 600 MB as time-domain floats, twice that as FFT partitions).
2. **Uniform-partitioned FFT convolution** (`mvp/piano-baked/part_conv.hpp`: radix-2 FFT, direct-form head of P taps for zero latency,
   frequency-delay line, one inverse FFT for L and R, no allocation in `step()`; unit test B6 against a direct convolution:
   -132 dB at P 64, -134 dB at P 128). On 6-8 s of the Chopin excerpt (Full recording, 85 ports driven per sample on average, the dense worst case) with IRs
   truncated to T: T = 0.25 s: error -24/-25 dB (L/R) re signal, 37 us per sample, 33 MB; T = 0.5 s: -31/-32 dB, 69 us, 65 MB; T = 1 s: -35/-36 dB,
   120 us, 130 MB (the low modes ring on). The modal bank costs 7.0-8.9 us for the same drive. **Convolution is 5-17x slower and not
   exact below 8.9 s: rejected.** (`baked-radiation-study-0.6.txt`, `piano-baked-study`.)
3. **Fitted IIR = reduced modal bank (chosen).** (a) 246 of the 512 board modes have a radiation weight of 1e-18 or less (numerically zero
   residues in the table, so they radiate nothing to either channel): dropping them is exact. (b) Energy-based selection: `piano-baked-select` measures the radiated
   energy of every mode over a training corpus in Full (a chromatic pedalled sweep of all 88 keys at three velocities, and Chopin
   5-30 s and 30-40 s; `merge_importance.py` writes `baked_importance.hpp`); modes are dropped from the weakest while the dropped energy stays
   below `tolerance`. It does not help: all 266 radiating modes carry more than 1e-6 of the energy (tolerance 1e-6 drops 5 modes and costs
   -58 dB; the default 3e-9 drops none beyond the 246). (c) The drive is accumulated in float with phi stored chunk-major (`kern::bankDrive`,
   four chunk chains to hide the add latency, ports added in list order, no FMA contraction, so a vector build equals the scalar one), the
   mode update stays in double (a float recursion would shift the 130 Hz modes by 0.35 cent), the 288 slots (266 modes padded to groups of
   32; unstruck strings drive the first 96) are summed with eight fixed accumulators.
   Result on the Chopin excerpt, Full, baked against modal: **rms error -133.9 dB re signal** (max abs difference 2.3e-8; octave bands 63 Hz
   ... 4 kHz: -130, -141, -144, -143, -130, -123, -108 dB); single notes C2/C4/C6 at three velocities: -132.5 ... -136.6 dB (test B1;
   target was -80 dB). Cost of the stage alone, single thread, 85 ports per sample: modal 7.0-8.9 us (drive 4.4-4.9, modes 1.2, sum 1.3), baked
   3.0 us (drive 2.3, modes 0.5, sum 0.09), 2.3-2.9x. The drive pass is bound by L2 reads of the 94 KB phi table once per sample; a block
   GEMM (several samples per phi load) would be the next step.
4. **Setting.** `Radiation: Auto | Modal | Baked` (CLAP parameter id 5, LV2 port 9, appended after port 8; stepped, default Auto, saved in byte 10
   of the state; 7-byte and 9-byte states load with Auto). Auto = Baked in Live and Balanced, Modal in Full ("Full keeps the modal bank for
   bit-exactness"); Modal and Baked force one bank in every quality. Real-time safe; the choice is taken at the next block (or sample) and clears
   the bank taking over (the other bank's radiation tail is lost; the 4.4 ms delay line is kept). `piano-midi-render`, `piano-block-bench`:
   `--radiation auto|modal|baked`. Library: `Piano::setRadiation`, `setBakedTolerance` (before prepare).

CPU (i3-8100, `piano-block-bench`, mean deadline ratio; p99.9 in parentheses; helper = Worker threads 1). Chopin 52-58 s, Live:

| block | modal + helper (0.5.5) | baked + helper | baked, no helper | modal, no helper |
| --- | --- | --- | --- | --- |
| 64 | 0.21 (0.66) | 0.19 (0.65) | 0.28 (0.76) | 0.40 (0.91) |
| 128 | 0.20 (0.62) | 0.19 (0.63) | 0.28 (0.74) | 0.40 (0.85) |
| 256 | 0.20 (0.49) | 0.19 (0.49) | 0.29 (0.62) | 0.40 (0.73) |

12-key pedalled cluster, Live, 4 s: modal + helper 0.53 / baked + helper 0.53 / baked, no helper 0.65-0.68 / modal, no helper 0.76-0.78 (64, 128
and 256 frames). 88 keys pedalled, Full, 128 frames: modal + helper 8.47, baked + helper 8.21, baked no helper 8.41, modal no helper 8.89 (the
strings dominate: radiation is 3-5 % there).
**Can the helper thread go?** Baked removes about half of the penalty of running without the helper (+0.25 -> +0.12 of a deadline on the
cluster, +0.20 -> +0.09 on Chopin) and the no-helper Chopin p99.9 stays below 0.8; but the helper is still faster, so it stays the default;
`Worker threads 0` is now a workable setting on 2-core machines. Radiation was never the bottleneck: the strings are (step 2).

### Step 2: fitted strings for C2 (36), C4 (60), C6 (84) (experiment; not in the plug-in)

Pipeline (all reproducible): `piano-baked-capture KEY VEL PREFIX` renders the note in Full (no pedal, 8 s, imperfection 1) and writes the bridge
force of its port and the stereo output (also with sympathetic and leaky strings off: `.iso`); `fit_string.py` (numpy only; `bakelib.py`) finds
the partials (peak picking with a tracked f0 and inharmonicity B), extracts each partial's complex envelope (FFT band-pass, shifted to
baseband, decimated), fits it with 1-6 damped complex exponentials per partial (matrix-pencil method, smallest order whose residual is < 1 %),
prunes components below 1e-8 of the energy (-80 dB) and writes a `.fit` file (the force table of the first 0.12 s, crossfaded over 20 ms into the
modal sum; components: frequency, decay rate, complex amplitude at 0.1 s). `FittedString` (`mvp/piano-baked/fitted_string.*`) runs the
components as float complex rotors (`s <- z s`, force = sum Re s, eight fixed accumulators) and feeds the bridge-mass low pass and the baked
radiation exactly as `Piano` does for a struck port (check: Full force alone through this chain reproduces Full without sympathetic and leaky
strings at -137 ... -145 dB). Layers: MIDI velocity 38 / 76 / 114 (0.3 / 0.6 / 0.9). `baked-fit-report-0.6.json|txt`, fits in `baked-fits-0.6/`.

| key / vel | partials (scored) | rotors | freq error cents rms / max | decay error dB/s rms / max | force waveform error | octave-band envelope error, fit only, mean / max dB | missing sympathetic + leaky, mean / max | total vs Full, mean / max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 36 / 38 | 51 (14) | 140 | 0.00 / 0.01 | 0.02 / 0.04 | -47.7 dB | 0.06 / 0.13 | 1.62 / 2.82 | 3.62 / 14.01 |
| 36 / 76 | 73 (20) | 306 | 0.01 / 0.06 | 0.12 / 0.50 | -40.5 dB | 0.13 / 0.28 | 1.64 / 2.60 | 1.57 / 2.57 |
| 36 / 114 | 103 (24) | 460 | 0.31 / 1.53 | 0.29 / 0.92 | -27.7 dB | 0.48 / 0.84 | 1.51 / 2.77 | 1.52 / 2.74 |
| 60 / 38 | 30 (7) | 99 | 0.03 / 0.06 | 0.17 / 0.39 | -38.1 dB | 0.53 / 1.59 | 4.67 / 12.60 | 4.76 / 13.12 |
| 60 / 76 | 36 (10) | 189 | 1.18 / 3.74 | 0.60 / 1.39 | -37.1 dB | 0.46 / 1.66 | 4.72 / 13.21 | 4.78 / 13.74 |
| 60 / 114 | 37 (10) | 215 | 0.93 / 2.95 | 0.59 / 1.23 | -37.1 dB | 0.46 / 1.33 | 4.91 / 13.50 | 5.05 / 14.01 |
| 84 / 38 | 10 (1) | 29 | 0.01 / 0.01 | 0.00 / 0.00 | -37.1 dB | 0.60 / 2.65 | 7.05 / 15.04 | 7.37 / 15.32 |
| 84 / 76 | 10 (1) | 46 | 0.05 / 0.05 | 4.13 / 4.13 | -40.5 dB | 0.30 / 1.23 | 7.52 / 16.03 | 7.66 / 16.34 |
| 84 / 114 | 10 (1) | 55 | 0.00 / 0.00 | 1.03 / 1.03 | -42.2 dB | 0.26 / 0.80 | 7.36 / 16.32 | 7.56 / 16.63 |

"Scored" partials are those within 50 dB of the strongest in the 1.5-6 s window; frequencies are compared by an independent peak pick of Full and
of the resynthesis, decay is the slope of each partial's band envelope over 0.3-6 s; octave-band envelope = rms difference (dB) of 50 ms band
levels over 0.1-8 s, bands 63 Hz ... 8 kHz (centres), frames within 60 dB of the loudest. "Fit only" compares the fitted string with the Full force
through the same radiation; "missing sympathetic + leaky" compares that Full force-only output with the complete Full output; "total" fitted
against complete Full. At -80 instead of -60 dB the fit-only means are 0.5-4.2 dB (quiet bands, mostly pruned components).

**Finding: the sympathetic and leaky strings are a large part of the sound of a single note**, up to 7.5 dB mean envelope difference for C6 (16 dB in
the worst band): a force-only voice cannot reproduce them; they must stay live and be excited by the fitted force through the coupled board (step 3).
The fit itself is accurate (0.1-0.6 dB mean band envelope error; C2 forte worst in the waveform, -27.7 dB, because of the tension glide, which a sum of
fixed exponentials only approximates with 460 rotors). Chord (C2+C4+C6, mf): total 1.1 dB mean band envelope error (the low notes dominate).

Cost per voice (i3-8100 at 3.6 GHz, `piano-baked-fit cost|fitcost`, ns per sample and cycles = ns x 3.6): fitted voice, rotor bank only:
C2 (306 rotors) 54 ns (195 cycles), C4 (189) 32 ns (115), C6 (46) 12 ns (43); a fitted voice including its low pass and its drive of the radiation
about 110 ns (400 cycles) (the 3-voice pipeline costs 859 ns, one voice 557-645 ns; the shared radiation modes + sum are about 500 ns). The Live
voice, marginal (5 notes minus 4 notes): C2 3.8 us (13,700 cycles), C4 0.49 us (1,750), C6 0.32 us (1,150); one Live note alone 6.9 / 3.5 / 2.9 us
(including the board coupling and the strings it wakes). Full marginal 6.1 / 2.4 / 1.7 us. So the fitted voice is 3x (C6) to 35x (C2) cheaper
than the Live voice, with the gain concentrated in the bass, where Live also needs tension modulation.

A/B: `baked-ab-0.6.wav` (47 s): C2, C4, C6 at mf (5 s each: Full, 0.6 s gap, fitted with the baked radiation, 1.4 s gap), then the chord both ways;
`baked-ab-0.6-isolated.wav` (70 s) adds, between the two, Full with the sympathetic and leaky strings off (the fair reference for the fit itself).

### 0.6 piece renders and performance (whole pieces, 2026-10-26; plug-in code unchanged)

Two complete pieces, both CC BY-SA 3.0 Germany MIDI by Bernd Krueger (piano-midi.de via Wikimedia Commons): the Prelude Op. 28 No. 4 (106 s with
tail, 604 attacks) and the **Etude Op. 10 No. 12 "Revolutionary"** (142.8 s with tail, 2088 attacks, keys C1-G#6, up to 7 held, velocities 28-116,
345 CC64 messages already in the file, so no pedalling is added). The Etude is pinned like the Prelude: `listening/chopin-op10-no12.mid` (SHA-256
`a376584d...914f`), `.events`, `.audit.json`, `listening/ETUDE-README.md` (source URL, licence, changes) and `prepare_etude.py [--fetch DIR | --check]`.
Overlapping same-key notes (3 in the Etude) use the per-key count of `render_full_piece.convert`.

```bash
python3 mvp/piano-v4/prepare_etude.py --check            # mido==1.3.3 (listening/requirements.txt)
python3 mvp/piano-v4/render_pieces_06.py out/piano-mvp-v4     # 3 modes x 2 pieces, ~11 min on the i3-8100
python3 mvp/piano-v4/perf_pieces_06.py out/piano-mvp-v4       # 28 bench runs per piece set, ~40 min
```

`piano-midi-render` gained `--float` (32-bit float WAV, no clip check) and reports the maximum number of struck voices; `piano-block-bench` gained
`--whole`, `--series FILE`, the worst sliding 1 s window and time spent shed. Files per piece and mode `M` in `full|live|balanced`:
`<piece>-0.6-M-raw.wav` (float, unnormalised), `<piece>-0.6-M.wav` (24-bit listening copy; one gain per piece, Full's peak at -1 dBFS,
the same for every mode so levels compare), `<piece>-0.6-audit.json`, `<piece>-0.6.events`. Live and Balanced are the plug-in defaults (Radiation Auto =
Baked, preset Voices, Imperfection 1) with adaptive shedding **off** (deterministic). Full is the offline reference (Modal radiation, 88 voices).

| piece | mode | peak (raw) | voice steals | max struck voices | render time | x real time |
|---|---|---|---|---|---|---|
| Prelude | Full | 0.163 | 0 | 18 | 137.4 s | 0.77 |
| Prelude | Live | 0.176 | 4 | 16 | 22.0 s | 4.80 |
| Prelude | Balanced | 0.178 | 0 | 18 | 30.3 s | 3.49 |
| Etude | Full | 0.286 | 0 | 38 | 296.7 s | 0.48 |
| Etude | Live | 0.310 | 275 | 18 | 62.0 s | 2.30 |
| Etude | Balanced | 0.320 | 69 | 25 | 111.2 s | 1.28 |

No numerical faults and no clipped samples. **Live minus Full, octave-band energy (dB, mono sum, whole piece):**

| band (Hz) | 22-44 | 44-88 | 88-177 | 177-355 | 355-710 | 710-1.4k | 1.4k-2.8k | 2.8k-5.7k | 5.7k-11k | 11k-23k |
|---|---|---|---|---|---|---|---|---|---|---|
| Prelude Live | +6.8 | -1.1 | +1.0 | -0.9 | +0.1 | -0.3 | -0.8 | +0.1 | +0.0 | +0.1 |
| Etude Live | +5.3 | +0.6 | +0.8 | -0.6 | -0.1 | +0.4 | -0.3 | -0.1 | -0.0 | -0.2 |

Balanced is within 0.3 dB of Live in every band except 177-355 Hz in the Etude (Balanced +0.2, Live -0.6 against Full). Overall RMS is +0.4 to +0.5 dB above Full for both
Live and Balanced; the 1 s RMS envelope differs by 0.6-0.9 dB on average (max 12.6 dB Etude Live and 7.7 dB Prelude, single seconds; not traced to a cause, steals are the first suspect). The 22-44 Hz band carries only 0.2-0.3 % of Full's energy, so its +5 to +7 dB is audible as slightly more
sub-bass thump, not as a level change. The cause was not isolated (candidates: Live's 1e-6 truncation and sleep thresholds versus Full's exact
bass strings; steals in the Etude), so it is reported, not explained.

**Performance** (`performance-report-0.6.{json,txt}`, i3-8100, 48 kHz, 1 helper thread, deadline ratio = block time / block duration; "late" is
ratio > 1; adaptive shedding on means the plug-in's governor). Prelude (Live/Balanced) and Etude:

| piece | quality | adaptive | block | x real time | mean | p99 | p99.9 | max | late/total | max shed, time shed | steals |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Prelude | Live | off | 64/128/256 | 5.00 | 0.20 | 0.50/0.48/0.47 | 0.74/0.70/0.63 | 1.67/1.58/1.45 | 18/79330, 7/39665, 3/19833 | 0 | 4 |
| Prelude | Live | on | 64/128/256 | 5.88/5.56/5.26 | 0.17/0.18/0.19 | 0.35/0.45/0.46 | 0.69/0.67/0.60 | 1.43/1.31/1.46 | 9/79330, 4/39665, 1/19833 | 3 (43.2 s) / 3 (16.8 s) / 1 (12.1 s) | 16/6/4 |
| Prelude | Balanced | off | 64/128/256 | 3.45/3.57/3.45 | 0.29/0.28/0.29 | 0.67/0.65/0.65 | 0.97/0.95/0.94 | 2.00/2.52/1.76 | 55/79330, 26/39665, 7/19833 | 0 | 0 |
| Prelude | Balanced | on | 64/128/256 | 4.55/4.55/4.00 | 0.22/0.22/0.25 | 0.39/0.41/0.60 | 0.79/0.77/0.77 | 1.57/1.43/1.25 | 19/79330, 14/39665, 2/19833 | 3 (49.3 s) / 3 (49.4 s) / 2 (14.6 s) | 19/19/2 |
| Prelude | Full | off | 128/1024 | 0.75/0.75 | 1.33/1.34 | 2.01/2.38 | 2.40/2.79 | 3.42/2.84 | 35772/39665, 4458/4959 | 0 | 0 |
| Etude | Live | off | 64/128/256 | 2.22/2.27/2.27 | 0.45/0.44/0.44 | 1.06/1.05/1.04 | 1.38/1.36/1.36 | 1.79/1.68/2.11 | 2211/107075, 1049/53538, 511/26769 | 0 | 275 |
| Etude | Live | on | 64/128/256 | 5.00/5.00/4.17 | 0.20/0.20/0.24 | 0.43/0.45/0.53 | 0.62/0.66/0.70 | 1.22/1.86/1.21 | 8/107075, 15/53538, 2/26769 | 3 (140.5 s) / 3 (140.4 s) / 2 (128.6 s) | 581/676/453 |
| Etude | Balanced | off | 64/128/256 | 1.27 | 0.79 | 1.82/1.83/1.80 | 1.96/2.03/1.94 | 3.32/2.22/2.03 | 30898/107075, 15577/53538, 7752/26769 | 0 | 69 |
| Etude | Balanced | on | 64/128/256 | 4.35/4.17/3.57 | 0.23/0.24/0.28 | 0.47/0.46/0.58 | 0.66/0.65/0.69 | 5.45/1.17/1.12 | 9/107075, 6/53538, 2/26769 | 3 (140.8 s) / 3 (140.8 s) / 2 (140.8 s) | 676/590/461 |
| Etude | Full | off | 128/1024 | 0.47/0.48 | 2.14/2.10 | 3.92/3.86 | 4.26/4.25 | 4.69/4.51 | 52002/53538, 6498/6693 | 0 | 0 |

Reading: the Prelude is comfortable in Live (mean 0.20, worst sliding second 0.47). The Etude is **not** real-time with shedding off even in
Live: mean 0.44, p99 1.05, worst sliding second 1.12-1.17 and 2.1 % late blocks at 64 frames (Live's 16-voice cap already steals 275 times).
With the governor on it holds (0.20 mean, p99.9 0.62-0.70, 2-15 late blocks of 27k-107k) but sits at shed level 3 for essentially the whole piece and
steals 450-680 notes, so the Live preset is shed-limited on this piece; Balanced has the same story. Differences between block sizes are small except
the late-block count, which falls with larger blocks (a late block is the same cost spike spread over a longer deadline). One run
(Etude Balanced adaptive 64) has a single 5.45 block, a one-off on a 107k-block run.

**Full** over the whole piece: Prelude mean 1.33 deadline (0.75x real time), worst sliding second 2.05 at 63 s; Etude mean 2.14 (0.47x), worst second
4.16 at 91 s. Block 1024 changes nothing (1.34 / 2.10). **Expensive passages** (3 s windows of the Full run): Prelude 61-64 s (mean 1.97, the
climax: 35 strikes B1-C6 at velocity ~58, 7 pedal changes), 67-70 s (1.83) and 91-94 s (1.78: soft, 13 strikes E1-E4, 5 below C3, pedal changes; a low strike rate with a high
cost, so sounding tails rather than new strikes drive it, an inference from the strike counts only); Etude 89-92 s (3.57; 72 strikes in 3 s, G1-C6, 11 below C3, 11 pedal changes),
23-26 s (3.33), 112-120 s (3.2-3.3: 33-38 strikes but 12-13 below C3 and 8-14 pedal changes) and 59-66 s (3.1). Every expensive window has strikes below C3 and
frequent pedal changes; the cost split by mechanism was not profiled here (earlier profiles point at the pedalled low strings with tension modulation,
sympathetic and leaky strings; up to 38 simultaneous struck voices in the Etude).

**Full on more hardware.** Perfect scaling of the voice cost over N cores gives N = mean ratio for the whole piece and N = worst-second ratio for
glitch-free playback: **Prelude 1.3 cores (mean) / 2.1 (worst second) / 2.4 (p99 block 2.0, max 3.4)**; **Etude 2.1 / 4.2 / 4.7 (p99 3.9)**. Equivalently
Full needs this machine to be 1.3-2.1x faster for the Prelude's mean and worst second, and 2.1-4.2x faster for the Etude; with 20-30 % headroom
for a safe deadline: about 3 cores / 5-6 cores. This is a lower bound: the model today uses one helper thread (radiation only) and the voice work is
serial per block, so a real multi-voice partition would scale below ideal (shared board coupling). Four cores of an i3-8100 would be borderline for
the Etude's worst seconds even with perfect scaling.

### Reproduce

```bash
B=out/piano-mvp-v4; E=mvp/piano-v4/listening/chopin-op28-no4.events
# step 1: mode energies in Full (modal), merged into mvp/piano-baked/baked_importance.hpp; study of the three radiation forms
$B/piano-baked-select sweep sweep.txt; $B/piano-baked-select events:$E:5:30 a.txt; $B/piano-baked-select events:$E:30:40 b.txt
python3 mvp/piano-baked/merge_importance.py mvp/piano-baked/baked_importance.hpp sweep.txt a.txt b.txt
$B/piano-baked-study --seconds 6 --ir-seconds 0.25,0.5,1.0 --profile
$B/piano-block-bench --events $E --from 52 --seconds 6 --quality live --block 128 --threads 0 --radiation baked
# step 2 (cd mvp/piano-baked for the python files)
$B/piano-baked-capture 60 76 cap/k60_v76          # also cap/k60_v76.iso.*
python3 fit_string.py cap/k60_v76 60 cap/k60_v76.fit --order 6 --report cap/k60_v76.json
$B/piano-baked-fit synth cap/k60_v76.fit cap/k60_v76.fit.wav 8; $B/piano-baked-fit force cap/k60_v76.force.f64 60 cap/k60_v76.floor.wav
python3 report_fit.py cap report.json; $B/piano-baked-fit cost 60 76; $B/piano-baked-fit fitcost cap/k60_v76.fit
$B/piano-baked-fit ab baked-ab-0.6.wav cap/k36_v76.fit cap/k60_v76.fit cap/k84_v76.fit      # ab3: with Full without sympathetic strings
```

### Tests (in ctest as `piano-baked-radiation`, plus the existing ones)

B1 error bound (Chopin 6 s in Full and nine single notes: all below -132 dB; asserts -80 dB, octave bands -60 dB); B2 baked output identical for
`sample()`, blocks 64/100/256 and 0..3 helper threads (Live 2 s, Full 0.5 s); B3 no heap allocation (global `operator new` hook) in 140 blocks
including radiation and quality switches; B4 Modal/Baked/Auto switched while seven notes sound (finite, no faults), Auto = Baked in Live and
Modal in Full; B5 bank structure (266 modes, 288 slots, tolerance 1e-6 keeps 261); B6 partitioned convolution against a direct one.
`piano-clap-playback` covers the new parameter (info, text, parse, 10-byte state round trip, 9-byte and 7-byte states load with Auto, a Live
render with Baked within -80 dB of Modal, Auto = Baked in Live). `piano-rt-*`, `piano-voices`, `piano-previous-bitexact` unchanged and passing.

### Honest limits

The baked bank is not "baked IRs": the measured result is that a reduced modal IIR beats convolution by a wide margin, and that only the 246
silent modes can be dropped. Radiation is 3-5 % of Full and about a third of Live cost, so Step 1 alone does not make Full run live. The drive
pass is L2-bound at 85 ports. Switching the setting cuts the old bank's tail. Step 2 fits one layer per file (no interpolation between velocity
layers, no re-strike on a sounding string, no pedal and no hammer-felt table beyond the 0.12 s force copy); the fitted voice has no board
feedback and wakes no sympathetic strings; the 8 s capture is fitted up to 7 s (the tail beyond is extrapolated); C2 forte shows the tension glide.
Fits exist only for the three keys and three layers. Float rotors drift slowly over
long holds (not measured beyond 8 s). Cost numbers are single runs on a shared desktop (min of three).

## Update 0.5.5: imperfections and a Voices setting (display name "Piano MVP 4 - Imperfections")

Listener feedback on 0.5.4: "the string sounds too perfect, almost unrealistic... a piano has three strings per note, are they all
100 % in tune?" and "long, overlapping pedalled chords get thinned". 0.5.5 adds (1) imperfections of the strings and the hammer, with an
**Imperfection** amount, and (2) a **Voices** setting with a better steal policy. With Imperfection 0 the model is **bit-identical to
0.5.4** (test `piano-previous-bitexact`: the Chopin excerpt, `--previous` = imperfection 0, against `chopin-prelude-0.5.4-raw.wav`).

### What is added (every number is an assumption, not a measurement)

All deviations are fixed per key by a hash of the key index (splitmix64), computed once in `prepare()`: no random state on the audio
thread, identical on every thread, block size and run. At amount `a` a key's strings are moved from the 0.5.4 values by `a` times the
change to the "amount 1" values, so amount 0 is exactly 0.5.4 and amount 2 extrapolates.

1. **Unison mistuning.** 0.5.4 detuned the strings of every key by the same symmetric 0.8-cent steps. Now each key (two or three strings,
   keys 33-108) gets its own offsets with zero mean and a total spread (max - min) `S = base(key) (0.6 + 0.8 u)`, `u` a per-key draw,
   `base` = 1.1 cents at key 33, 1.6 at key 60, 0.8 at key 84, 0.4 at key 108 (linear between), `S` clamped to 0.3-2.0 cents; the
   offsets inside the spread are random (not symmetric; the middle of three strings is anywhere). Real tuners leave unisons a fraction
   of a cent apart and tighten the treble, where the beating would be too fast. Slight mistuning is what gives the double decay of a
   real piano (a prompt sound, then a longer aftersound, Weinreich 1977, "Coupled piano strings", J. Acoust. Soc. Am. 62): strings in
   phase drive the bridge and lose energy quickly, the out-of-phase part decays slowly. Realised by multiplying every vertical and
   horizontal mode frequency of the string (omegaHat^2 by ratio^2, damping unchanged) at note-on (`TensionStruckNote::retuneString`,
   `SavSegment::setCoordinate`, no allocation, state kept). Keys 21-32 have one string and are not changed.
2. **Uneven hammer contact.** Per key and string: contact strength factor `1 + a d_s` (the strike weight of the felt contact `w_s`),
   `d_s` of zero mean with amplitude 5-15 % (largest deviation of the key), and a contact lag of 0 to `T` = 30-90 microseconds (the
   nearest string 0): string `s` is touched `lag_s` later than the first, implemented as a gap `lag_s x hammer speed` (metres) in the
   felt contact (`contact.hpp`: MVP-local copy of the engine's `HammerPotential`/`HammerFelt` with a per-string constant gap; gap 0 is
   the engine arithmetic, `piano-tension-energy` T4 still compares bit for bit with `StruckNote`). A constant gap does not change the
   gradient directions and the contact stays convex in the penetration, so the SAV scheme stays passive (test 0.5.5-T2: lossless
   energy drift 1e-13, lossy energy never rises, at amounts 1 and 2). The gap is set when a new hammer flight starts, not during a
   repetition.
3. **False strings (partial splitting).** 35 % of the multi-string keys (29 of 76 at amount 1) have 2-4 partials among 2-14 shifted
   on one string of the key by 0.3-2 Hz, either sign: that partial of that string then beats against the same partial of its
   unison neighbours at 0.3-2 Hz (non-uniform string or termination in a real piano). A cheap modification of the same retuning
   (the modal string is unchanged otherwise). The assumption: a real false string splits a mode into two close ones; here one
   string of the unison carries the shifted mode, which only works for keys with 2-3 strings (keys 21-32 are single wound strings
   and get no false strings in this version, although they are where false strings are strongest in a real piano).
4. **Runtime control.** `Piano::setImperfection(0..2)` (real-time safe, an atomic). A key takes the new amount at its next note-on
   (the retuning is applied then, only when the amount changed; sounding notes are never touched: test 0.5.5-T5). Default: plug-ins
   1.0 (CLAP parameter "Imperfection", id 3, continuous, automatable, saved in the state; LV2 port 7), offline renderers 1.0
   (`piano-midi-render --imperfection X`, `piano-render --imperfection X`); the library default is 0. `--previous` of
   `piano-midi-render` now means 0.5.4 (imperfection 0, per-key held-note count); `--v053` is the old meaning (0.5.3, bit-exact).
   Unstruck (sympathetic and leaky) strings are not retuned (documented limit); a ringing unstruck string handed over to a hammer
   keeps its perfect tuning for that note.

### Results

Plan for the A/B keys (amount 1; cents of the three strings, the 0.5.4 values were -0.8, 0, +0.8): C4 (60) +0.57 / -0.40 / -0.17
(spread 0.97), contact strengths +10 / -5 / -5 %, lags 55 / 19 / 0 us, four false partials; C5 (72) +0.65 / -1.01 / +0.37
(spread 1.66), strengths -1.5 / +7 / -5.6 %, lags 64 / 38 / 0 us, three false partials. Over the 76 multi-string keys: spread
0.35-2.0 cents, mean offset 1e-15, contact deviation amplitude 5.0-14.7 %, lag up to 89.8 us, false-partial shifts 0.31-1.97 Hz
(test 0.5.5-T1).

Two-stage decay of single notes (velocity 0.62, no pedal, Full, mono mix, 50 ms RMS envelope, `piano-imperfection-measure decay`,
`imperfection-decay-0.5.5.json`):

| note, amount | prompt slope 0.25-1.5 s (dB/s) | aftersound slope 3-9 s (dB/s) | level at 5 s / 9 s re 0.1 s (dB) | envelope roughness 1-9 s (dB rms) |
| --- | --- | --- | --- | --- |
| C4, 0 | -12.3 | -0.85 | -41.7 / -49.8 | 5.2 |
| C4, 1 | -13.2 | -2.56 | -49.2 / -54.1 | 4.5 |
| C5, 0 | -25.1 | -4.11 | -65.2 / -83.4 | 5.3 |
| C5, 1 | -23.5 | -2.82 | -54.5 / -64.5 | 1.9 |

Honest reading: the effect depends on the key's drawn offsets. C5 gains a clearly stronger and smoother aftersound (+11 dB at 5 s,
+19 dB at 9 s, less beating roughness); C4's drawn offsets (spread 0.97 cent against 1.6 cent in 0.5.4) make the aftersound weaker and
faster than 0.5.4's, i.e. a tighter unison. The 0.5.4 model already had a two-stage decay from its fixed 0.8-cent spacing and from
the shared board; 0.5.5 changes it per key rather than making it uniformly "more". Whether it sounds more real is for listening:
`imperfection-demo-0.5.5.wav` (C4 and C5, mf, held 4 s, each perfect then imperfect, then a six-note chord both ways) and
`chopin-prelude-0.5.5.wav` (+ `-raw`, `-audit.json`, `chopin-prelude-latest.wav`; `chopin-prelude-0.5.4*` is the same render at
amount 0). Chopin excerpt, Full: peak 0.1625 against 0.1581, RMS 0.01412 against 0.01421.

Cost: none measurable (retuning runs only when the amount changes, the contact lag and strengths are a few multiplications at
note-on). `piano-block-bench`, Live, one helper, i3-8100, 64-frame blocks (and 128), amount 0 / 1: pedalled 12-key cluster mean 0.27 /
0.25 deadlines (p99.9 0.72 / 0.84, max 2.05 / 2.03, 3 late blocks of 3750 in both, all in the first attack block); rolling chords from
key 43: mean 0.32 / 0.32 (p99.9 0.94 / 0.95, none late); Chopin 52-58 s: mean 0.21 / 0.21 (p99.9 0.65 / 0.65, none late). At 128: the
same within noise (cluster 0.25 / 0.25, chords 0.32 / 0.32, Chopin 0.20 / 0.20).

### Voices setting and steal policy

Complaint: long, overlapping, pedalled chords are thinned by Live's 16-voice cap. **Voices** (CLAP parameter id 4: Auto, 16, 32, 48,
64, stepped, saved in the state; LV2 port 8) sets the number of struck keys that can sound at once in Live and Balanced (Full always
88). Auto is the quality's own cap (Live 16, Balanced 24): the default is unchanged. The sympathetic and leaky caps scale with
`sqrt(N / auto voices)`, rounded up: Live 32 -> 32 voices, 17 sympathetic, 9 leaky; 48 -> 48/21/11; 64 -> 64/24/12 (Balanced 48 -> 48/23/12).
The adaptive load shedding of 0.5.4.1 takes the minimum with its level tables, so **the shed levels cap whatever Voices is set to**
(level 3 is 8 voices). Changing it is real-time safe (limits only; all 88 voices exist), sounding notes keep ringing.

Steal policy (shared board): 0.5.4 cut the **oldest** struck voice. Now the **quietest** one is chosen (decaying peak of its bridge
force, 100 ms time constant; voices struck less than 100 ms ago or with the hammer in flight are protected; ties and the all-protected
case fall back to the oldest) and it **fades by 80 dB over 8 ms** (the damper scaling of the string state, allocation free) before it is
retired; it stops counting against the limit at once. Test `piano-voices` (24 pedalled notes, 8 steals at 16 voices): the largest one-sample
step of the difference to the run that keeps every voice, inside the steal windows, is 0.0017 with the fade and 0.0035 with a 1-sample cut
(the signal's own largest step is 0.021); identical output for `sample()` and blocks with 0, 1 and 3 helper threads; a limit above the demand
changes nothing bit for bit. Honest limit: the quietest voice is by definition quiet, so a cut would not click much either; the fade halves
the step, and the rest of the difference comes from the missing board load of the removed string.

### Big-chords stress (i3-8100, one helper, Live, `piano-block-bench --bigchords`, same pattern as `piano-rt-stress --scenario bigchords`)

Pattern: a pedalled 10-note chord (keys 28-83, spread 5 octaves, velocity 96-120) every 1.5 s, each note held 5 s, so three to four chords
(30-40 strings) ring at once, 14 s. Deadline ratio = block time / block duration; misses = blocks over 1.0.

| block | Voices | adaptive off: mean / p99.9 / late | steals | adaptive on: mean / p99.9 / late | steals (shed level reached) |
| --- | --- | --- | --- | --- | --- |
| 64 | Auto (16) | 1.03 / 2.09 / 8262 of 10500 | 77 | 0.23 / 1.02 / 14 | 87 (3) |
| 64 | 32 | 1.51 / 5.66 / 8262 | 27 | 0.27 / 1.08 / 28 | 87 (3) |
| 64 | 48 | 1.59 / 2.53 / 9034 | 0 | 0.25 / 1.06 / 20 | 87 (3) |
| 64 | 64 | 1.57 / 2.56 / 9378 | 0 | 0.26 / 1.07 / 24 | 87 (3) |
| 128 | Auto | 1.03 / 2.11 / 4131 of 5250 | 77 | 0.23 / 0.99 / 5 | 87 (3) |
| 128 | 32 | 1.47 / 2.44 / 4162 | 27 | 0.27 / 1.06 / 13 | 87 (3) |
| 128 | 48 | 1.66 / 3.12 / 4335 | 0 | 0.28 / 1.06 / 15 | 87 (3) |
| 128 | 64 | 1.58 / 2.56 / 4689 | 0 | 0.32 / 1.14 / 20 | 87 (3) |
| 256 | Auto | 1.04 / 2.11 / 2066 of 2625 | 77 | 0.45 / 1.23 / 9 | 87 (3) |
| 256 | 32 | 1.63 / 3.24 / 2075 | 27 | 0.36 / 1.14 / 6 | 82 (3) |
| 256 | 48 | 1.63 / 2.93 / 2265 | 0 | 0.29 / 1.05 / 5 | 87 (3) |
| 256 | 64 | 1.61 / 2.53 / 2292 | 0 | 0.46 / 1.40 / 14 | 87 (3) |

Variant without the bass (keys below 48 lifted by 14 semitones, so no tension-modulated string; adaptive off): Auto mean 0.54 /
p99.9 1.17 / 57 late at 64 frames, 0.64 / 1.49 / 91 late at 128; Voices 32: 0.86 / 1.85 / 1349 late (64), 0.99 / 2.40 / 3177 late (128);
Voices 64: 1.42 / 14.3 / 7844 late (64), 1.02 / 2.94 / 3716 late (128). (CLAP harness `piano-rt-stress` at 128 on a loaded desktop gave
the same ordering with noisier absolute numbers; the cluster/chords scenarios of 0.5.4 are unchanged at 3.9x and 1.9x.)

Findings and recommendation: (1) **no Voices value is real-time safe for this scenario on this CPU**: even Auto misses most deadlines (mean
1.03), because five-octave pedalled chords with bass keys pay for tension modulation, woken sympathetic/leaky strings and hammers
at once; more voices cost roughly linearly more (mean 1.5-1.65 at 32-64) and never get below 1. The p99.9 <= 0.7 criterion for raising
the Live Auto cap is not met by any setting (without the bass only Auto at 64 frames is close, mean 0.54, p99.9 1.17), so **the Live Auto
cap stays 16**. (2) With adaptive shedding on (the plug-in default) the plug-in survives (mean 0.23-0.46, 5-28 late blocks of thousands) by
shedding to level 3 (8 voices, 1 sympathetic, 1 leaky, no tension modulation) within about a second; it therefore plays the thinner
chords the user complained about, whatever Voices is set to. (3) Voices 32-64 is for hosts with a larger block, a faster CPU, lighter or
higher-register playing (even without the bass, Voices 32 and 64 were not safe here), or Balanced (not measured with the big
chords); turn Adaptive off only if you accept dropouts. (4) For the full ringing of such chords use Full offline: `big-chords-0.5.5.wav`
(`piano-bigchords render`) plays the pattern in Live Voices 16 (77 steals), Live Voices 32 (27 steals) and Full (none), 14 s each.

### Tests (all in ctest)

`piano-imperfections` (0.5.5-T1 plan bounds, T2 passivity with retuned strings and uneven hammer at amounts 1 and 2, T3 `sample()`
= blocks = 0..3 helper threads for Live, Balanced and Full, T4 amount 0 bit-identical also after a detour through amount 1.7, T5 the
amount reaches a key only at its next note-on, T6 amount 1 differs from 0: relative rms 0.17-0.19); `piano-voices` (V1 limit
mapping, V2/V3 steal and fade, V4 determinism, V5 headroom changes nothing); `piano-previous-bitexact` (amount 0 against 0.5.4);
`piano-clap-playback` (five parameters; 9-byte state round trip; the 7-byte states of 0.5.4 and 0.5.4.1 still load with Imperfection 1
and Voices Auto); `piano-lv2-playback` (the reference engine uses the plug-in default amount). Not tested: the shed levels capping a
Voices setting (they take a minimum in `applyLimits`; shedding depends on timing).

### Limits

Amounts, spreads, strengths, lags and false-string counts are assumptions, not measured from a piano; a key's character is fixed by
its hash (no per-session or per-tuning variation, no slow drift of the tuning). Single-string bass keys are unchanged. Unstruck strings
are not detuned. The contact lag is a constant gap, not a model of string height or of hammer rotation. Voices above 16 do not run in real
time on this machine for dense bass chords (above).

## Update 0.5.4.1: hotfix, 64-frame blocks and load shedding (display name "Piano MVP 4 - Real time 0.5.4.1")

Two user reports against 0.5.4 (and 0.5.3): (1) "from the 2nd note, a considerable delay is added each strike"; (2) in Carla the CPU
goes to 100% with thousands of xruns, especially below C5, and Full "clips". The user's PipeWire runs at **64 frames, 48 kHz**
(1.33 ms deadline, rtprio limit 70); the 0.5.4 benches used 128/256.

### Findings (measured on the i3-8100, 4 cores, performance governor, 48 kHz, `piano-block-bench`)

* **The audio path adds no delay.** Per-note onset latency through the LV2 path (MIDI event frame to the first output frame that differs
  from a run without the note) is the physical hammer flight: 4.5-8.4 ms at velocity 80 for keys 40-64 (the same in the 0.5.3
  bundle), constant over strikes and independent of block size (test LAT-2). What the player hears as "added delay" is **late blocks**:
  (a) bass chords cost more than the 64-frame deadline (below), so every strike pushes the plug-in into xruns; (b) with the radiation
  helper (Worker threads 1) the audio thread waited at the end of a block for a helper that had been **descheduled while holding the
  radiation lock** (priority inversion: the helper is a normal thread when the host's real-time class cannot be copied, or competes with a
  busy desktop). In a paced 64-frame run with three busy loops on the other cores the block completion lag reached 18 ms with one
  helper against 4.7 ms with none. A synthetic 3 ms hold per helper batch made every block take 4.1 ms.
* **Bass cost at 64 frames.** A rolling 4-note chord every 150 ms with pedal, velocity 0.9, Live, one helper: keys from 28 cost a mean
  **1.84** deadlines per block (p99.9 2.96, 8783 of 9000 blocks late); from 36: mean 0.82 (p99.9 1.68, 299 late); from 48: mean 0.29
  (p99.9 1.63, max 4.1, 14 late); from 60: 0.25 (p99.9 0.58); from 72: 0.23 (0.41). Single notes are cheap (0.16-0.31, any key). The cost
  is the number of simultaneously simulated strings: tension modulation (SAV with two potentials every sample while the tension energy is
  above the floor; keys up to E2), pedal-woken sympathetic and leaky strings, up to 16 struck voices held by the pedal, and the hammer
  contact (about 5 times the sustained cost of a voice for the first 3 ms). Mode truncation and the tension floor change nothing here.
  A fixed cost of about 0.23 deadlines (board + radiation) is always present.
* **Denormals** are not the cause: the plug-in sets FTZ/DAZ in `run()` and the helper mirrors the audio thread's control word;
  runs with and without FTZ in the bench cost the same.
* **Clipping:** none. Peak output over all qualities: 0.38-0.50 for 10-14 note chords at velocity 1.0, 0.47 for a 32-key bass
  cluster, 0.84 for a 29-key treble cluster (Full). The Full "clipping" is the crackle of missed deadlines (Full runs at 0.3-0.5x real
  time). Output gain and Full's offline bits are unchanged.
* **A race in the idle path (found while reproducing).** 0.5.4 advanced `produced_` and `consumed_` with two stores in the idle, inline
  and Worker-threads-0 paths while the helper thread was alive. If the helper read `consumed_` before and `produced_` after, it radiated
  a stale ring slot (ports and drives of a lap-old record) into the live radiation state: ghost sound at a note-on after silence, output
  that differed from run to run (about half of the activations at 64 frames in a harness, also with Worker threads 0). Fixed
  (`Piano::publishSkipped`: both counters move under the radiation lock).

### Changes

1. **Stall fallback.** The audio thread times its wait at the end of a block; after more than 0.4 ms it moves the radiation to the audio
   thread for 1 s (doubling to 32 s on repeated stalls, back to 1 s after 20 s without one). Output is bit-identical either way
   (test LAT-3: a helper that sleeps 3 ms per batch gives the single-thread output; worst block 0.6 ms instead of 4.1 ms).
   `Piano::radiationStalls()`.
2. **Helper spin 4 ms -> 0.3 ms** before it polls every 100 us (with 64-frame blocks the old window kept it spinning, a core at 100%).
3. **Adaptive load shedding** (Live and Balanced, parameter "Adaptive load shedding", default on; CLAP parameter 2, LV2 port 6). The
   audio thread times every block. Smoothed cost above 0.7 of the deadline, or a block over 1.0, steps the level up (at most every 0.15 s):

   | level | struck voices | sympathetic | leaky | tension modulation up to key |
   | --- | --- | --- | --- | --- |
   | 0 (preset) | 16 / 24 | 12 / 16 | 6 / 8 | 40 / 47 |
   | 1 | 12 | 6 | 3 | 33 |
   | 2 | 10 | 3 | 2 | none |
   | 3 | 8 | 1 | 1 | none |

   (Live/Balanced take the smaller of the preset and the level; sounding notes above the tension limit stop modulating, the same
   transition as V_KC falling below the floor.) It steps back after 4 s below 0.2 of the deadline and at once after silence. The
   output then depends on timing, so it is a plug-in feature only: `Piano::setAdaptive`, off in renderers and tests (the LV2 and CLAP
   checks set the port/parameter to 0 for their bit-exact comparisons); Full never sheds.
4. Plug-in versions: LV2 `minorVersion 5`, `microVersion 5` (0.5.4.1), name "Piano MVP 4 - Real time 0.5.4.1", same URI; CLAP "0.5.4.1".
   CLAP state byte 6 holds "adaptive off" (old states load with it on).

### Results at 64 frames, 48 kHz (rolling 4-note chords every 150 ms, pedal, velocity 0.9, Live, one helper; `piano-block-bench`)

| lowest key | before: mean / p99.9 / late blocks | after (adaptive): mean / p99.9 / late blocks / level |
| --- | --- | --- |
| 28 | 1.84 / 2.96 / 8783 of 9000 | 0.33 / 1.16 / 49 / 3 |
| 36 | 0.82 / 1.68 / 299 | 0.26 / 1.04 / 21 / 3 |
| 48 | 0.29 / 1.63 / 14 | 0.29 / 0.84 / 0 / 0 |
| 60 | 0.25 / 0.58 / 0 | 0.26 / 0.59 / 0 / 0 |
| 72 | 0.23 / 0.41 / 0 | 0.23 / 0.40 / 0 / 0 |

At 128 frames the same chords: base 28 after 0.33 / 1.03 / 23 of 4500, base 36 0.28 / 0.95 / 2, base 48 and up clean. Chopin op. 28 no. 4
(0-40 s), Live, 64 frames: mean 0.20, p99.9 0.68, no miss with the helper (0.38 / 0.90 / 6 late without).

**Honest limit.** The p99.9 target of 0.7 holds from E3 (key 52) upward and for the Chopin prelude. Bass chords of four or more notes
under the pedal at 64 frames cannot be done at the Live model on this CPU: the hammer contact of each bass voice costs about 0.2
deadlines for 3 ms, so a four-note bass chord is a single burst of 0.8-1.0 deadlines on top of the 0.23 fixed cost. The governor
removes the sustained load (mean 0.3) and leaves roughly 0.5% late blocks on that worst case; 128 frames (p99.9 about 1.0, 2-23 late
blocks of 4500) or a faster core is needed for rolling four-note bass chords without any xrun. The next speed-ups are a fused
tension-SAV step (it is the generic `savStep`, not the fused flight step) and a FMA clone of the fast kernels.

### Tests

`piano-lv2-latency` (new, `lv2_latency.cpp`): LAT-1 threaded output == single-thread output for block patterns 64/128/256/1024 and a
variable one, gaps below and above the idle hold; LAT-2 onset latency per note <= 12 ms and constant over five strikes of one key, in
the LV2 path; LAT-3 a stalled helper (`Piano::testHelperHold`) does not delay the audio thread beyond the first stalled block and the
output is bit-identical. Note: block patterns differ from each other at the -200 dB level because the idle skip is evaluated at block
ends and the tension model amplifies it; only equal patterns are compared. `piano-block-bench` (new) is the 64-frame deadline bench
(`--events FILE` or `--keys`/`--chords BASE`, `--block`, `--fs`, `--quality`, `--threads`, `--adaptive`, `--o-NAME` overrides).
`piano-rt-stress` got `--adaptive`; `piano-host-check` expects three CLAP parameters. The Full offline output is unchanged
(`piano-previous-bitexact`, `piano-rt-equal-full` pass).

## Update 0.5.4: real time (display name "Piano MVP 4 - Real time")

Goal: sub-5 ms and sub-10 ms response in the plug-ins. The model adds no latency (events are
sample-accurate), so the response time is the host block size: **128 samples at 48 kHz = 2.7 ms, 256 = 5.3 ms**.
The obstacle was throughput: with the 0.5.3 plug-in settings (12 voices, sympathetic cap 8, leaky cap 4,
tension up to key 47) the pedalled 12-key cluster ran at 0.90x real time, the unpedalled one at 0.94x and the
Chopin excerpt at 1.57x on one thread of the i3-8100 (4 cores, 3.6 GHz). Now (Live, default, block 128, one helper
thread): **pedalled cluster 3.96x, unpedalled cluster 3.82x, Chopin excerpt 4.45x real time, no missed
deadline in the cluster runs**, and the full offline model (Full) is still **bit-identical to 0.5.3**.

### How to play with low latency

1. In Carla/JACK/Ardour set the block size to **128** (2.7 ms) or **256** (5.3 ms) at 48 kHz. The model has no look-ahead;
   the first block after a long silence is the most expensive one (the plug-in warms up when it is activated).
2. Leave **Quality = Live** (default) and **Worker threads = 1** (default). Live is for playing; it keeps up with
   fast repeated 12-note chords on this machine (tables below) and limits the model as listed.
3. For a bounce or a render choose **Full** (CLAP: the host's offline render mode selects it automatically; LV2: set
   Quality = Full, and hosts that raise the free-wheeling port get it automatically). Full is the complete offline model
   (88 voices, nothing capped, tension on every key); it runs at 0.3-0.9x real time here, so use it only offline.
   **Balanced** sits between (24 voices, tension to key 47, weaker savings): about 3x real time on the clusters and
   1.4-1.6x on the 12-note chord stress; use it at 256 samples or on a faster machine.
4. A larger block does not make Live more reliable, it only moves the deadline. If you hear clicks at 128, check that the
   host does not run other busy plug-ins on the same core, then try 256. The cost is bursty: the attack of a 12-note chord
   costs about three times the sustained cost for the length of the hammer contact (about 3 ms).
5. When nothing sounds the plug-in stops simulating (idle skip) and costs well under 1 percent of a core (about 240x real
   time in the harness); the next note-on wakes it in the same sample. After a note, the tail of woken strings can keep
   the plug-in busy for about 15 s (it is silent long before).

### Quality setting (parameter in CLAP and LV2)

| | Live (default) | Balanced | Full |
| --- | --- | --- | --- |
| struck voices | 16 | 24 | 88 (never stolen) |
| sympathetic (lifted-damper) strings | 12 | 16 | 88 |
| leaky (damped) strings | 6 | 8 | 88 |
| tension modulation, keys | 21-40 (to E2) | 21-47 | 21-108 |
| tension floor (J) below which the free update is used | 1e-10 | 1e-12 | 1e-14 |
| string modes dropped below (relative to the note's peak) | 1e-6 (-120 dB) | 1e-7 | never |
| unstruck string sleeps after force below / for | 1e-6 N / 0.5 s | 3e-7 / 0.75 s | 1e-7 / 1 s (static force counts, as 0.5.3) |
| dot products | reassociated (fast) | fast | sequential reference order |
| idle skip | yes | yes | no |

Changing the quality is real-time safe (the plug-ins prepare the model for Full in `prepare`; switching only changes limits and
flags: no allocation, no thread start). Sounding notes keep ringing; modes already dropped stay off until the key is struck again;
the tension model of a sounding note changes at its next note-on; a lower voice limit steals the oldest struck voices at the
following note-ons. CLAP: `clap.render` offline mode forces Full for its duration (the Quality parameter is ignored then). CLAP
parameters: `Quality` (0 Live, 1 Balanced, 2 Full; stepped, automatable), `Worker threads` (0-3, default 1); state is saved. LV2
control ports: 3 quality, 4 threads, 5 free-wheeling (optional, `lv2:freeWheeling`).

### Profile (before: 0.5.3 plug-in settings, one thread, cycles per sample at 3.6 GHz, rdtsc)

| part | pedalled 12-key cluster (76.6k, 0.90x) | Chopin 52-58 s (43k, 1.57x) |
| --- | --- | --- |
| struck strings: free modal update, three sequential force dot products, horizontal modes, a division per mode | 43.8k (57%) | 17.4k (40%) |
| radiating board modes: update of 512 modes and pressure sum | 11.2k (15%) | 10.7k (25%) |
| radiating board modes: drive of 512 modes by every active port | 10.5k (14%) | 6.4k (15%) |
| coupled board: force of every port into 128 modes, advance | 5.0k (6.5%) | 4.2k (10%) |
| port motion (128-mode dot product per port) | 2.9k (3.8%) | 2.6k (6%) |
| unstruck (sympathetic/leaky) strings | 3.2k (4.2%) | 1.7k (4%) |

The struck-string cost was mostly latency-bound sequential dot products and divisions, not arithmetic. The radiation (28-40%)
never feeds back into the strings.

### Design

1. **Exact kernel work (all qualities).** `kernels.cpp`: the difference form is elementwise and compiled twice (`target_clones`
   "avx2"/"default" on x86-64 Linux, dispatched at load time; the AVX2 clone does not enable FMA, so the bits equal the scalar
   code). The three sequential force dot products of a voice are interleaved (independent chains, each summed in index order);
   horizontal modes are skipped while their state is exactly zero (a point without perpendicular conductance never drives them);
   the `savStep` skip path is fused (`TensionStruckNote::tryBypass`) under the same preconditions as the engine's skip rule.
   Result: 0.9x to 1.6x on the pedalled cluster on one thread with **identical output bits**.
2. **Multi-core: a radiation pipeline, not a per-sample barrier.** The board's 512 radiating modes are driven by the port forces
   and never act back on the strings, so the exact coupling stays on the audio thread (strings and the 128 coupled board modes,
   per sample) while a helper thread consumes per-sample records (active ports and their bridge-mass-filtered drive) from a ring
   and produces the output. `Piano::blockBegin/blockStep/blockEnd` (used by both plug-ins and `piano-midi-render`; chunks of at
   most 256 frames) hide it; `sample()` keeps the one-sample interface. The radiation state has one writer at a time (a lock taken
   in batches of 8 records); at the end of a block the audio thread helps or waits, so a sleeping or late helper costs the audio
   thread only the radiation of that block. Helper threads are created in `prepare`/`activate`, joined in `deactivate`
   (`stopThreads`), spin for 4 ms after the last work, then poll every 100 us; they copy the audio thread's real-time scheduling
   class (one priority lower) and its FTZ/DAZ state. The audio thread never allocates, locks a mutex, waits on a condition
   variable or makes a system call (the LV2 `run()` allocation check runs with threads on).
   *Tried, not the default:* a per-sample fork/join of the voices over a fixed sticky partition (`WorkerPool`, Worker threads >= 2:
   tasks claimed with a stamped compare-and-swap, stolen after 2.8 us, late workers left out for a while). It is bit-identical too,
   but it never beat the pipeline on this machine (the voice phase moves about 20 cache lines of port data per sample) and it
   produced misses when a helper was preempted (columns "threads 3" in `realtime-report-0.5.4.json`); it stays for experiments.
3. **Determinism.** Results do not depend on the thread count or on which thread runs a task: every reduction has a fixed order
   (the radiation sum is the sequential mode-order sum; fast dot products use fixed accumulator counts). Tests: `piano-rt-behaviour`
   RT-1 (0..3 helpers, sample vs block interface; Live, Balanced, Full), `piano-rt-equal-threads/-full` (the CLAP plug-in at 0 vs N
   helpers), the LV2 check against a single-thread reference including quality and thread switches, and `piano-previous-bitexact`.
4. **Quiet-string savings (Live/Balanced).** (a) String modes whose force amplitude `|a_n| sqrt(q_n^2 + (u_n/(omega_n dt))^2)` falls below
   `truncation` times the note's peak bridge force are zeroed from the top down every 32 samples and skipped until the next strike
   (zeroing only removes energy). (b) Tension floor and tension keys (table). (c) Unstruck strings sleep earlier, and a string woken
   while the board was displaced is judged by its modal force only: its static term `-(T/L) w` never vanished in 0.5.3, so woken
   strings stayed awake for ever (18 simulated strings 55 s after a single note), kept the board and the CPU busy and left a
   constant DC drive; they now retire after about 15 s. (d) Idle skip: after 0.1 s without a simulated string, every 256 samples the
   board and radiation state is bounded; below 1e-9 (about -186 dBFS) frames are zeros until the next note-on.
5. **Fast kernels (Live/Balanced).** Eight-accumulator dot products (port motion, bridge force, tension sum), a division-free free
   update, a cached force dot product and a fused hammer-flight SAV step (`TensionStruckNote::flightFast`, ranks 1-4, the same
   equations as `pme::nl::savStep`, three passes) replace the sequential versions. Agreement with the sequential versions: 292 dB below
   the signal (rounding level) for the dot products and 285 dB for the flight step (chords). Threaded and single-thread output remain
   bit-identical for the same settings; vs the exact path the difference is documented here, not bit-identical.
6. **Repeated-note bug.** `Piano::noteOff` released the key at the first note-off of overlapping same-key notes; it now counts held
   note-ons per key (bounded `uint8`, no allocation) and releases when the count reaches zero (CC123 clears all). Applied in
   `dispatchMidi` (CLAP, LV2, renderers). `setHeldNoteCount(false)` and `--previous` keep the 0.5.3 flag. Test RT-3. The full-piece
   converter workaround is unchanged (it gives identical results).

### Measured differences from Full (Chopin excerpt 40-64 s, whole piece simulated)

Signal rms -36.6 dBFS, peak -16.0 dBFS; octave bands 62 Hz ... 16 kHz of the mono sum.

| change (against Full) | band energy difference, dB | total energy | waveform difference |
| --- | --- | --- | --- |
| fast kernels only | 0.00 in all bands | 0.00 dB | max -276 dBFS (rounding) |
| fast + mode truncation 1e-6 | within 0.01 | 0.00 dB | rms -78.7 dBFS, max -55 dBFS |
| ... + sleep 1e-6/0.5 s + floor 1e-10 + idle skip | within 0.01 | 0.00 dB | rms -78.6 dBFS, max -56 dBFS |
| Live (everything, incl. caps 16/12/6, tension to key 40) | -0.2 +0.4 -0.6 +0.6 -1.9 -0.3 +0.1 +0.1 | +0.66 dB | decorrelated |
| Balanced | -0.3 +0.3 -0.5 +0.5 -1.8 -0.3 +0.1 0.0 | +0.64 dB | decorrelated |

The truncation threshold alone is not what limits the waveform difference. Without tension in the reference (cluster, tension off) the
rms difference is -63 / -76 / -86 dBFS for thresholds 1e-3 / 1e-4 / 1e-5 (SNR 35 / 48 / 58 dB). With tension on every key in the reference a
perturbation as small as 1e-6 already gives -79 dBFS rms, because the tension model amplifies small differences, so waveform SNR is not
a meaningful measure for Live vs Full (the dropped modes themselves are more than 120 dB below their note's peak); octave-band energy is.
Live differs audibly from Full only in what the table states: at most 16 simultaneous struck keys, fewer woken strings,
tension modulation only up to E2. On this excerpt the 1-2 kHz octave is 1.9 dB lower in Live.

### Real-time results (`piano-rt-stress`, the CLAP plug-in, 48 kHz)

Method: back-to-back `process()` calls at 128 and 256 frames; every block is timed against its deadline (block/fs); the first 16 blocks
after activation are warm-up (counted in the real-time factor, not as misses); the audio thread asks for SCHED_FIFO 50 (helpers mirror
it). Machine: i3-8100 (4 cores, no SMT, 3.6 GHz, Linux) with the usual desktop load. `realtime-report-0.5.4.json` has every row. "x" is
the real-time factor, "p99.9" the 99.9th percentile of block time / deadline (1.0 = the deadline), misses = blocks later than the deadline.

Scenarios: *cluster* keys 48-59 velocity 120 with the sustain pedal (the worst case asked for); *cluster, no pedal*; *chords* a 12-note
chord every 75 ms with pedal (attack bursts); *repeat* the same every 60 ms; *Chopin excerpt* 24 s.

**Live**

| scenario | block | 0 helpers: x real time / misses / p99.9 | 1 helper (default): x / misses / p99.9 |
| --- | --- | --- | --- |
| cluster, pedal | 128 | 2.09 / 0 / 0.60 | 3.96 / 0 / 0.29 |
| cluster, pedal | 256 | 2.10 / 0 / 0.51 | 4.03 / 0 / 0.28 |
| cluster, no pedal | 128 | 1.96 / 0 / 0.55 | 3.82 / 0 / 0.29 |
| cluster, no pedal | 256 | 1.97 / 0 / 0.57 | 3.75 / 0 / 0.29 |
| chords (pedal) | 128 | 1.69 / 88 / 1.23 | 2.97 / 0 / 0.98 |
| chords (pedal) | 256 | 1.62 / 40 / 3.27 | 3.10 / 0 / 0.85 |
| repeat (pedal) | 128 | 1.71 / 82 / 1.19 | 3.26 / 0 / 0.91 |
| repeat (pedal) | 256 | 1.76 / 20 / 1.11 | 3.25 / 0 / 0.86 |
| Chopin excerpt | 128 | 2.34 / 10 / 1.01 | 4.45 / 0 / 0.76 |
| Chopin excerpt | 256 | 2.37 / 3 / 0.98 | 4.42 / 1 / 0.74 |

**Balanced**

| scenario | block | 0 helpers: x real time / misses / p99.9 | 1 helper (default): x / misses / p99.9 |
| --- | --- | --- | --- |
| cluster, pedal | 128 | 1.75 / 0 / 0.59 | 3.11 / 0 / 0.35 |
| cluster, pedal | 256 | 1.76 / 0 / 0.59 | 3.11 / 2 / 6.25 |
| cluster, no pedal | 128 | 1.73 / 0 / 0.61 | 3.02 / 0 / 0.35 |
| cluster, no pedal | 256 | 1.68 / 0 / 0.62 | 2.98 / 0 / 0.36 |
| chords (pedal) | 128 | 0.95 / 1066 / 1.75 | 1.44 / 94 / 1.39 |
| chords (pedal) | 256 | 0.97 / 138 / 10.79 | 1.44 / 47 / 1.26 |
| repeat (pedal) | 128 | 1.08 / 135 / 1.64 | 1.57 / 66 / 1.37 |
| repeat (pedal) | 256 | 1.06 / 144 / 1.53 | 1.65 / 22 / 1.16 |
| Chopin excerpt | 128 | 1.97 / 3 / 1.09 | 3.18 / 0 / 0.87 |
| Chopin excerpt | 256 | 1.94 / 1 / 1.01 | 3.13 / 1 / 4.23 |

**Full** (offline model, 1 helper): cluster 0.48x, unpedalled 0.49x, chords 0.34x, Chopin excerpt 0.94x (0.41 / 0.42 / 0.29 / 0.69x without a
helper); not a real-time setting. `piano-midi-render` (offline Chopin excerpt, 26 s): 123 s in 0.5.3, about 82 s in 0.5.4 with one helper
(timed while other jobs ran), output bit-identical.

Honest limits. (1) With the helper, the audio thread occasionally waits for it at the end of a block; if the helper is descheduled while it
holds the lock, the block is late. In the tables this appears as isolated misses of a few milliseconds (rows with a non-zero miss count, for
example 1 of 9000 blocks on the Chopin excerpt), and the same stalls appear in a plain spin-loop test of this machine (`chrt -f 60`, one thread:
six gaps of up to 17 ms in 20 s), so part of it is the machine, not the code. With 0 helpers the clusters pass (about 2x, p99.9 0.55-0.6) but
the chord stresses and the last chords of the excerpt miss (1.2-1.7x). (2) Burst cost: a block containing 12 hammer contacts costs up to 0.98
of its deadline at 128 on this machine (chords, p99.9), so 128 is at the edge for fast 12-note chords; 256 has about 0.85. (3) Worker threads
2 and 3 (per-sample voice helpers) are not recommended (misses). (4) The first note after a long silence meets a cold helper; activation
renders a warm-up burst, but one 3-8 ms outlier at that note was still seen in some runs.

### Verification

`cmake --build out/piano-mvp-v4 -j8 && ctest --test-dir out/piano-mvp-v4 --output-on-failure` (13 tests): the 7 existing ones, plus
`piano-rt-behaviour` (RT-1 thread/block identity, RT-3 repeated note, RT-4 idle skip incl. cost and wake-up, RT-5 quality switch while
sounding, RT-6 thread stop/start), `piano-previous-bitexact` (`--previous` renders the committed `chopin-prelude-0.5.3-raw.wav` byte for
byte), `piano-rt-equal-threads`, `piano-rt-equal-full`, `piano-rt-idle` (>= 150x real time when idle), `piano-rt-deadlines` (a report and a
clean shutdown), and the extended LV2/CLAP checks (quality, thread and free-wheeling ports, parameters, render mode, state, run() allocation
count 0 with threads on). `piano-rt-stress PLUGIN.clap --scenario S --block N --quality Q --threads T --seconds D [--events FILE] [--rt]
[--json] [--expect-equal] [--min-rtf X]` is the deadline harness; `piano-hash-bench` prints an FNV hash and the real-time factor of a
scenario at the Piano level (`--o-NAME VALUE` overrides preset fields).

### Flags

`piano-midi-render EVENTS OUT.wav [--previous|--v052|--v051|...] [--threads N] [--quality live|balanced|full] [--full]`. **`--previous` is
0.5.3** (bit-for-bit, verified against the committed raw render); the former `--previous` (0.5.2, no leaky dampers) is now `--v052`; the
default is the 0.5.4 offline model (Full), which equals 0.5.3 on the excerpt (`chopin-prelude-0.5.4-raw.wav` is byte-identical to
`chopin-prelude-0.5.3-raw.wav`). Same renaming in `piano-render` (`previous` = 0.5.3, `v052`) and `piano-sympathetic-demo`.

### For 0.5.5 (stereo width)

CPU headroom in Live at block 128 with one helper: pedalled cluster 3.96x, Chopin excerpt 4.45x. The radiation stage costs about 5 us per
sample (24% of the budget) on the helper; a per-key radiation decorrelation or microphone model placed after the board adds work only to that
stage, which can take roughly twice as much before it limits the real-time factor, and the audio thread is unaffected. The radiation never feeds
back into the strings, so a linear post-board stage has no coupling constraints.

## Update 0.5.3: leaky dampers

Listener feedback on 0.5.2: "almost perfect; unsustained strings feel rather muffled; the box
feels small; with the pedal down almost perfect". Without the pedal a struck note excited only its
own strings, because damped strings were never simulated; with the pedal all strings ring through
the shared board. A felt damper lowers a string's Q, it does not remove the string from the system.
`setLeakyDampers(bool)` (before `prepare`, default on) simulates damped, unstruck strings as
sympathetic strings with an added damper loss. Off is 0.5.2 bit-for-bit: `piano-midi-render ...
--previous` is now **0.5.2** and reproduces the committed `chopin-prelude-0.5.2-raw.wav` byte for
byte (verified with `cmp`, also the 0.5.2 listening WAV); `--v051` is the former `--previous`
(0.5.1); `--v050`, `--shared-pan`, `--v049`, `--v048`, `--v046`, `--legacy` unchanged.
`piano-render` takes `previous` (0.5.2), `v051`, `v050`; `piano-sympathetic-demo` takes `--previous`
(0.5.2), `--v051`, `--v050`.

### How to listen

`unpedalled-demo-0.5.3.wav` (no CC64 anywhere, identical gain, 9 s each, 1.5 s gaps): staccato notes
in three registers, a legato line, a six-note chord; played (1) 0.5.2, (2) 0.5.3, (3) 0.5.3 with the
0.4.9 bridge-top mass switched off (diagnostic only; defaults unchanged). Listen for: does (2) sound
less muffled / bigger than (1)? Does (3) sound better than (2) (then the 55 g mass is the cause)?
Does a released note now ring on too long (see "Side effect")? Also `chopin-prelude-0.5.3.wav`
(`-latest`), `C4-reference-0.5.2-0.5.3.wav` (reference, 0.5.2, 0.5.3 at equal initial RMS).

### Design

1. **Damper law.** `sigma_d(f) = rate (f / 200 Hz)^(-exponent)`, an added amplitude decay rate (1/s)
   per vertical mode; each step multiplies the mode's `q` and `u` (SPEC-OSC difference form) by
   `exp(-sigma_d/fs) <= 1`, so it only removes energy. `setDamperLaw(rate, exponent)`; fitted
   defaults **rate = 2.0 /s, exponent = 0** (one parameter). No literature value for a felt damper's
   modal loss on a piano string was found that could be used directly, so this is an effective,
   fitted parameter (see Fit), not a measured felt.
2. **Machinery.** Reuses the 0.5.1 wake/sleep sympathetic strings (`SympatheticString`, vertical
   modes below 2 kHz, start from rest). A damped idle string wakes when its board bridge-point
   speed exceeds 1e-4 m/s (as lifted ones) and sleeps after 1 s of negligible force. Two pools with
   separate caps: lifted strings first (`setSympatheticLimit`), then damped strings
   (`setLeakyLimit`); within a pool, ranking by harmonic coincidence with the loudest struck strings
   when candidates exceed the cap. A woken damped string uses the leaky factors instead of the old
   `exp(-35/fs)` step and is not retired by the release counter. Deterministic, no allocation in the
   audio path (factors are tables built in `prepare`).
3. **Released notes: kept separate.** `setLeakyRelease(bool)` (default **off**) applies the same law
   to a struck key after note-off instead of `exp(-35/fs)`. The fitted 2/s is 17x weaker than the
   35/s release step; used for release it changes the audible decay little (the board and the
   woken neighbours dominate the tail, measured below), but the physical meaning differs (35/s is a
   fast felt stop, 2/s is an effective leakage that absorbs model coupling error), so it stays a
   switch. `piano-render --leaky-release` renders it.

### Fit

Late window (0.5-1.5 s) octave shares, model minus Salamander, dB, bands 62.5-125, 125-250,
250-500 Hz, nine notes (A2/C4/C6 at v40/60/92); mean and rms over the 27 values. Flat law:

| rate (1/s) | mean | rms |
| --- | --- | --- |
| 0.5 | +3.1 | 6.8 |
| 1 | +2.1 | 6.2 |
| 1.5 | +1.0 | 5.9 |
| **2** | **-0.2** | **5.9** |
| 3 | -2.5 | 6.8 |
| 5 | -6.9 | 10.6 |
| 10 | -16.6 | 24.3 |
| 35 | -21.5 | 32.0 |
| 0.5.2 (no damped strings) | -18.3 | 29.1 |

A second parameter was tried (sigma rising or falling with frequency): exponent -0.5 at 2/s gives
rms 5.0 (mean +2.5), -1 at 2.5/s gives 5.8, +1 at 1.5/s gives 10.4. The gain (5.9 to 5.0) is within
what nine points can resolve, so the law stays flat. Residuals at rate 2 (62-125 / 125-250 /
250-500, v60): A2 +0.4 / -0.5 / +1.8; C4 -5.7 / +10.8 / +5.1; C6 -8.5 / -7.5 / -0.4. The 62-125 Hz
band of C4 stays 6-13 dB low and its 125-250 band 7-14 dB high: **which** bass strings ring is not
fixed by one number. **Caveat:** the fitted 2/s is far below a physical felt damper (T60 of a few
tenths of a second means 20-35/s). With strong damping the strings are quiet and the 62-500 Hz
deficit remains (35/s: C4 62-125 -41 dB); with weak damping they approach the all-dampers-lifted
case. The fit therefore says "the model's board-string coupling is stronger than the real one
unless the damper is almost absent", i.e. 2/s absorbs model error as well as damper physics.
The response is a cliff between 2 and 10 /s, not a smooth slope.

Absolute late power for C4 v60 (dB re full scale, bands from 62.5 Hz): reference -74.7, -81.7,
-46.5, -43.5; 0.5.2 -109, -126, -55, -75; 0.5.3 -97.5, -88.0, -58.5, -75.8. So the low bands
come up by 12 and 38 dB; the 250-500 band of C4 is 4 dB lower (loaded by the extra strings).

### Band tables, v60 (model minus reference, dB, band lower edges 62.5 ... 8000 Hz; 0.5.2 / 0.5.3)

```
A2 attack  -7.5/-4.2 +3.7/+2.7 +0.6/+0.8 -3.5/-0.6 -9.8/-6.1 +2.6/+5.3 +23.8/+26.4 -14.3/-11.7
A2 late    -6.8/+0.4 +9.9/-0.5 +0.3/+1.8 -17.8/-10.3 -13.0/-6.8 -5.1/+0.3 +12.2/+16.9 -25.1/-20.0
C4 attack  -3.1/-1.1 +12.0/+14.9 +1.0/+2.6 -1.1/-6.5 -1.4/-0.4 +4.2/+6.5 +12.4/+14.5 -0.6/+1.6
C4 late    -21.3/-5.7 -31.3/+10.8 +5.2/+5.1 -17.4/-15.1 -11.0/-7.7 -7.1/-3.4 -3.5/+0.3 -13.5/-9.9
C6 attack  -4.0/-15.3 -5.7/-1.4 -11.7/-5.7 -10.6/+0.6 +0.2/+0.2 -2.2/-2.5 +9.5/+9.1 -9.6/-10.1
C6 late    -16.7/-8.5 -41.6/-7.5 -63.1/-0.4 -52.7/+8.3 +0.4/+0.3 -13.0/-18.3 +1.3/-4.1 -14.0/-19.3
```

All nine notes and v40/v92 are in `reference-calibration-0.5.3.json`. Mean attack high-band error
(energy share above 2 kHz): 0.4.9 5.68 dB, 0.5.1 3.80, 0.5.2 3.83, **0.5.3 4.94**: the extra strings
add load to the board during the attack and the high share moves (A2 v60 4.13 to 5.85, C4 v60
3.72 to 5.92). This is a cost of the change (it is not the aim); the attack window includes the
woken strings of the same note's neighbours.

### Muffled attribution: the bridge-top mass

Variant (3) of the demo, and `calibration-*-v60-0.5.3-nobridgemass.wav` (`piano-render ...
--no-bridge-mass`), v60 shares, model minus reference, dB (bands 62.5 ... 8000 Hz), 0.5.3 with
mass / without:

```
A2 attack  -4.2/-4.3 +2.7/+2.6 +0.8/+0.9 -0.6/-0.3 -6.1/-4.6 +5.3/+9.4 +26.4/+34.0 -11.7/+1.0
A2 late    +0.4/+0.4 -0.5/-0.5 +1.8/+1.9 -10.3/-9.8 -6.8/-5.1 +0.3/+4.1 +16.9/+24.7 -20.0/-7.9
C4 attack  -1.1/-1.3 +14.9/+14.7 +2.6/+2.4 -6.5/-6.4 -0.4/+1.4 +6.5/+11.2 +14.5/+22.4 +1.6/+13.8
C4 late    -5.7/-5.8 +10.8/+10.7 +5.1/+5.1 -15.1/-14.9 -7.7/-6.1 -3.4/+0.6 +0.3/+8.4 -9.9/+2.4
C6 attack  -15.3/-16.5 -1.4/-2.6 -5.7/-6.7 +0.6/+0.2 +0.2/+0.0 -2.5/+1.6 +9.1/+17.0 -10.1/+2.1
C6 late    -8.5/-9.6 -8.5... 
```

Removing the mass raises the 2-4 kHz shares by about +8 dB in every note and the 4-8 kHz shares by
+12 to +13 dB (1-2 kHz by +4), so the model is +17 to +34 dB above the reference in the 2-4 kHz share
(with the mass: +9 to +26), while leaving the sub-500 Hz shares unchanged. In this share metric the
55 g mass is **not** over-darkening: the model's 2-4 kHz share is already above the reference. Shares
are relative, though; whether the ear wants more 4-8 kHz air is for the listener to judge on
variant (3).

### Side effect: released notes ring on

Energy after note-off, dB per 0.25 s block relative to the first block, 0.5.2 vs 0.5.3 (default
35/s release; woken neighbours leak): C4 `0 -19 -22 -25 -28` (0.5.2) vs `0 -5 -9 -13 -20`
(0.5.3); A2 `0 -19 -23 -26` vs `0 -9 -11 -16`; A1 `0 -24 -30 -34` vs `0 -4 -11 -14`; C5 (72)
`0 -8 -11 -14` vs `0 -8 -16 -21`; C6 `0 -12 -16 -19` vs `0 -14 -23 -27`. In the bass and middle
a released note now stops about 14-20 dB less abruptly in the first 0.25 s (neighbours woken
by the note keep ringing through the board at 2/s), while the treble stops faster than before
(its undamped neighbours were already ringing). `--leaky-release` changes this tail by at most
a few dB (C4: `0 -4 -8 -14 -23`). If "stopping" sounds smeared compared to a real piano, the
rate is too low for notes that have been released; a time-dependent law (strong damping that
relaxes) is the next thing to try.

### "Box feels small" (investigation only, `box_analysis.py`, `box-analysis-0.5.3.json`)

Absolute band power (dB re full scale) and stereo statistics, Salamander vs model, 10-250 ms attack
window, v60:

| | 62-125 Hz | 125-250 | 250-500 | coherence L/R, 125-2000 Hz |
| --- | --- | --- | --- | --- |
| C4 reference | -55.0 | -54.9 | -26.3 | 0.42-0.55 |
| C4 0.5.2 / 0.5.3 | -77.6 / -77.7 | -62.4 / -61.6 | -44.7 / -45.3 | 0.80-0.88 / 0.57-0.71 |
| A2 v92 reference | -26.2 | -27.6 | -30.8 | 0.50-0.65 |
| A2 v92 0.5.2 / 0.5.3 | -50.2 / -49.6 | -40.2 / -43.8 | -46.9 / -49.3 | 0.86-0.87 / 0.65-0.78 |

Findings: (a) relative to the strongest band the model's 62-125 Hz attack content is 29 dB (ref) vs
32 dB (model) down: about 3 dB of "knock" missing, not the big deficit; the overall model level
is 15-20 dB below the close-miked reference, which is a level, not a shape, difference. (b) **Width:**
the reference left/right coherence is 0.4-0.6 over 125 Hz-2 kHz; the model is 0.85-0.9 (nearly
mono-like), side/mid ratios are lower. 0.5.3 reduces it to 0.5-0.7 in 125-1000 Hz because the woken
strings sit at other bridge points, but above 2 kHz it stays 0.84-0.91. A single pan per key and a
mono-ish shared radiation are the likely source: a board that is "small" in the stereo sense. The next
iteration may target decorrelation in the radiation stage (different radiating-mode weights per
channel / more distinct left and right pickup points). No change made now.

### Tests (`ctest`: 7/7)

- `piano-behaviour`: existing tests (0.5.1 expectations run with `setLeakyDampers(false)`), plus
  0.5.3: damped string's board-force energy 1-3 s is below 0.25 of a lifted one but above zero, and
  exactly zero with leaky off; leaky limit 0 wakes nothing; determinism without pedal; damped pool
  never above its cap (lifted pool off); setters rejected after `prepare` and range-checked; law
  values (flat and power); both release laws decay.
- `bridge-energy`: new passivity test: the 7 unexcited strings of the 8-string shared board are
  damped each step with the leaky factors; energy never exceeds the initial (lossless and lossy
  board), final below initial.
- Unchanged and passing: tension energy, hammer contact, CLAP and LV2 playback (LV2 `run()`
  allocation check included, plug-in caps set).
`piano-behaviour` now takes about 2.5 minutes (timeout raised to 400 s): the 88-key chord tests
wake many strings.

### CPU (single thread, -O3, no SIMD; plug-in settings: 12 voices, sympathetic cap 8, tension keys <= 47)

| Case | 0.5.2 (leaky off) | 0.5.3 leaky cap 4 | cap 8 | uncapped |
| --- | --- | --- | --- | --- |
| Pedalled 12-key cluster 48-59 | 0.97x | 0.96x | | |
| Unpedalled 12-key cluster | 0.92x | 0.86x | | 0.57x |
| Plug-in Chopin 52-58 s | 1.73x | 1.58x | 1.47x | 0.73x |
| Offline Chopin excerpt (66 s simulated, 88 voices) | 115-125 s | 125 s | | |

Plug-in default `pluginLeakyLimit = 4`: pedalled worst case unchanged (the damped pool is idle
under the pedal), unpedalled cluster 6% slower, Chopin 9% slower. Tool: `piano-tension-bench
chopin|cluster|cluster-nopedal events maxkey [off|N]`. No threading or SIMD yet.

### Setters and tools

`setLeakyDampers`, `setDamperLaw(rate, exponent)`, `setLeakyLimit`, `setLeakyRelease`; constants
`damperRefHz`, `defaultDamperRate`, `pluginLeakyLimit`. `piano-render` developer options
`--damper-rate`, `--damper-exp`, `--leaky-limit`, `--leaky-release`, `--no-bridge-mass`.
`piano-unpedalled-demo out.wav` makes the listening demo.

### Full-piece render

`render_full_piece.py` renders the complete pinned Chopin Op. 28 No. 4 (same MIDI, same SHA-256,
same tempo map, velocities and CC64 as the 26 s excerpt) with the unchanged 0.5.3 model, 88-voice
limit and the same fixed +12.04 dB listening gain: from frame 0 to the last event, plus a 4 s natural
release tail, with a fade only over the final 50 ms. `piano-midi-render ... --full` selects this range
(the default 40-64 s excerpt behaviour is unchanged); the script first re-runs `prepare_classical.py
--check`, so the source, events and audit are verified before rendering.

```bash
/tmp/piano-midi-venv/bin/python mvp/piano-v4/render_full_piece.py out/piano-mvp-v4
```

Outputs: `chopin-prelude-full-0.5.3.wav` (listening), `-raw.wav`, `-audit.json`, the converted
`chopin-prelude-full-0.5.3.events`, and `chopin-prelude-full-latest.wav`. If the fixed gain ever
clipped, the script would write a separate `-safe.wav` at +6.02 dB and leave the gain unchanged.

**Overlapping same-key notes.** The source has three note-ons for a key that is still sounding
(all on key 52, E3, at 70.127 s, 73.975 s and 81.784 s; the first note's off follows each by 7-8 ms, so the three overlaps are short legato joins). A second note-on re-strikes the key's strings, exactly as on a
piano and as `Piano::noteOn` does. `Piano::noteOff` only clears a flag, so the first note-off would
release the second note too. The converter therefore keeps a per-key count of sounding notes and drops
every note-off except the one that ends the last overlapping note; note-ons, velocities, timing and
pedal are untouched, so 604 note-ons and 601 note-offs are rendered. The dropped note-offs are recorded in
the audit. Result: 105.77 s, raw peak 0.158 / RMS 0.0107, listening peak 0.632 (no clipping at +12.04 dB,
no `-safe` copy needed), 0 voice steals (88 voices), 0 numerical faults, render 204 s (0.52x realtime;
the offline 88-voice model is slower than realtime).

## Update 0.5.2: tension modulation of the struck strings

Physical "soft distortion": large transverse motion stretches a string, raising its tension, hence
a small pitch rise at the strike (glide as the amplitude decays) and coupling between partials
(phantom partials, bass growl). Strings so far were linear. `setTensionModulation(bool)` (before
`prepare`, default on; off is 0.5.1 bit-for-bit). `piano-midi-render ... --previous` is now
**0.5.1** and reproduces the committed `chopin-prelude-0.5.1-raw.wav` byte-for-byte (verified with
`cmp`, also the 0.5.1 listening WAV); `--v050` is the former `--previous` (0.5.0); `--shared-pan`,
`--v049`, `--v048`, `--v046`, `--legacy` unchanged. `piano-render` takes `previous` (0.5.1) and
`v050`; `piano-sympathetic-demo` takes `--previous` (0.5.1) and `--v050`.

### Design

1. **`TensionStruckNote` (`tension_note.*`).** Copy-and-adapt of `pme::exciters::StruckNote` (the
   engine is untouched). Same SPEC-EXEC order, hammer, bridge port, felt and release rules; the SAV
   segment (vertical modes of every struck string plus the hammer) gets a second potential
   `V_KC = sum_s c_s S_s^2 / 4`, `S_s = sum_n k_n^2 q_s,n^2`, `c_s = (EA - T_s)/(2 L)` (SPEC-GEO
   "Tension modulation"), which adds to the hammer potential and shares the one psi (SPEC-NL "SAV
   formulation", rank one). It is the formula of `pme::nl::TensionModulationPotential`, re-coded as
   a vertical-only potential per string slice because the core class addresses `[V modes, H
   modes, lumped]` of one string and cannot address one slice of a stacked 1-3 string segment; test
   0.5.2-T1 checks value and gradient against the core class (relative difference 0). Each unison
   string of a key has its own `S_s` (strings are separate); the bridge reaction, hammer, damper law
   and the 0.5.1 shared board are unchanged.
2. **Consequences of the second potential.** The tension potential is non-zero whenever the string
   moves, so the SAV solve runs every sample while a struck string sounds (the skip rule no longer
   saves work); the felt dashpots can no longer be merged into the SAV column (gradient not parallel
   to the felt direction), so during contact the solve is rank 1 + dashpots (<= 4). A floor of
   1e-14 J (1% of eps0, DeltaT < ~2e-5 N, pitch effect < 1e-7) treats `V_KC` as zero so a decayed
   string returns to the cheap path (energy tests set the floor to 0). The damper law scales the
   string state outside the SAV, so after each damped sample `clampPsi()` lowers psi to
   `sqrt(2(V + eps0))` if above (only removes energy; the same rule as SPEC-NL Drift control 2).
3. **EA.** The core's axial stiffness `E_c pi d_c^2/4` (`axialStiffness`, SPEC-GEO) with the
   preset's `E_c = 2.02e11 Pa` and the report's string diameter (Chabassier-Durufle RT-0425
   appendix; for wound bass strings that diameter is the core, the winding is homogenised only in
   density, so it adds no axial stiffness: "core-only EA"). A1: EA = 3.5e5 N for T = 1722 N
   (key 24), A2: 1.8e5 N, T = 754 N, C4: 1.6e5 N, T = 741 N, C6: 1.3e5 N, T = 697 N. Nominal values
   from the report, no measurement of this piano.
4. **Polarisation.** Vertical only: the horizontal modes have no port on this board (no
   perpendicular bridge conductance), stay linear and are driven by the bridge term only, so
   `y_x^2 + z_x^2` is reduced to `y_x^2`. The tension acts on the string only; the extra tension's
   own force on the bridge (kappa_T DeltaT, SPEC-GEO hook) is not transmitted (kappa_T = 0), as in
   the core's own default; the core notes that the movable-bridge energy balance is then not exact
   (SI-0015 issue). **Sympathetic (unstruck) strings stay linear**: their amplitude is 40-60 dB
   below a struck string (DeltaT ~ amplitude^2), so the effect is below 1e-6 relative.
5. **Longitudinal modes: not included.** Phantom partials in real pianos come mainly from
   longitudinal string waves (`GeometricSegment`, A7-R2/R3: ~5 longitudinal modes at 48 kHz, an
   O(N) pair-sum per mode per sample plus their own bridge port). They would need the vertical
   bridge-force path (kappa_L) and a longitudinal board port. Cost: a few more passes per
   string group per sample on top of the tension step (not measured); feasible for the bass keys the
   plug-in already covers, not tried.

### Tests (`ctest`: 7/7, new `piano-tension-energy`)

- 0.5.2-T1 potential vs the core `TensionModulationPotential` (keys 24, 45, 60, 84): identical.
- 0.5.2-T2 **lossless** string + hammer (muHC = 0, no drift relaxation, rigid bridge, 2 s of
  ff and mf notes, keys 24, 33, 45, 60): modified energy (`savEnergy` + released hammer energy +
  reset removals) conserved to **1.2e-12 relative at worst** (peak extra tension up to 41 N, key 60 at
  nu = 1).
- 0.5.2-T3 **lossy** (full loss law, felt dashpots, drift relaxation, keys 24, 33, 60, 84 at
  nu = 1): never above the initial energy (max rise 0), final 67-87% of it.
- 0.5.2-T4 tension off is bit-identical to `StruckNote` (keys 33, 60, 1 s).
- 0.5.2-T5 a loud A1 (nu = 1, rigid bridge) rings **+5.2 cents** higher in mode 1 over 50-250 ms
  than the same note without tension.
- Unchanged and passing: `bridge-check` (shared board energy), `piano-check` (all 0.5.1 tests with
  tension on), the CLAP and LV2 playback tests (LV2 `run()` allocation check included).
The coupled string + board system with tension is not given an energy test: the missing kappa_T
term (item 4) makes the moving-support balance inexact, and the board path adds loss; stability
is covered by the playback tests and the Chopin render (0 numerical faults, peak 0.1537 vs
0.1560).

### Results

Measurements (`tension_analysis.py` / `piano-tension-measure`, bridge force of the key on the full
Piano with shared board, unisons, 3 s, `tension-measurements-0.5.2.json`). Pitch: a partial near
700 Hz tracked by an analytic-signal phase slope, 0.5.2 minus 0.5.1 in cents:

| Key | Velocity | 30-60 ms | 60-100 ms | 100-250 ms | 0.25-0.5 s | 1-1.5 s |
| --- | --- | --- | --- | --- | --- | --- |
| A1 (33) | 40 | +0.07 | +0.06 | +0.05 | +0.03 | +0.01 |
| A1 | 92 | +0.62 | +0.57 | +0.41 | +0.28 | +0.12 |
| A2 (45) | 40 | +0.16 | +0.14 | +0.10 | +0.06 | +0.02 |
| A2 | 92 | **+1.52** | +1.39 | +1.03 | +0.61 | +0.20 |
| C4 (60) | 40 | +0.08 | +0.06 | +0.04 | +0.03 | +0.00 |
| C4 | 92 | +0.94 | +0.70 | +0.42 | +0.22 | +0.01 |

Glide inside the note (30-100 ms against 1-1.5 s, minus the same quantity without tension, which
is 0.3 cents and comes from board loading): A1 0.05 / 0.47, A2 0.13 / 1.24, C4 0.07 / 0.79 cents
at velocity 40 / 92. C6 (key 84): 0.0 / 0.8 / 2.9 cents at velocity 40 / 92 / 127. **These are
small: a few tenths of a cent to 1.5 cents, below pitch discrimination, because the model's
bass strings reach a few N of DeltaT (11-13 N for a bare string at nu = 1, test T3; 0.2-0.8% of
T) only in the first tens of ms and lose their energy to the board; velocity 127 on a bare string gives +5 cents (T5).**

Inharmonic sideband power (fraction of 30 Hz-3 kHz power of the bridge force outside +-2 Hz or
0.06% of any string partial; the 0.5.1 baseline is about -42 to -46 dB from board-coupled modes and
leakage):

| Key | Velocity | 0.5.1 | 0.5.2 | Change |
| --- | --- | --- | --- | --- |
| A0 (24) | 92 | -46.1 dB | -45.9 | +0.2 dB |
| A0 | 127 | -46.1 | -43.6 | +2.4 |
| A1 (33) | 92 | -44.1 | -44.1 | +0.05 |
| A1 | 127 | -44.2 | -43.4 | +0.8 |
| A2 (45) | 92 | -42.4 | -41.5 | +0.9 |
| A2 | 127 | -42.3 | **-35.3** | **+7.1** |

So phantom-type energy appears (and grows roughly as velocity^4) but only audibly (> 3 dB) at the
hardest strikes; the strongest outside peaks sit at 222.5 Hz for A2 (the 2f0 region), 63.5 Hz
for A0. Longitudinal waves (not modelled) are the stronger real source.

Octave-band shares versus Salamander, model minus reference, dB, velocity 60 (`0.5.1 / 0.5.2`,
band lower edges 62.5 ... 8000 Hz; all nine notes in `reference-calibration-0.5.2.json`):

```
A2 attack  -7.5/-7.5 +3.7/+3.7 +0.6/+0.6 -3.5/-3.5 -9.8/-9.8 +2.6/+2.6 +23.8/+23.8 -14.3/-14.3
A2 late    -6.8/-6.8 +9.9/+9.9 +0.5/+0.3 -17.6/-17.8 -13.9/-13.0 -5.0/-5.1 +11.6/+12.2 -25.1/-25.1
C4 attack  -3.1/-3.1 +12.0/+12.0 +1.0/+1.0 -1.2/-1.1 -1.4/-1.4 +4.2/+4.2 +12.4/+12.4 -0.6/-0.6
C4 late    -21.2/-21.3 -31.1/-31.3 +5.2/+5.2 -17.3/-17.4 -10.8/-11.0 -7.2/-7.1 -3.2/-3.5 -13.4/-13.5
C6 attack  -4.0/-4.0 -5.8/-5.7 -11.7/-11.7 -10.6/-10.6 +0.2/+0.2 -2.2/-2.2 +9.5/+9.5 -9.8/-9.6
C6 late    -16.4/-16.7 -41.3/-41.6 -63.1/-63.1 -52.2/-52.7 +0.4/+0.4 -12.6/-13.0 +1.5/+1.3 -13.7/-14.0
```

Band shares move by at most 0.9 dB at velocity 60 (A2 late 1000-2000 Hz +0.9, 4-8 kHz +0.6); at
velocity 92 the late window changes more (A2 +2.7 dB at 1-2 kHz, C6 -1.6 to -2.8 dB in the bands
from 62 Hz to 8 kHz, i.e. the late tail of a loud C6 is a little quieter relative to the attack). Mean attack high-band error (energy share above 2 kHz): 0.4.9 5.68 dB, **0.5.1 3.80,
0.5.2 3.83**. As expected, tension modulation does not change the calibration.

CPU (single thread, -O3, no SIMD, this machine; plug-in settings = 12 voices, sympathetic cap 8):

| Case | 0.5.1 | 0.5.2 all keys | 0.5.2 keys <= 47 |
| --- | --- | --- | --- |
| Chopin excerpt offline (66 s simulated, 88 voices) | 91-96 s | **113-115 s** (+24%) | n/a |
| Plug-in settings, Chopin 52-58 s | 1.68x real time | 1.03x | **1.64x** |
| Plug-in settings, pedalled 12-key cluster (48-59) | 0.90-0.97x | 0.50x | **0.97x** |
| same, tension for keys <= 53 / <= 59 | | 0.64x / 0.47x | |

The cost per struck string is about 2x with tension (the solve runs every sample, ~10 passes over
the modes instead of one), more in the middle and treble where three strings sound. **Plug-in
setting: tension for keys 21-47 (A0-B2) only** (`Piano::pluginTensionMaxKey`, `setTensionMaxKey`);
that is where the effect is largest per string (long strings, large DeltaT) and the strings have
one or two voices; the cluster stays at 0.97x as in 0.5.1. The pedalled cluster is already at
real-time limit with or without tension on this machine; for a safer plug-in use a larger
buffer or fewer voices. Offline renders use the full model.

### How to listen, and what to listen for

- `tension-demo-0.5.2.wav` (20 s, 24-bit, fixed rendering by `tension_demo.py`): A1, then A2, each
  played at fortissimo as 0.5.1 followed by 0.5.2, initial level (100-400 ms RMS) matched. Listen
  for: a very slight upward bend at the very start of the 0.5.2 note and a little extra
  roughness or "growl" in the A2 sustain, small and mostly in the first 100-300 ms. If you cannot
  hear it, that is consistent with the measurements (1.5 cents, +7 dB sidebands at velocity 127 only);
  a difference you do hear would more likely be the changed decay-phase spectrum than pitch.
- `chopin-prelude-0.5.1.wav` vs `chopin-prelude-0.5.2.wav` (same +12.04 dB gain, `-raw.wav` and
  `-audit.json` alongside, `chopin-prelude-latest.wav` = 0.5.2): RMS 0.013738 vs 0.013761 (+0.014 dB),
  peak 0.156 vs 0.154, left/right 1.06 vs 1.05 dB, octave-band levels (31 Hz ... 8 kHz) change by
  +0.02, +0.60, +0.01, -0.01, +0.03, -0.02, -0.11, -0.05, -0.10 dB; the null between the two is
  -18 dB re the signal (chaotic decorrelation of struck-string detail, no systematic timbre change).
  Expect the bass chords in the loud passages to feel very slightly harder-edged, not different.
- Known limits: no longitudinal modes (the main real source of phantom partials and bass
  precursor/"twang"); vertical polarisation only; sympathetic strings linear; kappa_T = 0;
  EA and everything else nominal; the hammer amplitudes follow the 0.5.1 velocity map, which
  was not tuned for large-amplitude behaviour.

### Why C4/C6 stay 28-65 dB low at 62-500 Hz (view for planning)

From the code and the data (not a finding of this iteration): a C4 string has no energy below
262 Hz and nothing a string does (tension modulation, phantom partials, longitudinal waves: all
sit at multiples or differences of its partials, hence >= f0) can fill the 62 and 125 Hz bands, so
that content is not the struck string. Candidates, with the cheapest test first:

1. **Damper leakage / sympathetic response of damped strings** (cheapest, already measured with
   existing code). The model never simulates damped strings, a real damper only lowers the Q.
   Rendering C4 at velocity 60/127 with every other damper lifted (`liftDamper`, upper bound)
   changes the late (0.5-1.5 s) octave shares from (62, 125 Hz) -22.8, -33.1 dB to **-3.8, +16.3 dB**
   versus the reference: the whole deficit is within reach of sympathetic bass strings, overshooting
   by about 16 dB at 125 Hz, i.e. a leakage factor of roughly -15 to -20 dB would fit. Next
   experiment: a leaky damper (woken damped strings with the damper law at a fraction of full
   damping), a few hours of work on the existing wake logic; verify against the reference spectrum
   lines (are the late low lines at bass string partials?).
2. Board/room low modes excited by the attack: check by looking at whether the reference late 62-250 Hz
   lines sit at fixed frequencies independent of the played pitch (compare C4 and C6 reference
   notes; offline, no engine change).
3. Key-bed/action thump and hammer-shank noise: a decaying transient, so it should show in the
   0-100 ms band, not the 0.5-1.5 s window (the model attack at 125 Hz is already +12 dB high), a
   cheap check on the reference envelope but unlikely to explain the late window.
4. Longitudinal string motion: excluded for these bands by the argument above.

## Update 0.5.1: one shared soundboard, sympathetic strings, key-dependent bridge points

Listener feedback on 0.5.0/0.4.9: "still rather bright and small ... lacks wood and
dimension", and "on a real long piano its shape imposes different harmonics on different
strings". Until now every key had a private 128-mode bridge and unstruck strings did not
exist. This iteration changes two things, each with its own setter (before `prepare`), on by
default so the listener can attribute differences.

| What | Setter | Off means |
| --- | --- | --- |
| One shared board; every simulated string coupled to it at its own bridge point | `setSharedBoard` | 0.5.0 private bridges, bit-for-bit |
| Key-dependent bridge points on a bass + treble bridge curve | `setGrandBridgeGeometry` | the 0.5.0 two-column pan blend (shared board only) |
| Unstruck strings with lifted dampers are simulated | `setSympatheticStrings`, `setSympatheticLimit(n)` | no sympathetic ringing |

`piano-midi-render ... --previous` is now **0.5.0** and reproduces the committed
`chopin-prelude-0.5.0-raw.wav` byte-for-byte (verified: `cmp` identical). `--shared-pan` is
0.5.1 coupling with the 0.5.0 bridge positions; `--v049` is the former `--previous`; `--v048`,
`--v046`, `--legacy` are unchanged. `piano-render` takes `previous` (0.5.0), `sharedpan`,
`v049`, `v048`, `v046`.

### Design

1. **Shared board (`grand_board.*`).** The 128 board modes that were private per key are now one
   set. Mode force is `sum_s phi_s,i F_s` and the motion of port `s` is
   `w_s = sum_i phi_s,i r_i`: the discretisation of `pme::bridge::BridgePoint` (SPEC-OSC
   difference form, forces at step n, accelerations `delta_tt w` from increments), generalised
   from one port to 88 (the engine's `BridgePoint` has a single `phi` per mode, so it cannot
   serve several bridge positions; its coefficients come from the same
   `compileBridgeMode`). Each key's `StruckNote` still owns a `BridgePoint`, but that point has no
   modes or conductance: the board's port increment `w^{n+1}-w^n` enters through the engine's
   external-velocity hook (SPEC-BODY Sympathetic path), so the string sees exactly the board's
   `delta_tt w`. Per sample: ports add `F^n`, the board advances, strings advance, then each port
   adds `F^{n+1}` to the radiating modes (as 0.5.0 did). All 512 modes still radiate with the
   MVP 3 zero-order-hold formulation, fed the same forces through the 0.4.9 bridge-mass low-pass;
   modes 129-512 are driven one way and reach the strings only as the passive residual loss, now
   evaluated with each key's own participation. **Documented simplification:** the 128 coupled
   modes exist twice, as mechanical (raw force) and as radiating (low-passed force) states of the
   same equations; this keeps the 0.4.9 high-frequency calibration intact and makes the
   one-voice case equal 0.5.0 (relative difference 2e-8, `piano-check` 0.5.1-T2).
2. **Sympathetic strings.** A string group is simulated while it is struck or while its damper is
   lifted (sustain pedal down, key held, or undamped treble, keys >= F#6) and the board moves its
   bridge point faster than `Piano::wakeVelocity` = 1e-4 m/s (assumption: about -55 dB re a
   loud strike's 0.06 m/s; checked every 32 samples, deterministic, allocation free). An
   unstruck group (`sympathetic_string.hpp`) is the engine's `ModalSegment` equations,
   vertical polarisation only (horizontal modes are never driven at a point without
   perpendicular conductance) and only modes below 2 kHz (the board is coupled to 1.87 kHz;
   truncating a passive system is passive). Checked against the full string model: the same
   A1 strike gives sympathetic forces of 8.0 vs 8.1 N (key 21), 1.8 vs 1.8 N (key 22), etc.
   Strings **start from rest** when they wake: if the pedal goes down while the board is
   already ringing, each string begins to follow the board at that moment (nothing is
   back-calculated), and a string struck while ringing hands its vertical modes over to the
   hammer voice. Releasing the pedal (or the key) damps woken strings with the existing damper
   law and they retire. Idle strings go back to sleep after 1 s with force below 1e-7 N and
   board speed below half the wake threshold. `liftDamper(key)` lifts a damper without a strike
   (used by the demo; there is no MIDI message for it). Cap: `setSympatheticLimit(n)`
   (offline default 88; plug-ins 8, `Piano::pluginSympatheticLimit`). When more strings are
   eligible than slots, those with the highest harmonic coincidence (partials 1-8 within 20 cents,
   weight 1/(m n)) with the loudest sounding struck strings are woken first. This ranking is a
   heuristic.
3. **Key-dependent bridge points.** Mode shapes are the analytic ones of the MVP 3 plate
   (simply supported 1.4 x 1.6 m orthotropic spruce plate with homogenised ribs; Chabassier &
   Durufle RT-0425 material, other numbers nominal) evaluated at each key's bridge point:
   `phi_mn = 2/sqrt(mu lx ly) sin(m pi x/lx) sin(n pi y/ly)`. `generate_grand_board.py`
   re-derives the (m, n) of each table row and checks all 512 omegas against the MVP 3 table to
   1e-12 (`grand_board_table.hpp`); a test shows the shapes reproduce the two old drive columns at
   the old bridge points (0.42, 0.91) and (0.97, 0.58) to 1e-9. **Bridge curve (assumption, not
   measured, no source):** bass bridge keys 21-44 straight from (0.30, 1.15) to (0.55, 0.82) m;
   long (treble) bridge keys 45-108 a quadratic Bezier from (0.62, 0.90) through control point
   (0.95, 0.78) to (1.25, 0.25) m, keys evenly spaced in the parameter (about 1.8 cm per key
   on the bass bridge, 1.5 cm on the treble bridge), at least 0.15 m from the plate edge. Split
   at key 44 is a guess (a real grand's bass bridge usually carries the wound strings). The
   rectangle is **not a grand outline**.
   **B2 body FEM:** `compiler/pme_compiler/body` can mesh an arbitrary outline with rib and
   bridge beams (Gmsh + scikit-fem + SciPy) but those packages are not installed here, and the
   MVP radiation table is built from the analytic plate, so a FEM board would also need a new
   radiation table. Not done; a grand outline with explicit ribs from B2 is the obvious next
   step for the board. Stereo stays the nominal two-microphone radiation (fixed microphones at x = 0.2 and 1.2 m):
   the right/left level of a 0.7-velocity note is within +-2.5 dB for keys 21-60 in both versions,
   and scatters in the treble (key 81: +14 dB in 0.5.0, +11 dB now; key 87: -9 dB now). There is
   no systematic bass-left/treble-right tendency, as in 0.5.0.

### Tests (all pass; `ctest`: 6/6)

- `bridge-check` (extended): 8 strings (keys 36-84) on the shared 128-mode board, energy =
  board modes + every segment. Lossless: maximum relative drift **5.1e-15** (pan columns) and
  **2.0e-14** (grand geometry) over 40 000 steps; lossy: never above the initial energy, final
  45% / 29% of it. Energy moves between strings: up to 56% / 27% of the initial energy sits in the
  seven unexcited strings (lossless), 23% / 11% with damping.
- `piano-check` 0.5.1-T1 key-dependent coupling (shapes reproduce the old columns; all bridge points
  inside the plate margins; adjacent keys differ, distant keys decorrelate, geometry differs from
  the pan blend), -T2 one voice on the shared board equals the 0.5.0 bridge to 1e-6 for keys
  33/60/84, -T3 sympathetic transfer (pedal, held key: an unstruck octave string wakes and has
  >1e-6 N force; damped, disabled, limit 0: it does not; pedal release damps it; pedal pressed
  after the strike wakes it later; two pianos produce identical bits).
- The existing LV2 playback test (no `operator new` during `run()`, identical output to the
  engine) passes with sympathetic strings on (limit 8).

### Results

CPU (single thread, -O3, no SIMD, this machine):

| Case | 0.5.0 | 0.5.1 |
| --- | --- | --- |
| Chopin excerpt offline (66 s simulated, 88 voices, pedal heavy) | 33 s (0.79x excerpt length) | **93 s** (0.28x excerpt length, 0.71x real time overall) |
| Full keyboard cluster, pedal, 88 struck | 23.0 s per 4 s | 22.1 s per 4 s |
| Pedal down, one loud A1, 87 sympathetic strings | 4.04x real time | **0.80x real time** (worst case) |
| Plug-in settings (12 voices, cap 8), Chopin 52-58 s | 1.83x real time | 1.62x real time |
| Plug-in settings, 12-key cluster with pedal | 1.12x real time | 1.05x real time |

The plug-ins are capped at 8 sympathetic strings; a 12-key cluster with a cap of 16 runs at 0.99x.
The cost per awake unstruck string is dominated by its 128-mode port (force, readout) rather than
its few modes; threading or SIMD would be the next lever and are not used.

Reference calibration (`reference-calibration-0.5.1.json`, 24-bit `calibration-*-0.5.1.wav`
plus `-0.5.1-pan.wav`, mean attack energy-share error above 2 kHz): **0.4.9 5.68 dB, 0.5.1-pan
5.63 dB, 0.5.1 3.80 dB.** The change from 0.4.9 to the new geometry is partly luck of the
modal sampling at each key's bridge point (C6 velocity 40 improves 13.3 to 4.5 dB, C6 velocity
92 worsens 1.5 to 5.0 dB): with only nine reference points do not read this as a calibrated
gain. Octave-band shares (band lower edges 62.5 ... 8000 Hz, model minus Salamander, dB), velocity 60:

```
A2 attack 0.4.9  +0.5 +0.8 -4.1 -0.7 -7.0 +4.5 +20.5 -17.5   0.5.1 -7.5 +3.7 +0.6 -3.5 -9.8 +2.6 +23.8 -14.3
A2 late   0.4.9  -0.1 +4.7 -6.5 -10.9 -10.6 -1.5 +8.9 -30.0  0.5.1 -6.8 +9.9 +0.5 -17.6 -13.9 -5.0 +11.6 -25.1
C4 attack 0.4.9  -5.1 +10.4 +2.2 -4.2 -0.5 +4.6 +10.5 -5.1   0.5.1 -3.1 +12.0 +1.0 -1.2 -1.4 +4.2 +12.4 -0.6
C4 late   0.4.9 -25.3 -31.8 +4.9 -8.8 -8.7 -3.1 -1.3 -19.1   0.5.1 -21.2 -31.1 +5.2 -17.3 -10.8 -7.2 -3.2 -13.4
C6 attack 0.4.9  -8.3 +1.4 -10.0 -6.6 -0.3 +5.3 +17.5 -8.6   0.5.1 -4.0 -5.8 -11.7 -10.6 +0.2 -2.2 +9.5 -9.8
C6 late   0.4.9 -14.6 -36.2 -57.3 -47.2 -0.1 +11.5 +13.2 -11.5 0.5.1 -16.4 -41.3 -63.1 -52.2 +0.4 -12.6 +1.5 -13.7
```

Velocities 40 and 92 and the `-pan` variant are in the JSON. **The low-band deficit is not
fixed.** C4 and C6 late 125-500 Hz stay 28-65 dB below the reference (C4 late 62-125 Hz improves
by about 4 dB, C6 does not), and the attack 62-500 Hz of C6 gets slightly worse. This is the
honest outcome of the experiment: the Salamander notes were recorded with the dampers down on
all other keys and no pedal, so for an isolated note only the undamped treble (>= F#6, here an
assumed boundary) can ring sympathetically in the model; damped strings, which are not simulated,
leak a little in reality. The reference's late-window 62-500 Hz content of C6 is about 15 dB above
the recording's own end-of-file floor, so it is not just converter noise, but whether it is key-bed or
action thump, room, damper leakage or longitudinal string motion cannot be told from these
files. The shared board moves energy between bands (late 500-1000 Hz share of A2 and C4 falls 7-9 dB;
late C6 2-4 kHz falls 24 dB and 4-8 kHz 12 dB, overshooting the reference) and the 4-8 kHz attack
excess is not removed (A2 +24, C4 +12, C6 +9.5 at velocity 60).

Chopin Op.28 No.4 excerpt, same fixed +12.04 dB listening gain: `chopin-prelude-0.5.1.wav` (+
`-raw.wav`, `-audit.json`, copied to `chopin-prelude-latest.wav`) next to the re-rendered
`chopin-prelude-0.5.0.wav`. RMS 0.013739 vs 0.013695 (+0.03 dB), peak 0.156 vs 0.145,
no voice steals, 0 numerical faults. Octave-band level change (0.5.1 minus 0.5.0, 31 Hz to 8 kHz
octaves): +9.2, +2.1, -0.8, +1.8, +0.9, -2.2, -1.5, 0.0, +1.5 dB; the lowest octave rises because
pedalled bass strings now ring with the board. Left/right balance +1.06 dB vs -0.47 dB.

`sympathetic-demo-0.5.1.wav` (and `-0.5.0.wav`, `-0.5.1-pan.wav`, 14 s, `piano-sympathetic-demo`,
fixed +12.04 dB gain): 0-6.5 s C3+E3+G3 have their dampers lifted but are never struck; staccato
C4 at 1 s and C5 at 3 s. 7-14 s pedal down and one loud A1.

| Window | 0.5.0 | 0.5.1-pan | 0.5.1 |
| --- | --- | --- | --- |
| C4 staccato 1.0-1.3 s | -23.7 dBFS | -24.5 | -20.5 |
| after C4, 1.5-2.9 s | **-60.2** | -38.0 | **-39.7** |
| after C5, 3.5-6.4 s | -62.8 | -45.0 | -48.9 |
| A1 with pedal, 8-10 s | -24.6 | -25.0 | -24.5 |

In (a) the unstruck C3-E3-G3 chord rings about 20 dB above the 0.5.0 silence after a staccato note.
In (b) the total level hardly changes (the pedalled A1 already rings by itself); above 1 kHz
the 0.5.1 tail is 2.7 dB lower, below 200 Hz 1.1 dB higher.

### How to listen, and what to listen for

Play `sympathetic-demo-0.5.0.wav` then `-0.5.1.wav`: after each staccato note the chord should
bloom softly (octave, fifth and third partials) instead of stopping dead. Then the Chopin pair
(`chopin-prelude-0.5.0.wav` vs `-0.5.1.wav`, same gain): look for a fuller low register with
the pedal, less transparent sustain, and key-to-key tone differences from the bridge geometry.
`--shared-pan` isolates the board coupling from the geometry. What it cannot
yet do: a wood-like knock, a grand outline, damped-string leakage, or any nonlinearity.

Known limits: the wake threshold, the 2 kHz cut, the treble undamped boundary, the
bridge curve, the board outline and all board numbers are assumptions; the radiating low modes
duplicate the coupled ones; sympathetic strings above 1.87 kHz are driven by nothing; the rank
heuristic is not physics; the sympathetic forces are large (up to 8 N peak at a bass string
after a loud A1, 10-20 dB below the struck force) and nothing here is tuned to a measurement of a real piano.

### Notes for 0.5.2 (string tension modulation)

Done in 0.5.2: see "Update 0.5.2" above.

## Update 0.5.0: full pedal set (polyphony, una corda, undamped treble)

The listener found 0.4.9 "small". Three cheap changes with no new physics; each has its
own setter (before `prepare`) and is on by default. `--previous` on `piano-midi-render`
and `piano-render` reproduces **0.4.9 bit-for-bit** (verified: the re-rendered
`chopin-prelude-0.4.9-raw.wav` and listening WAV are byte-identical to the committed
files); `--v048` is the old 0.4.8 mode; `--v046` and `--legacy` are unchanged.

1. **Polyphony.** `setVoiceLimit(1..88)`; class and real-time plug-ins (CLAP, LV2) stay at
   12 voices; offline renderers use 88 (one voice per key, no stealing). Pinned Chopin
   excerpt: **13 voice steals at 12, 0 at 88**; RMS 0.013695 vs 0.013695 (peak identical
   0.145065). Offline speed at 88: 66 s simulated (40 s pre-roll + 26 s) in 33.0 s, i.e.
   **2.0x realtime single-thread** (0.79x the 26 s excerpt length; 12 voices: 32.4 s).
2. **Una corda (CC67 >= 64).** `setUnaCorda`, `softPedal(bool)`; wired in `dispatchMidi`
   (CLAP, LV2, MIDI renderer; the event validator now also accepts CC67). Uses the engine's
   grand una-corda map (SPEC-HAM Una corda) through `StruckNote::launch(nu, shift=1)`:
   stiffness `K(1-s(1-kappa_uc))`, exponent `p + s*dp`, and the string at index 0 leaves the
   hammer with weight `1-smoothstep(s)`. The shift is read at launch only, so a hammer in
   flight is never moved (tested). The preset has no una-corda data, so the **engine
   defaults are assumed**: s0 0.3, s1 0.9, kappa_uc 0.6, dp 0, leaving string 0 (not
   measured). Effect: 3- and 2-string keys strike 2 and 1 strings with 0.6x felt stiffness;
   1-string keys (bass, key <= 32) keep their one string and only get the softer felt
   (the engine has no leaving string for a single string). Chopin fixture: no CC67 events
   (only CC64, 7, 10, 91), so the pinned excerpt is unchanged by this item.
3. **Undamped treble.** Keys >= 90 (F#6) ignore note-off damping, with or without sustain;
   they ring until natural decay or voice sleep. Boundary is an **assumption**
   (`firstUndampedKey`, no measurement): grands commonly stop the dampers around F#6/G6,
   about the top 1.5 octaves. The pinned excerpt uses keys 35-84, so it is unchanged here.
   The released radiating board still rings after a damped release, so tests compare the
   0.1-0.2 s window and whether the string voice survives.

Tests (`piano-check`): voice-limit range/steals/default, una corda on keys 24/40/60
(measurable, softer for multi-string, pedal after strike harmless, CC67 release, disabled
pedal), undamped boundary and damped-key invariance. Isolated single notes below key 90
without the pedal are unchanged, so the reference calibration was not rerun
(`reference-calibration-0.4.9.json` still applies).

## Update 0.4.9: calmer highs (bridge mass and saturating string loss)

Listener feedback on 0.4.8: "still rather bright and small, like a harpsichord and a
plastic upright; lacks wood and dimension". Octave-band shares against the Salamander
C5 recordings (see [reference](reference/README.md)) showed 4-8 kHz energy 10-30 dB
too high in the first 250 ms. This iteration changes only the high-frequency path;
the **low-frequency body deficit (knock, low modes) is deliberately untouched** (0.5.0).

### Diagnosis (measured, not assumed)

Bridge force (string+hammer, before the board) and radiated pressure were rendered
separately for A2, C4 and C6 and compared with the reference shares:

- **Board damping (sigma in `kSoundboardModes`) is not the cause.** The 512 modes have a
  constant loss factor 0.02 and overlap strongly above ~1 kHz (bandwidth 0.02 f vs mean
  spacing ~14 Hz), so the mean response there is set by modal density, not by damping.
- **Hammer spectrum is smooth** (A2 force envelope: -56 dB at 4 kHz, -82 dB at 8 kHz re
  peak), but felt stiffness and Hunt-Crossley mu (0.5 s/m, a declared MVP assumption)
  both move the attack 4 kHz band strongly (K x0.3: -14 dB; mu 0.5 to 0.2: -5 dB).
  Not changed here: that was the 0.4.8 voicing lever and softening further worsens the
  decay mismatch below.
- **The board adds +7 dB (2-4 kHz) and +12 dB (4-8 kHz) to the A2 attack share**: white
  noise through the radiation path rises about 8 dB from 500 Hz to 4 kHz. Radiation is
  proportional to modal acceleration (+6 dB/octave for a flat point mobility), and the
  board is driven directly by the bridge force with no bridge-top inertia.
- **String HF decays too fast.** Attack-to-late (0.5-1.5 s) change of the 4-8 kHz band:
  A2 -12.5 dB model vs +0.5 reference; C4 -14.7 vs -3.7. The report's `eta*T/(rho*A)*k^2`
  viscosity (`preset.cpp`) gives about 1.6 1/s at 4 kHz and 6.6 1/s at 8 kHz for C4.
  A static low-pass alone fixes the attack but leaves the late window 15 dB too dark.
- **The 8-16 kHz band in 16-bit WAVs is quantisation noise**, not model output
  (unquantised A2 8 kHz attack is -5 dB, quantised +17 dB). Calibration renders are now
  24-bit (`piano-render ... --24bit`); listening WAVs are unchanged (16-bit).

### Change (two linked mechanisms, each with its own switch)

1. **`setBridgeMassLoading`**: a series bridge-top mass m on a board point of real
   mobility Y passes `F/(1 + i omega m Y)` to the board (one-pole low pass, corner
   `1/(2 pi m Y)`). Y = 1.47e-3 m/(N s) is the infinite orthotropic plate value
   `1/(8 sqrt(sqrt(Dx Dy) mu))` of the MVP 3 board; **m = 55 g effective is an
   assumption** (corner 1.97 kHz; 1.4-2.0 kHz tested). It is applied to the summed
   bridge force entering the soundboard, not to the output. The string still sees the
   128-mode bridge motion unchanged (the string-side reaction is not mass-loaded).
   Rationale: bridge inertia is a recognised influence on piano bridge admittance
   (see Conklin, "Design and tone in the mechanoacoustic piano", JASA 1996; cited for
   context, the numeric mass is not taken from it or from any measurement).
2. **`setSaturatingStringLoss`**: the report's viscous term follows k^2 up to
   `kStringLossCornerHz` = 2.5 kHz, then keeps a constant loss factor (sigma proportional
   to omega, matched at the corner). Rationale: the k^2 law of the report/JASA 2013 is fitted
   to low partials; measured string loss factors grow far more slowly at high frequency.
   The corner is an assumption (1.0-3.5 kHz tested); Ru, bridge and residual losses unchanged.

Without (2), (1) makes the late window 4-8 kHz 7-16 dB too dark; without (1), (2) leaves
the attack unchanged. Mono-mix 4-8 kHz band, model minus reference, dB (attack / late):

| note | 0.4.8 | string loss only | bridge mass only | both (0.4.9) |
| --- | --- | --- | --- | --- |
| A2 | +18.2 / +1.3 | +18.9 / +7.3 | +10.3 / -6.6 | +10.9 / -1.0 |
| C4 | +11.6 / -7.4 | +12.6 / +1.7 | +2.9 / -15.8 | +3.9 / -7.0 |
| C6 | +13.8 / +3.1 | +15.2 / +17.8 | +5.9 / -4.7 | +7.3 / +9.2 |

### Result (24-bit renders, stereo power, shares re total, model minus reference in dB)

Mean attack >2 kHz energy-share error (nine points) falls from **9.44 to 5.68 dB**
(`reference-calibration-0.4.9.json` holds all windows and bands). 4 kHz band, 10-250 ms:

| note, layer velocity | 0.4.8 | 0.4.9 |
| --- | --- | --- |
| A2 40 / 60 / 92 | +19.8 / +27.4 / +33.0 | +12.9 / +20.5 / +26.0 |
| C4 40 / 60 / 92 | +24.6 / +17.3 / +11.1 | +17.9 / +10.5 / +4.1 |
| C6 40 / 60 / 92 | +27.7 / +23.1 / +19.5 | +22.5 / +17.5 / +15.0 |

Full octave table, velocity 60 (band lower edges 62...8000 Hz):

```
A2 attack 0.4.8  +0.4 +0.8 -4.0 -0.4 -5.4  +8.3 +27.4  -7.5    0.4.9 +0.5 +0.8 -4.1 -0.7 -7.0  +4.5 +20.5 -17.5
A2 late   0.4.8  -0.1 +4.7 -6.3 -10.4 -8.6 +2.2 +11.8 -34.6    0.4.9 -0.1 +4.7 -6.5 -10.9 -10.6 -1.5 +8.9 -30.0
C4 attack 0.4.8  -5.3 +10.2 +2.1 -4.0 +1.0 +9.2 +17.3  +3.6    0.4.9 -5.1 +10.4 +2.2 -4.2 -0.5  +4.6 +10.5  -5.1
C4 late   0.4.8 -25.4 -31.9 +4.8 -8.4 -7.3 +0.6  +0.0 -30.0    0.4.9 -25.3 -31.8 +4.9 -8.8 -8.7 -3.1  -1.3 -19.1
C6 attack 0.4.8 -10.0  -0.3 -11.5 -7.6 -1.0 +8.4 +23.1  -3.4    0.4.9 -8.3  +1.4 -10.0 -6.6 -0.3  +5.3 +17.5  -8.6
C6 late   0.4.8 -16.0 -37.5 -58.6 -48.6 -0.4 +13.5 +9.7 -19.8    0.4.9 -14.6 -36.2 -57.3 -47.2 -0.1 +11.5 +13.2 -11.5
```

The +-6 dB target at 4 kHz is **not met** in the attack (only C4 at velocity 92, +4.1, is within
+-6; the others are +10 to +26 dB), and it cannot be judged at 8 kHz: the reference there is within ~10 dB of
its own noise floor and the model's 8-16 kHz content comes only from partials above the
last board mode (7.2 kHz). The 1-2 kHz attack stays within -7.5 dB (A2 -7.0 at velocity 60,
-7.5 at 92; C4/C6 within -1.7); that band lost 1-2 dB. Late 4 kHz is within +-6 dB for
C4 (all velocities) and A2 at velocity 40, but A2 at 60/92 is +8.9/+15.9 and C6 +9 to +17 dB. Shares are relative: the
C4 and C6 low bands (62-250 Hz and 250-500 Hz) remain 25-60 dB low, so every other band
looks high. Reference late-window HF is also contaminated by the recording's noise
floor above ~4 kHz for C6. These are signal measurements, not a listening endorsement.

Compare `out/piano-mvp-v4/chopin-prelude-0.4.8.wav` with `chopin-prelude-0.4.9.wav`
(identical source, tempo, velocities, pedal, excerpt and +12.04 dB gain; 13 voice
steals at the unchanged 12-key limit). `chopin-prelude-latest.wav` is 0.4.9. RMS is
0.0137 vs 0.0142 (-0.3 dB). `C4-reference-0.4.8-0.4.9.wav` replaces the old
diagnostic clip. `piano-midi-render ... --previous` reproduces 0.4.8 bit-for-bit
(checked against the committed 0.4.8 WAV), `--v046` reproduces 0.4.6, `--legacy`
0.4.4; `piano-render ... previous|v046`, `piano-hammer-audit --previous|--v046` match.
The test `piano-behaviour` now checks the saturated viscosity law (identical below the
corner, constant loss factor above it, passive, monotone), the bridge-mass DC gain and
corner, and that the 0.4.9 attack has at least 25% less fourth-difference
(high-frequency) energy than 0.4.8 for A2 and C4. LV2 URI unchanged; name is now
**Piano MVP 4 - Calmer highs**.

## Update 0.4.8: reference-guided felt voicing

The listener finds 0.4.6 closer, but still too metallic and lacking wooden body
character. We now compare against nine pinned original lossless recordings from
Alexander Holm's **Salamander Grand Piano** (Yamaha C5), covering A2/C4/C6 at three
strengths. The [reference instructions, attribution and limits](reference/README.md)
are committed, along with per-file source hashes. Audio is a calibration reference;
the instrument still synthesises its output without playing samples.

This iteration reduces total contact stiffness to **35%** in the bass/mids through
C5, returning smoothly to the original value between C5 and C6. It keeps the
hammer mass, nonlinear exponent, velocity map, string geometry, board response
and output gain fixed. The first full-range soft-felt candidate was rejected in
the treble because it moved the attack spectrum further from the reference.

Middle C's medium-strike contact grows from about 2.01 ms to 2.21 ms (audit velocity
72); softer felt reduces high-frequency excitation at the source. This is a voicing
choice, not a claim that the tabulated source K was wrong. Contact duration's role
in spectral balance is described by
[Askenfelt and Jansson](https://www.speech.kth.se/music/5_lectures/askenflt/stricont.html).
Adding a darker tone alone does not establish the missing wooden body sound.
The soundboard remains nominal and its response still needs independent calibration.

Listen to `out/piano-mvp-v4/chopin-prelude-latest.wav` (26 seconds), comparing
`chopin-prelude-0.4.6.wav` with `chopin-prelude-0.4.8.wav`. The source/event checksums,
tempo, velocities, pedal, excerpt and fixed +12.04 dB listening gain are unchanged.
The separate 24.75-second `C4-reference-previous-soft-felt.wav` compares actual piano,
old model and candidate at each strength; it matches initial levels only for timbre
comparison. The original FLAC source is not altered.

`piano-hammer-audit` measures contact duration and force across six registers and
three strengths. A new test requires shorter contacts and larger peak forces for
harder strikes. `piano-midi-render ... --previous` reproduces 0.4.6;
`--legacy` reproduces 0.4.4. The LV2 URI is unchanged; its name is now
**Piano MVP 4 - Soft felt**. Historical outputs below retain their version labels.

## Update 0.4.6: omitted bridge-mode losses and a fixed classical excerpt

The radiation path already contained 512 soundboard modes, while the real-time
mechanical bridge retained only the lowest 128 (through 1868.6 Hz). This truncation
underrepresented the high-frequency energy escaping the strings. This iteration
adds the **real mechanical admittance of modes 129–512** as a passive, diagonal
string-loss approximation, calculated at each partial frequency during prepare.
There is no extra audio EQ, modulation, reverb or playback-time modal work.

For a mass-normalised board mode with drive phi, natural frequency Omega and
decay sigma, the residual conductance at string frequency omega is:

```
Re Y = sum(phi^2 * 2*sigma*omega^2 /
           ((Omega^2 - omega^2)^2 + (2*sigma*omega)^2))
extra string sigma_n = rhoA * omega_n^2 * g_n^2 * Re Y / 2
```

The first expression is the dissipative part of the omitted modal mobility. The
second equates average bridge power to the string mode's energy loss. It is a
weak-coupling approximation: it does not reproduce the omitted modes' reactive
pitch shift or cross-string correlations. The explicit low 128 modes still couple
the strings. The underlying board is still nominal, not a measured piano; this
is a test of the metallic-ring hypothesis, not a claim of calibrated realism.
The modal-mobility approach is discussed by
[Boutillon and Ege (2013)](https://ppi-lms.polytechnique.fr/xavier.boutillon/Annexes/Boutillon2013.pdf).

For the same middle-C strike, energy above 2 kHz in the 0.5–1 s window falls from
13.81% to 3.22% of that window's spectral energy. Initial 50–250 ms RMS changes by
only −0.05 dB. These are signal measurements, not a listening assessment.

New listening outputs use **Chopin, Prelude Op. 28 No. 4**, MIDI performance by
Bernd Krueger. The [fixed source, licence and validation](listening/README.md)
are committed. Listen to `out/piano-mvp-v4/chopin-prelude-latest.wav` (26 seconds),
or compare `chopin-prelude-0.4.4.wav` and `chopin-prelude-0.4.6.wav`. Both use the
same source notes, velocities 18–92, tempo map, pedal, excerpt, and fixed +12.04 dB
listening gain. Source time 40–64 s is followed by a two-second release.
`chopin-prelude-0.4.6-audit.json` records the source/event/WAV checksums and render
reports. There are 13 voice steals under sustain in this excerpt at the unchanged
12-key limit. This limitation is shared by both comparison versions.

Reproduce with `render_classical.py` as described in the fixture README.
`piano-midi-render ... --legacy` and the single-note renderer's `unisons` mode
disable only this new residual loss, preserving the 0.4.4 model.
The installed LV2 keeps its URI and is named **Piano MVP 4 - Bridge loss**.
Previous demos and comparison files below remain labelled historical baselines.

## Update 0.4.4: coupled unison strings

This update supersedes the single-string description below. MIDI keys 21–32 use
one string, 33–51 use two, and 52–108 use three. These register boundaries are
explicit prototype assumptions, not measurements of the source piano. Each key
uses one shared full-mass hammer and one shared moving bridge; every string has
its own compiled modes, tension, loss terms and state. Felt stiffness is divided
between the contacts, treating the published K as the whole contact patch. That
interpretation still needs experimental calibration. The tuning offsets are
±0.4 cents for pairs and −0.8/0/+0.8 cents for triples, also declared assumptions.

All strings contribute to radiation and all are damped on key release. Cross-key
sympathetic ringing and calibrated soundboard geometry remain unfinished. The
soundboard, output gain and velocity curve are unchanged. Polyphony is now 12
active keys to control the additional CPU cost; oldest keys are stolen at the limit.

Listen to `out/piano-mvp-v4/coupled-unisons-demo.wav`, then
`single-vs-unisons-C4.wav` (single at 0 s, new unisons at 5.5 s). Only the initial
50–250 ms RMS is matched in that comparison; later decay is preserved. The
early/late energy change is −9.92 dB for the single versus −14.25 dB for the group.
This demonstrates changed decay, not verified acoustic-piano realism.
`velocity-soft-medium-hard.wav` plays velocities 32/72/112 at 0/6.5/13 s using
one common gain; the updated soft/hard RMS difference is 14.28 dB.

Reproduce with `python3 mvp/piano-v4/unison_compare.py out/piano-mvp-v4` and
`python3 mvp/piano-v4/velocity_check.py out/piano-mvp-v4`.
The renderer's `single` mode retains the previous coupled single-string control.
The installed LV2 keeps its existing URI and is named **Piano MVP 4 - Coupled unisons**.
All five CTest checks pass, including 88 keys at 44.1/48/96 kHz and sample-exact
LV2/core playback. Actual Carla peaks at velocities 32/72/112 were
0.012155/0.042394/0.082726. At the 12-key limit, the offline plug-in stress test
rendered 10 seconds in 9.38 seconds on this machine: headroom remains tight and
this is not a guarantee of dropout-free live playback.

## Original 0.4 baseline

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

### Velocity audit

`python3 mvp/piano-v4/velocity_check.py out/piano-mvp-v4` renders middle C at MIDI
velocities 32, 72 and 112. `velocity-soft-medium-hard.wav` plays those strengths
at 0, 6.5 and 13 seconds, using one common gain to preserve their level differences.
Raw renders and `velocity-metrics.json` are retained alongside it. The measured
soft/hard RMS difference is 14.41 dB; harder strikes also have more high-frequency
attack energy. The actual Carla test now checks all three velocities too.
This verifies the synthesis/host path, not the physical keyboard's transmitted
velocities. No timbre or installed instrument binary changed in this audit.
