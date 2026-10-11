# SPDX-License-Identifier: MIT
"""B2-R2 material library: every entry records its source and loads into PlateParams."""

from __future__ import annotations

import copy

import pytest
from test_b2_common import p05_plate

from pme_compiler.body import geometry
from pme_compiler.materials import library


def test_b2_r2_every_entry_has_a_complete_source() -> None:
    doc = library.load_raw()
    assert doc["materials"], "the library must not be empty"
    for entry in doc["materials"]:
        assert library.validate_entry(entry) == []
        src = entry["source"]
        for f in library.SOURCE_FIELDS:
            assert src[f]
        for f in (*library.CONSTANT_FIELDS, "moistureSlopes"):
            assert src["derivation"][f], f"{entry['name']}: no derivation for {f}"


def test_b2_r2_entry_without_a_source_or_derivation_is_rejected() -> None:
    entry = copy.deepcopy(library.load_raw()["materials"][0])
    del entry["source"]
    assert any("no source" in p for p in library.validate_entry(entry))
    entry = copy.deepcopy(library.load_raw()["materials"][0])
    entry["source"]["derivation"].pop("youngsR")
    assert any("derivation.youngsR" in p for p in library.validate_entry(entry))
    entry = copy.deepcopy(library.load_raw()["materials"][0])
    entry["source"]["author"] = ""
    assert any("source.author" in p for p in library.validate_entry(entry))
    entry = copy.deepcopy(library.load_raw()["materials"][0])
    entry["constants"]["density"] = -1.0
    assert any("constants.density" in p for p in library.validate_entry(entry))


def test_b2_r2_entries_load_into_plate_params_and_compile_geometry() -> None:
    lib = library.load_library()
    assert lib
    base = p05_plate()
    for name, mat in lib.items():
        assert mat.kind in ("tonewood", "test-reference")
        plate = library.apply(mat, base)
        assert plate.youngsL == mat.constants["youngsL"]
        assert plate.dEdMcL == mat.slopes["dEdMcL"]
        assert plate.lengthX == base.lengthX and plate.rimStiffness == base.rimStiffness
        geometry.from_plate(plate)  # valid wood data
        assert name == mat.name


def test_b2_r2_reference_entry_matches_the_p0_5_plate() -> None:
    ref = library.load_library()["p0_5_reference_plate"]
    plate = library.apply(ref, p05_plate(youngsL=1.0))
    p = p05_plate()
    for f in ("youngsL", "youngsR", "shearLR", "poissonLR", "poissonRL", "density"):
        assert getattr(plate, f) == pytest.approx(getattr(p, f))


def test_b2_r2_omissions_are_listed_and_the_reference_moisture_agrees() -> None:
    assert library.omissions()
    assert library.load_raw()["mcRef"] == geometry.MC_REF == 0.12


def test_b2_r2_loss_factor_table_is_interpolated_in_log_frequency() -> None:
    ref = library.load_library()["p0_5_reference_plate"]
    import dataclasses

    m = dataclasses.replace(ref, loss_table=((100.0, 0.01), (10000.0, 0.03)))
    assert m.loss_factor(50.0) == 0.01
    assert m.loss_factor(1000.0) == pytest.approx(0.02)
    assert m.loss_factor(1e6) == 0.03
    assert ref.loss_factor(123.0) == ref.constants["lossFactor"]
