# SI-0030 ORC-19's quantitative rise criterion: 20 dB by 0.5 s is not met at Z₀|Y| = 0.02

Status: resolved (2026-10-12, planner; the number was the planner's in the A5 test-author brief, not SPEC's) · Raised by: A5, test-author · Date: 2026-10-12
SPEC section: SPEC-ORC row "Una corda aftersound" (ORC-19); docs/agents/briefs/A5-test-author.md, ORC-19 check 2

## Failing test
ORC-19 check 2 of the A5 test-author brief: the passive string's RMS bridge-force envelope (50 ms windows) rises at least 20 dB above the first window by 0.5 s. Measured at Z₀|Y| = 0.02: 8.49 dB. The bridge couples the strings within about 50 ms, so the first window already contains energy; 20 dB would need roughly six times weaker coupling.

## Evidence
SPEC's criterion is qualitative ("starts at zero and grows over time, as in Miranda Valiente et al."). The literature survey cites Miranda Valiente et al. (2024) and Weinreich (1977) for the effect but gives no rise in dB, and the rise depends on the coupling strength (Weinreich: the energy transfer rate between unison strings scales with the bridge admittance). The 20 dB figure was a planner choice without a source.

## Proposed correction
Brief criterion (A5 test-author, ORC-19 check 2), replacing "at least 20 dB … by 0.5 s":
- the first 50 ms window of the passive string's bridge force lies at least 60 dB below the struck strings' first window (it starts from zero);
- its envelope rises by at least 6 dB above the first window within 0.5 s, and the maximum lies after 0.1 s;
- the 20 dB figure is dropped (not kept as an expected failure); the measured rise is reported;
- SHOULD (report, not gate): the rise within the first 100 ms increases monotonically with Z₀|Y| over 0.005, 0.01 and 0.02.

The golden envelope stays "the first accepted run", **provisional until the owner accepts it after a re-run on the merged A5 code** (the current file comes from the test-author's SPEC-derived rig).

## Resolution
Applied to docs/agents/briefs/A5-test-author.md on 2026-10-12; SPEC-ORC unchanged (still qualitative plus regression). Affected phases: A5 (test-author follow-up: replace the may-fail 20 dB case; regenerate the golden on the merged code for the owner's acceptance), E1-T2 (uses ORC-19 on the full instrument).

## Follow-up (2026-10-13)
- The golden envelope was regenerated from the real A5 run: first window 76.88 dB below the struck strings, rise 8.796 dB by 0.5 s, maximum 3.670 N at 0.425 s. It remains provisional until the owner accepts it.
- The SHOULD added above (rise within 100 ms monotone in Z₀|Y|) is not met (7.53, 6.98, 6.70 dB at 0.005, 0.01, 0.02) and was the wrong expectation; replaced by [SI-0031](SI-0031-orc19-rise-monotonicity.md).
