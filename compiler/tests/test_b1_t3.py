# SPDX-License-Identifier: MIT
"""B1-T3 and B1-R2: version 0 fixtures migrate to the current version."""

from __future__ import annotations

import json
from pathlib import Path

from pme_compiler.schema import CURRENT_VERSION, dumps, load, try_load, try_loads

DATA = Path(__file__).resolve().parent / "data" / "b1"
V0 = DATA / "v0"


def test_b1_t3_piano_v0_migrates_to_the_expected_file() -> None:
    """B1-T3: the v0 piano loads, reports source version 0 and writes the expected v1 file."""
    migrated = load(V0 / "piano-v0.json")
    assert migrated.source_version == 0
    assert dumps(migrated) == (V0 / "piano-v0.expected.json").read_text(encoding="utf-8")
    again = load(V0 / "piano-v0.expected.json")
    assert again.source_version == CURRENT_VERSION
    assert again.desc == migrated.desc


def test_b1_t3_migration_steps() -> None:
    """B1-R2: unison detune 0, peakForce renamed with strengthGamma 1, new plate members empty."""
    migrated = load(V0 / "piano-v0.json")
    pulse = migrated.desc.groups[2].exciter
    assert pulse is not None
    assert pulse.e.peakForceMax == 5.0  # type: ignore[union-attr]
    assert pulse.e.strengthGamma == 1.0  # type: ignore[union-attr]
    assert all(s.unisonDetuneCents == 0.0 for g in migrated.desc.groups for s in g.segments)
    plate = migrated.desc.body.plate
    assert plate.outline == [] and plate.thicknessGrid == [] and plate.bridges == []
    assert plate.thicknessNx == 0 and plate.rimRotationalStiffness == 0.0


def test_b1_t3_plectrum_and_finger_need_new_members() -> None:
    """B1-T3: v0 plectrum and finger fail with `migration` at the exciter's path."""
    expected = json.loads((V0 / "expected.json").read_text())
    for name, pairs in expected.items():
        result, errors = try_load(V0 / name)
        assert result is None
        assert sorted((e.path, e.code) for e in errors) == sorted(tuple(p) for p in pairs)


def test_b1_t3_supplied_members_make_a_v0_plectrum_migrate() -> None:
    """B1-R2: a v0 plectrum that already carries position and angle migrates."""
    doc = json.loads((V0 / "plectrum-v0.json").read_text())
    doc["instrument"]["groups"][2]["exciter"]["plectrum"].update(position=0.2, angle=0.0)
    result, errors = try_loads(json.dumps(doc))
    assert errors == []
    assert result is not None
    plectrum = result.desc.groups[2].exciter
    assert plectrum is not None
    assert plectrum.e.driverSpeedMax == 0.5  # type: ignore[union-attr]
    assert plectrum.e.strengthGamma == 1.0  # type: ignore[union-attr]


def test_b1_t3_future_version_is_an_error() -> None:
    """B1-T3: schemaVersion 2 fails with `version`; the file is not migrated."""
    _, errors = try_load(DATA / "malformed" / "version-future.json")
    assert [(e.path, e.code) for e in errors] == [("/schemaVersion", "version")]


def test_b1_r2_writers_always_write_the_current_version() -> None:
    """B1-R2: dumps writes the current version whatever the source version was."""
    text = dumps(load(V0 / "piano-v0.json"))
    assert json.loads(text)["schemaVersion"] == CURRENT_VERSION
