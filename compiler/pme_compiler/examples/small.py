# SPDX-License-Identifier: MIT
"""test-monochord and test-unison-pair (B1-R6).

    python -m pme_compiler.examples.small --out instruments/examples

All parameter values are NOMINAL: chosen by the planner for testing, not measured or fitted. The
monochord has b_2 = 0 and a plain rectangular plate so that it stays comparable with the
reference solver (P0.5).
"""

from __future__ import annotations

import argparse
from pathlib import Path

from pme_compiler.examples import common
from pme_compiler.schema import models

# --- nominal constants (source: planner's test values, brief B1-R6) ------------------------------
STEEL_DENSITY = 7850.0  # kg/m^3
STEEL_YOUNGS = 200.0e9  # Pa
LOSSES = (0.5, 0.0, 2.0e-9)  # b1 (1/s), b2 (s^-1/2), b3 (s)
HAMMER_MASS = 0.008  # kg
HAMMER_P = 2.5
HAMMER_CONTACT_TIME = 0.002  # s, SPEC-HAM oracle contact time at HAMMER_SPEED
HAMMER_SPEED = 3.0  # m/s
HAMMER_MU_HC = 0.2  # s/m
HAMMER_WIDTH = 0.010  # m
STRIKE_RATIO = 1.0 / 8.0
BRIDGE_CONDUCTANCE = 1.0e-3  # s/kg
PLATE_X, PLATE_Y, PLATE_H = 0.6, 0.4, 0.006  # m
BRIDGE_POSITION = models.Point(x=0.3, y=0.2, z=0.0)
CHANNEL_POSITION = models.Point(x=0.3, y=0.2, z=1.0)


def hammer() -> models.HammerParams:
    """The 8 g Hunt-Crossley hammer of both instruments."""
    return models.HammerParams(
        mass=HAMMER_MASS,
        K=common.sig(
            common.hammer_stiffness_for_contact_time(
                HAMMER_MASS, HAMMER_P, HAMMER_SPEED, HAMMER_CONTACT_TIME
            ),
            6,
        ),
        p=HAMMER_P,
        muHC=HAMMER_MU_HC,
        width=HAMMER_WIDTH,
        strikeRatio=STRIKE_RATIO,
        stulovEps=0.0,
        stulovTau=0.0,
    )


def _instrument(
    name: str, key: int, strings: list[models.StringParams], leaving: int
) -> models.InstrumentFile:
    segments = [
        models.SegmentDesc(
            string=s,
            bridgePoint=0,
            polarisationAngle=0.0,
            barriers=[],
            muteRatio=0.0,
            tensionModulation=False,
            kappaT=0.0,
            unisonDetuneCents=0.0,
        )
        for s in strings
    ]
    group = models.NoteGroupDesc(
        key=key,
        segments=segments,
        exciter=models.ExciterParams(e=hammer()),
        unaCorda=models.UnaCordaParams(leavingString=leaving),
    )
    desc = models.InstrumentDesc(
        name=name,
        groups=[group],
        bridgePoints=[
            models.BridgePointDesc(
                position=BRIDGE_POSITION, thetaB=0.0, conductanceH=BRIDGE_CONDUCTANCE
            )
        ],
        body=models.BodyParams(
            source="PlateModel",
            plate=common.spruce_plate(PLATE_X, PLATE_Y, PLATE_H),
            modalTableHash="",
        ),
        channels=[models.ChannelDesc(name="mono", position=CHANNEL_POSITION)],
        tuning=models.TuningCurve(stretchCents=[]),
    )
    return models.InstrumentFile(desc=desc, blobs=[])


def monochord() -> models.InstrumentFile:
    """One plain steel string, key 45 (A2, 110 Hz), L = 1.0 m, d = 1.0 mm."""
    key = 45
    string = common.plain_string(
        1.0, 1.0e-3, STEEL_DENSITY, STEEL_YOUNGS, common.equal_tempered(key), LOSSES
    )
    return _instrument("test-monochord", key, [string], leaving=-1)


def unison_pair() -> models.InstrumentFile:
    """Two identical plain steel strings, key 60 (C4), L = 0.64 m, d = 1.05 mm."""
    key = 60
    strings = [
        common.plain_string(
            0.64, 1.05e-3, STEEL_DENSITY, STEEL_YOUNGS, common.equal_tempered(key), LOSSES
        )
        for _ in range(2)
    ]
    return _instrument("test-unison-pair", key, strings, leaving=0)


def main(argv: list[str] | None = None) -> int:
    """Write both instruments below ``--out``."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="examples root directory")
    args = parser.parse_args(argv)
    for name, build in (("test-monochord", monochord), ("test-unison-pair", unison_pair)):
        print(f"wrote {common.write_example(build(), args.out / name)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
