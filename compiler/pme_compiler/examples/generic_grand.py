# SPDX-License-Identifier: MIT
"""generic-grand-88: a generic 88-key grand piano description (B1-R6).

    python -m pme_compiler.examples.generic_grand --out instruments/examples/generic-grand-88/

Every constant below is NOMINAL: chosen by the planner for testing, not measured, fitted or taken
from any real piano. Experiments E1 and P6 replace them. Rear segments are not included
(SI-0010 is open). Tension modulation is off.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from pme_compiler.examples import common
from pme_compiler.schema import models

# --- nominal constants ---------------------------------------------------------------------------
FIRST_KEY, LAST_KEY = 21, 108
SINGLE_KEYS = range(21, 29)  # 1 string
DOUBLE_KEYS = range(29, 41)  # 2 strings
WOUND_KEYS = range(21, 41)  # wound speaking strings
# Speaking length anchors (key, m), log-linear in between.
LENGTH_ANCHORS = [
    (21, 2.00), (28, 1.85), (29, 1.83), (40, 1.50), (41, 1.45), (48, 1.15), (60, 0.64),
    (72, 0.33), (84, 0.17), (96, 0.090), (108, 0.052),
]  # fmt: skip
PLAIN_D = ((41, 1.20e-3), (108, 0.80e-3))  # plain steel diameter, log-linear
STEEL_DENSITY, STEEL_YOUNGS = 7850.0, 200.0e9
CORE_D = ((21, 1.60e-3), (40, 1.15e-3))  # wound steel core diameter, log-linear
TARGET_TENSION = ((21, 1600.0), (40, 850.0))  # wound strings, linear (N)
COPPER_DENSITY, WINDING_PACKING, WINDING_EPS = 8960.0, 0.85, 0.0
LOSSES = (0.4, 0.0, 2.0e-9)
TENSION_BOUNDS = (400.0, 2000.0)  # N, asserted for every speaking string
HAMMER_MASS = ((21, 0.011), (108, 0.0045))  # kg, log-linear
HAMMER_CONTACT_TIME = ((21, 0.004), (108, 0.0005))  # s at HAMMER_SPEED, log-linear
HAMMER_SPEED, HAMMER_P, HAMMER_MU_HC = 3.0, 2.5, 0.2
HAMMER_WIDTH = ((21, 0.015), (108, 0.005))  # m, linear
STRIKE_RATIO = 1.0 / 8.0  # up to STRIKE_BREAK, then linear to STRIKE_RATIO_TOP at the last key
STRIKE_BREAK, STRIKE_RATIO_TOP = 88, 1.0 / 12.0
LAST_DAMPED_KEY = 88
DAMPER = {"xRatio": 0.05, "width": 0.020, "Kd": 5000.0, "Rd": 5.0}
BRIDGE_CONDUCTANCE = 1.0e-3  # s/kg
BASS_KEYS = range(21, 41)
CHANNELS = (("L", (-0.5, 0.7, 1.0)), ("R", (2.0, 0.7, 1.0)))
PLATE_THICKNESS, GRAIN_ANGLE, RIM_STIFFNESS = 0.009, 0.7, 1.0e7
RIB_COUNT, RIB_WIDTH, RIB_HEIGHT, RIB_INSET = 12, 0.025, 0.020, 0.03
SPRUCE_YOUNGS, SPRUCE_DENSITY = 11.0e9, 420.0
BRIDGE_BEAM = {"width": 0.025, "height": 0.030, "youngs": 12.0e9, "density": 700.0}  # maple
# Soundboard outline (m, body frame, counter-clockwise, convex) of about 1.5 m x 1.4 m.
OUTLINE = [
    (0.10, 0.00), (1.20, 0.00), (1.45, 0.35), (1.50, 0.75), (1.40, 1.15), (1.10, 1.40),
    (0.60, 1.35), (0.20, 1.10), (0.00, 0.70), (0.00, 0.25),
]  # fmt: skip
BASS_BRIDGE = [(0.15, 0.35), (0.25, 0.65), (0.35, 0.95)]
LONG_BRIDGE = [(0.30, 0.20), (0.55, 0.55), (0.62, 0.90), (0.50, 1.20)]


def keys() -> range:
    """The 88 keys."""
    return range(FIRST_KEY, LAST_KEY + 1)


def string_count(key: int) -> int:
    """Strings per key: 1, 2 or 3."""
    return 1 if key in SINGLE_KEYS else 2 if key in DOUBLE_KEYS else 3


def speaking_string(key: int) -> models.StringParams:
    """The speaking string of a key (all strings of a key are identical)."""
    length = common.sig(common.anchored(key, LENGTH_ANCHORS))
    f1 = common.equal_tempered(key)
    if key in WOUND_KEYS:
        d_core = common.sig(common.loglin(key, CORE_D[0][0], CORE_D[0][1], CORE_D[1][0], CORE_D[1][1]))
        target = common.lin(
            key, TARGET_TENSION[0][0], TARGET_TENSION[0][1], TARGET_TENSION[1][0], TARGET_TENSION[1][1]
        )
        d_outer = common.sig(
            common.wound_outer_diameter(
                length, f1, target, d_core, STEEL_DENSITY, STEEL_YOUNGS,
                COPPER_DENSITY, WINDING_PACKING, WINDING_EPS,
            )
        )  # fmt: skip
        return models.StringParams(
            L=length, dCore=d_core, rhoCore=STEEL_DENSITY, ECore=STEEL_YOUNGS, dOuter=d_outer,
            rhoWind=COPPER_DENSITY, packing=WINDING_PACKING, epsWind=WINDING_EPS, f1Target=f1,
            b1=LOSSES[0], b2=LOSSES[1], b3=LOSSES[2], sigmaOverride=[], detuneH=0.0,
        )  # fmt: skip
    d = common.sig(common.loglin(key, PLAIN_D[0][0], PLAIN_D[0][1], PLAIN_D[1][0], PLAIN_D[1][1]))
    return common.plain_string(length, d, STEEL_DENSITY, STEEL_YOUNGS, f1, LOSSES)


def hammer(key: int) -> models.HammerParams:
    """The Hunt-Crossley hammer of a key (SPEC-HAM)."""
    mass = common.sig(
        common.loglin(key, FIRST_KEY, HAMMER_MASS[0][1], LAST_KEY, HAMMER_MASS[1][1])
    )
    t_c = common.loglin(key, FIRST_KEY, HAMMER_CONTACT_TIME[0][1], LAST_KEY, HAMMER_CONTACT_TIME[1][1])
    k = common.sig(
        common.hammer_stiffness_for_contact_time(mass, HAMMER_P, HAMMER_SPEED, t_c), 6
    )
    width = common.sig(common.lin(key, FIRST_KEY, HAMMER_WIDTH[0][1], LAST_KEY, HAMMER_WIDTH[1][1]))
    if key <= STRIKE_BREAK:
        ratio = STRIKE_RATIO
    else:
        ratio = common.sig(common.lin(key, STRIKE_BREAK, STRIKE_RATIO, LAST_KEY, STRIKE_RATIO_TOP))
    return models.HammerParams(
        mass=mass, K=k, p=HAMMER_P, muHC=HAMMER_MU_HC, width=width, strikeRatio=ratio,
        stulovEps=0.0, stulovTau=0.0,
    )  # fmt: skip


def polyline_point(vertices: list[tuple[float, float]], fraction: float) -> tuple[float, float]:
    """The point at arc-length ``fraction`` (0 to 1) along a polyline."""
    lengths = [math.dist(a, b) for a, b in zip(vertices, vertices[1:], strict=False)]
    target = fraction * sum(lengths)
    for (a, b), length in zip(zip(vertices, vertices[1:], strict=False), lengths, strict=False):
        if target <= length or (a, b) == (vertices[-2], vertices[-1]):
            t = min(target / length, 1.0)
            return (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
        target -= length
    raise AssertionError("unreachable")


def bridge_point(key: int) -> models.BridgePointDesc:
    """One bridge point per key along the bass bridge (keys 21-40) or the long bridge (41-108)."""
    if key in BASS_KEYS:
        poly, i, n = BASS_BRIDGE, key - BASS_KEYS.start, len(BASS_KEYS)
    else:
        n = LAST_KEY - 41 + 1
        poly, i = LONG_BRIDGE, key - 41
    x, y = polyline_point(poly, i / (n - 1))
    return models.BridgePointDesc(
        position=models.Point(x=common.sig(x, 7), y=common.sig(y, 7), z=0.0),
        thetaB=0.0,
        conductanceH=BRIDGE_CONDUCTANCE,
    )


def ribs() -> list[models.RibDesc]:
    """RIB_COUNT spruce ribs across the grain, clipped to the (convex) outline."""
    g = (math.cos(GRAIN_ANGLE), math.sin(GRAIN_ANGLE))  # grain direction
    u = (-g[1], g[0])  # across the grain
    local = [(x * g[0] + y * g[1], x * u[0] + y * u[1]) for x, y in OUTLINE]
    t_min = min(t for t, _ in local)
    t_max = max(t for t, _ in local)
    out: list[models.RibDesc] = []
    for i in range(1, RIB_COUNT + 1):
        t = t_min + i * (t_max - t_min) / (RIB_COUNT + 1)
        hits = []
        for (t0, s0), (t1, s1) in zip(local, local[1:] + local[:1], strict=False):
            if (t0 - t) * (t1 - t) <= 0.0 and t0 != t1:
                hits.append(s0 + (t - t0) / (t1 - t0) * (s1 - s0))
        s_lo, s_hi = min(hits) + RIB_INSET, max(hits) - RIB_INSET
        start = (t * g[0] + s_lo * u[0], t * g[1] + s_lo * u[1])
        end = (t * g[0] + s_hi * u[0], t * g[1] + s_hi * u[1])
        out.append(
            models.RibDesc(
                start=models.Point(x=common.sig(start[0], 7), y=common.sig(start[1], 7), z=0.0),
                end=models.Point(x=common.sig(end[0], 7), y=common.sig(end[1], 7), z=0.0),
                width=RIB_WIDTH, height=RIB_HEIGHT, youngs=SPRUCE_YOUNGS, density=SPRUCE_DENSITY,
            )  # fmt: skip
        )
    return out


def body() -> models.BodyParams:
    """Nominal grand soundboard."""
    outline = [models.Point(x=x, y=y, z=0.0) for x, y in OUTLINE]
    bridges = [
        models.BridgeBeamDesc(path=[models.Point(x=x, y=y, z=0.0) for x, y in poly], **BRIDGE_BEAM)
        for poly in (BASS_BRIDGE, LONG_BRIDGE)
    ]
    plate = common.spruce_plate(
        1.5, 1.4, PLATE_THICKNESS, GRAIN_ANGLE, RIM_STIFFNESS, outline, ribs(), bridges
    )
    return models.BodyParams(source="PlateModel", plate=plate, modalTableHash="")


def build() -> models.InstrumentFile:
    """The instrument."""
    groups: list[models.NoteGroupDesc] = []
    for key in keys():
        n = string_count(key)
        damper = models.DamperParams(hasDamper=key <= LAST_DAMPED_KEY, **DAMPER)
        segments = [
            models.SegmentDesc(
                string=speaking_string(key),
                bridgePoint=key - FIRST_KEY,
                polarisationAngle=0.0,
                barriers=[],
                damper=damper,
                muteRatio=0.0,
                tensionModulation=False,
                kappaT=0.0,
                unisonDetuneCents=0.0,
            )
            for _ in range(n)
        ]
        groups.append(
            models.NoteGroupDesc(
                key=key,
                segments=segments,
                exciter=models.ExciterParams(e=hammer(key)),
                unaCorda=models.UnaCordaParams(leavingString=0 if n > 1 else -1),
            )
        )
    desc = models.InstrumentDesc(
        name="generic-grand-88",
        groups=groups,
        bridgePoints=[bridge_point(k) for k in keys()],
        body=body(),
        channels=[
            models.ChannelDesc(name=name, position=models.Point(x=x, y=y, z=z))
            for name, (x, y, z) in CHANNELS
        ],
        tuning=models.TuningCurve(stretchCents=[]),
    )
    return models.InstrumentFile(desc=desc, blobs=[])


def tension_report(file: models.InstrumentFile) -> dict[str, tuple[float, float, float]]:
    """Min and max tension (N) and mean inharmonicity B per region; asserts the 400-2000 N bound."""
    regions: dict[str, list[tuple[float, float]]] = {"wound 21-40": [], "plain 41-108": []}
    for g in file.desc.groups:
        s = g.segments[0].string
        t = common.tension(s)
        assert TENSION_BOUNDS[0] <= t <= TENSION_BOUNDS[1], f"key {g.key}: tension {t:.1f} N"
        regions["wound 21-40" if g.key in WOUND_KEYS else "plain 41-108"].append(
            (t, common.inharmonicity(s))
        )
    return {
        name: (min(t for t, _ in v), max(t for t, _ in v), sum(b for _, b in v) / len(v))
        for name, v in regions.items()
    }


def main(argv: list[str] | None = None) -> int:
    """Write the instrument to ``--out``/instrument.json and print the tension report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    args = parser.parse_args(argv)
    file = build()
    for name, (lo, hi, b) in tension_report(file).items():
        print(f"{name}: T {lo:.1f} to {hi:.1f} N, mean B {b:.3e}")
    print(f"wrote {common.write_example(file, args.out)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
