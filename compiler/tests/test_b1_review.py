# SPDX-License-Identifier: MIT
"""B1 reviewer tests: independent recomputation of the generic-grand-88 and test-monochord values.

Nothing here imports the example generators or ``examples.common``: the anchors are copied from the
B1 brief, and the tension and contact-time formulas are written out again from SPEC-STR "Tension
for pitch" and "Wound strings" and from the SPEC-HAM oracle (by numerical integration of the
contact, not by the closed form the generator uses).
"""

from __future__ import annotations

import itertools
import math
from pathlib import Path

from pme_compiler.schema import load, models

EXAMPLES = Path(__file__).resolve().parents[2] / "instruments" / "examples"

# Speaking length anchors, key -> metres (B1 brief, generic-grand-88).
ANCHORS = {
    21: 2.00,
    28: 1.85,
    29: 1.83,
    40: 1.50,
    41: 1.45,
    48: 1.15,
    60: 0.64,
    72: 0.33,
    84: 0.17,
    96: 0.090,
    108: 0.052,
}


def _length(key: int) -> float:
    keys = sorted(ANCHORS)
    for lo, hi in itertools.pairwise(keys):
        if lo <= key <= hi:
            t = (key - lo) / (hi - lo)
            return math.exp(
                math.log(ANCHORS[lo]) + t * (math.log(ANCHORS[hi]) - math.log(ANCHORS[lo]))
            )
    raise AssertionError(key)


def _tension(s: models.StringParams, f1: float) -> float:
    """SPEC-STR: T = 4 L^2 rho A f1^2 - pi^2 EI / L^2 with the wound-string rho A and EI."""
    rho_a = (
        math.pi / 4 * (s.rhoCore * s.dCore**2 + s.packing * s.rhoWind * (s.dOuter**2 - s.dCore**2))
    )
    ei = (1 + s.epsWind) * math.pi * s.ECore * s.dCore**4 / 64
    return 4 * s.L**2 * rho_a * f1**2 - math.pi**2 * ei / s.L**2


def _hammer(exciter: models.ExciterParams | None) -> models.HammerParams:
    assert exciter is not None
    assert isinstance(exciter.e, models.HammerParams)
    return exciter.e


def _contact_time(m: float, k: float, p: float, v: float) -> float:
    """Contact time of a rigid felt K[delta]_+^p by energy-conservation quadrature."""
    delta_m = ((p + 1) * m * v * v / (2 * k)) ** (1 / (p + 1))
    n = 20000
    total = 0.0
    for i in range(n):
        u = (i + 0.5) / n
        x = delta_m * (1 - u * u)
        speed = math.sqrt(v * v - 2 * k * x ** (p + 1) / ((p + 1) * m))
        total += 2 * delta_m * u / speed / n
    return float(2 * total)


def test_b1_r6_review_piano_strings_match_the_brief_anchors() -> None:
    """B1-R6: lengths, equal-tempered f1 and closed-form tensions recomputed from the brief."""
    inst = load(EXAMPLES / "generic-grand-88" / "instrument.json").desc
    by_key = {g.key: g for g in inst.groups}
    assert sorted(by_key) == list(range(21, 109))
    for key in (21, 40, 41, 60, 69, 108):
        group = by_key[key]
        expected_strings = 1 if key <= 28 else 2 if key <= 40 else 3
        assert len(group.segments) == expected_strings
        for seg in group.segments:
            s = seg.string
            f1 = 440.0 * 2 ** ((key - 69) / 12)
            assert math.isclose(s.L, _length(key), rel_tol=1e-9)
            assert math.isclose(s.f1Target, f1, rel_tol=1e-8)
            assert 400.0 <= _tension(s, f1) <= 2000.0
            assert seg.unisonDetuneCents == 0.0
            assert s.boundary == "Pinned"
    # Wound bass: target tension linear from 1600 N (key 21) to 850 N (key 40).
    for key, target in ((21, 1600.0), (40, 850.0)):
        s = by_key[key].segments[0].string
        f1 = 440.0 * 2 ** ((key - 69) / 12)
        assert math.isclose(_tension(s, f1), target, rel_tol=1e-6)
        assert s.dOuter > s.dCore and s.rhoWind == 8960.0 and s.packing == 0.85
    # Plain steel: d log-linear 1.20 mm (41) to 0.80 mm (108); key 41 is the first anchor.
    s41 = by_key[41].segments[0].string
    s108 = by_key[108].segments[0].string
    assert math.isclose(s41.dCore, 1.20e-3, rel_tol=1e-9) and s41.dOuter == s41.dCore
    assert math.isclose(s108.dCore, 0.80e-3, rel_tol=1e-9) and s108.dOuter == s108.dCore


def test_b1_r6_review_piano_dampers_cover_keys_21_to_88_only() -> None:
    """B1-R6: hasDamper is true on keys 21-88 and false on 89-108 (SPEC-DMP Undamped notes)."""
    inst = load(EXAMPLES / "generic-grand-88" / "instrument.json").desc
    for g in inst.groups:
        for seg in g.segments:
            assert seg.damper is not None
            assert seg.damper.hasDamper == (g.key <= 88)


def test_b1_r6_review_hammer_contact_times_by_quadrature() -> None:
    """B1-R6: contact time at 3 m/s by quadrature: 4 ms at key 21, 0.5 ms at key 108."""
    inst = load(EXAMPLES / "generic-grand-88" / "instrument.json").desc
    by_key = {g.key: g for g in inst.groups}
    for key, expected in ((21, 4.0e-3), (108, 0.5e-3)):
        h = _hammer(by_key[key].exciter)
        assert math.isclose(_contact_time(h.mass, h.K, h.p, 3.0), expected, rel_tol=1e-4)
    # Log-linear in key: the middle key lies between the ends and decreases monotonically.
    times = []
    for key in range(21, 109):
        h = _hammer(by_key[key].exciter)
        times.append(_contact_time(h.mass, h.K, h.p, 3.0))
    assert all(a > b for a, b in itertools.pairwise(times))
    m60 = _hammer(by_key[60].exciter)
    t60 = _contact_time(m60.mass, m60.K, m60.p, 3.0)
    expected60 = 4.0e-3 * (0.5e-3 / 4.0e-3) ** ((60 - 21) / (108 - 21))
    assert math.isclose(t60, expected60, rel_tol=1e-3)


def test_b1_r6_review_monochord_and_pair_match_the_brief() -> None:
    """B1-R6: test-monochord and test-unison-pair values, hammer contact time 2 ms at 3 m/s."""
    for name, key, length, diameter, count in (
        ("test-monochord", 45, 1.0, 1.0e-3, 1),
        ("test-unison-pair", 60, 0.64, 1.05e-3, 2),
    ):
        inst = load(EXAMPLES / name / "instrument.json").desc
        assert inst.name == name and len(inst.groups) == 1
        g = inst.groups[0]
        assert g.key == key and len(g.segments) == count
        for seg in g.segments:
            s = seg.string
            assert (s.L, s.dCore, s.dOuter) == (length, diameter, diameter)
            assert (s.rhoCore, s.ECore, s.b1, s.b2, s.b3) == (7850.0, 200.0e9, 0.5, 0.0, 2.0e-9)
            assert seg.damper is None and seg.bridgePoint == 0
            assert seg.unisonDetuneCents == 0.0
        h = _hammer(g.exciter)

        assert (h.mass, h.p, h.muHC, h.width, h.strikeRatio) == (8.0e-3, 2.5, 0.2, 0.01, 0.125)
        assert math.isclose(_contact_time(h.mass, h.K, h.p, 3.0), 2.0e-3, rel_tol=1e-4)
        plate = inst.body.plate
        assert inst.body.source == "PlateModel"
        assert (plate.lengthX, plate.lengthY, plate.thickness) == (0.6, 0.4, 0.006)
        assert plate.outline == [] and plate.thicknessGrid == [] and plate.ribs == []
        assert plate.bridges == [] and plate.rimStiffness == 0.0
        assert len(inst.bridgePoints) == 1 and inst.bridgePoints[0].conductanceH == 1.0e-3
        assert [c.name for c in inst.channels] == ["mono"]
