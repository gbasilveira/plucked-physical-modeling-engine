# SI-0031 ORC-19's SHOULD "rise grows monotonically with Z₀|Y|" is not expected physically

Status: resolved (2026-10-13, planner; the SHOULD was the planner's in the A5 test-author brief, not SPEC's) · Raised by: A5, test-author · Date: 2026-10-13
SPEC section: SPEC-ORC row "Una corda aftersound" (ORC-19); docs/agents/briefs/A5-test-author.md, ORC-19 check 2 (SHOULD, added by SI-0030)

## Failing test
ORC-19, SHOULD (reported, not gating): "the rise within the first 100 ms grows monotonically with Z₀|Y| over 0.005, 0.01 and 0.02". Measured rise of the passive string's 50–100 ms window over its first 50 ms window:

| Z₀\|Y\| | Rise (dB) |
| --- | --- |
| 0.005 | 7.53 |
| 0.01 | 6.98 |
| 0.02 | 6.70 |

The rise falls with coupling. The numbers are the same on the test author's SPEC-derived scratch scheme and on the A5 code, so this is not a code defect.

## Evidence
The criterion compares two windows of the same envelope, so it measures the shape of the build-up, not its speed or level.

- At weak coupling the bridge motion is proportional to the struck strings' force times Y, and the passive string, driven at resonance, grows linearly in time with a slope proportional to Z₀|Y|. A linear ramp gives RMS over 50–100 ms / RMS over 0–50 ms = √7 ≈ 2.65, i.e. **8.4 dB, independent of Z₀|Y|**. The coupling sets the level of both windows, not their ratio.
- At stronger coupling the transfer saturates sooner (the energy exchange period between unison strings shortens as the coupling grows, Weinreich 1977) and the coupled pair loses energy through the bridge faster. Both bend the ramp down within 100 ms, so the ratio falls below 8.4 dB as Z₀|Y| grows. The measured 7.53 → 6.98 → 6.70 dB is this trend, with the weakest coupling closest to the ramp limit.

The SHOULD therefore expected the wrong sign. What does grow monotonically with Z₀|Y| at weak coupling is the passive string's level relative to the struck strings (about +6 dB per doubling of Z₀|Y| while the ramp holds).

## Proposed correction
A5 test-author brief, ORC-19 check 2, replace the SHOULD by:

- SHOULD (reported, not gating): the passive string's first-window level relative to the struck strings' first window increases monotonically with Z₀|Y| over 0.005, 0.01 and 0.02 (expected about +6 dB per doubling at weak coupling); the 50–100 ms rise over the first window is reported for each Z₀|Y| and is expected to stay at or below the linear-ramp value 8.4 dB and to fall slowly with Z₀|Y|.

No SPEC change: SPEC-ORC stays qualitative plus regression.

## ORC-19 golden envelope (recorded 2026-10-13)
Regenerated from the real A5 run (Z₀|Y| as in the test's default setup), replacing the scratch-rig file. **Provisional, pending the owner's acceptance** (SI-0030):

- passive string's first window 76.88 dB below the struck strings' first window (criterion ≥ 60 dB);
- rise 8.796 dB above its first window by 0.5 s (criterion ≥ 6 dB);
- envelope maximum 3.670 N at 0.425 s (criterion: after 0.1 s).

## Resolution
Applied to docs/agents/briefs/A5-test-author.md on 2026-10-13. A brief criterion only, no physics or numerics change. Affected phases: A5 (test author rewords the reported SHOULD; no code change), E1-T2 (uses ORC-19 on the full instrument; same metric).
