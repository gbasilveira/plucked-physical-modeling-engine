# SPDX-License-Identifier: MIT
"""test-guitar-6 (B1-R6).

    python -m pme_compiler.examples.test_guitar --out instruments/examples/test-guitar-6/

Six plucked nylon strings addressed by segment (key -1, SI-0004), a finger exciter, a small
rectangular spruce body. All parameter values are NOMINAL: chosen by the planner for testing, not
measured or fitted. P3 and P6 add frets and fit the values.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from pme_compiler.examples import common
from pme_compiler.schema import models

# --- nominal constants (source: planner's test values, brief B1-R6) ------------------------------
SCALE_LENGTH = 0.650  # m
TARGET_TENSION = 70.0  # N, every string
# Open-string pitches of strings 6 to 1 (Hz).
PITCHES = (82.407, 110.000, 146.832, 195.998, 246.942, 329.628)
NYLON_DENSITY = 1140.0  # kg/m^3
NYLON_YOUNGS = 4.0e9  # Pa
CORE_DIAMETER = 0.50e-3  # m, nylon core of the wound strings 4 to 6
COPPER_DENSITY = 8960.0  # kg/m^3
WINDING_PACKING = 0.85
LOSSES = (1.0, 0.0, 5.0e-9)  # b1 (1/s), b2 (s^-1/2), b3 (s)
FINGER_POSITION = SCALE_LENGTH - 0.12  # m
STRING_SPACING = 0.012  # m along the saddle
SADDLE_X, SADDLE_Y = 0.12, 0.18  # m, body frame
BRIDGE_CONDUCTANCE = 1.0e-3  # s/kg
BODY_X, BODY_Y, BODY_H = 0.48, 0.36, 0.0025  # m


def finger() -> models.Finger:
    """The nominal fingertip."""
    return models.Finger(
        tipMass=0.002,
        driverStiffness=2000.0,
        driverSpeedMax=1.0,
        slipHeight=0.002,
        contactK=1.0e5,
        contactAlpha=1.5,
        muFlesh=0.5,
        width=0.010,
        position=common.sig(FINGER_POSITION),
        angle=0.0,
        strengthGamma=1.0,
    )


def _string(index: int, f1: float) -> models.StringParams:
    """String ``index`` (0 = string 6): wound for the lowest three, plain for the others."""
    if index >= 3:
        diameter = common.sig(
            common.plain_diameter_for_tension(
                SCALE_LENGTH, f1, TARGET_TENSION, NYLON_DENSITY, NYLON_YOUNGS
            )
        )
        return common.plain_string(SCALE_LENGTH, diameter, NYLON_DENSITY, NYLON_YOUNGS, f1, LOSSES)
    outer = common.sig(
        common.wound_outer_diameter(
            SCALE_LENGTH,
            f1,
            TARGET_TENSION,
            CORE_DIAMETER,
            NYLON_DENSITY,
            NYLON_YOUNGS,
            COPPER_DENSITY,
            WINDING_PACKING,
        )
    )
    return models.StringParams(
        L=SCALE_LENGTH,
        dCore=CORE_DIAMETER,
        rhoCore=NYLON_DENSITY,
        ECore=NYLON_YOUNGS,
        dOuter=outer,
        rhoWind=COPPER_DENSITY,
        packing=WINDING_PACKING,
        epsWind=0.0,
        f1Target=f1,
        b1=LOSSES[0],
        b2=LOSSES[1],
        b3=LOSSES[2],
        sigmaOverride=[],
        detuneH=0.0,
    )


def build() -> models.InstrumentFile:
    """The instrument."""
    groups = []
    points = []
    for i, f1 in enumerate(PITCHES):
        segment = models.SegmentDesc(
            string=_string(i, f1),
            bridgePoint=i,
            polarisationAngle=0.0,
            barriers=[],
            muteRatio=0.0,
            tensionModulation=False,
            kappaT=0.0,
            unisonDetuneCents=0.0,
        )
        groups.append(
            models.NoteGroupDesc(
                key=-1,
                segments=[segment],
                exciter=models.ExciterParams(e=finger()),
                unaCorda=models.UnaCordaParams(leavingString=-1),
            )
        )
        points.append(
            models.BridgePointDesc(
                position=models.Point(
                    x=SADDLE_X, y=common.sig(SADDLE_Y + (i - 2.5) * STRING_SPACING), z=0.0
                ),
                thetaB=0.0,
                conductanceH=BRIDGE_CONDUCTANCE,
            )
        )
    desc = models.InstrumentDesc(
        name="test-guitar-6",
        groups=groups,
        bridgePoints=points,
        body=models.BodyParams(
            source="PlateModel",
            plate=common.spruce_plate(BODY_X, BODY_Y, BODY_H),
            modalTableHash="",
        ),
        channels=[models.ChannelDesc(name="mono", position=models.Point(x=0.24, y=0.18, z=1.0))],
        tuning=models.TuningCurve(stretchCents=[]),
    )
    return models.InstrumentFile(desc=desc, blobs=[])


def main(argv: list[str] | None = None) -> int:
    """Write the instrument to ``--out``/instrument.json."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    args = parser.parse_args(argv)
    file = build()
    for g in file.desc.groups:
        s = g.segments[0].string
        t = common.tension(s)
        print(f"f1 {s.f1Target:8.3f} Hz  d_o {s.dOuter * 1e3:.4f} mm  T {t:.3f} N")
        assert abs(t - TARGET_TENSION) < 1.0e-3 * TARGET_TENSION, "rounding moved the tension"
    print(f"wrote {common.write_example(file, args.out)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
