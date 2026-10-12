# SPDX-License-Identifier: MIT
"""B2-R2 material library: every entry records its source and loads into PlateParams."""

from __future__ import annotations

import copy
import dataclasses
import warnings

import pytest
from test_b2_common import p05_plate

from pme_compiler.body import compile as plate_compile
from pme_compiler.body import geometry
from pme_compiler.materials import library


def test_b2_r2_every_entry_has_a_complete_source() -> None:
    """B2-R2: every entry has a complete source."""
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
    """B2-R2: entry without a source or derivation is rejected."""
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
    """B2-R2: entries load into plate params and compile geometry."""
    lib = library.load_library()
    assert lib
    base = p05_plate()
    for name, mat in lib.items():
        assert mat.kind in ("tonewood", "test-reference")
        plate = library.apply(mat, base, loss_factor=0.01 if mat.kind == "tonewood" else None)
        assert plate.youngsL == mat.constants["youngsL"]
        assert plate.dEdMcL == mat.slopes["dEdMcL"]
        assert plate.lengthX == base.lengthX and plate.rimStiffness == base.rimStiffness
        geometry.from_plate(plate)  # valid wood data
        assert name == mat.name


def test_b2_r2_reference_entry_matches_the_p0_5_plate() -> None:
    """B2-R2: reference entry matches the p0 5 plate."""
    ref = library.load_library()["p0_5_reference_plate"]
    plate = library.apply(ref, p05_plate(youngsL=1.0))
    p = p05_plate()
    for f in ("youngsL", "youngsR", "shearLR", "poissonLR", "poissonRL", "density"):
        assert getattr(plate, f) == pytest.approx(getattr(p, f))


def test_b2_r2_omissions_are_listed_and_the_reference_moisture_agrees() -> None:
    """B2-R2: omissions are listed and the reference moisture agrees."""
    assert library.omissions()
    assert library.load_raw()["mcRef"] == geometry.MC_REF == 0.12


def test_b2_r2_loss_factor_table_is_interpolated_in_log_frequency() -> None:
    """B2-R2: loss factor table is interpolated in log frequency."""
    ref = library.load_library()["p0_5_reference_plate"]
    m = dataclasses.replace(ref, loss_table=((100.0, 0.01), (10000.0, 0.03)))
    assert m.loss_factor(50.0) == 0.01
    assert m.loss_factor(1000.0) == pytest.approx(0.02)
    assert m.loss_factor(1e6) == 0.03
    assert ref.loss_factor(123.0) == ref.constants["lossFactor"]


# --- Wood Handbook tonewood entries (FPL-GTR-282, 2021) -------------------------------------------

TONEWOODS = (
    "sitka_spruce",
    "engelmann_spruce",
    "western_redcedar",
    "sugar_maple",
    "african_mahogany",
    "american_mahogany",
)
RHO_W = 1000.0


def _entries() -> dict[str, dict]:  # type: ignore[type-arg]
    return {e["name"]: e for e in library.load_raw()["materials"] if e["kind"] == "tonewood"}


def test_b2_r2_tonewood_entries_present_and_validate() -> None:
    """B2-R2: the Wood Handbook tonewoods are present and every entry validates."""
    entries = _entries()
    assert set(entries) == set(TONEWOODS)
    for e in entries.values():
        assert library.validate_entry(e) == []
        src = e["source"]
        assert "FPL-GTR-282" in src["title"] and src["year"] == 2021
        assert "Public domain" in src["licence"]
        for f in (*library.CONSTANT_FIELDS, *library.SLOPE_FIELDS):
            assert src["locators"][f], f"{e['name']}: no locator for {f}"
        # Printed and PDF page recorded for every sourced field's locator.
        for f in (
            "youngsL",
            "youngsR",
            "shearLR",
            "poissonLR",
            "poissonRL",
            "density",
            "dEdMcL",
            "dRhodMc",
        ):
            assert "printed p." in src["locators"][f] or "printed pp." in src["locators"][f]
            assert "PDF p" in src["locators"][f]
    lib = library.load_library()
    for name in TONEWOODS:
        assert lib[name].kind == "tonewood"
        assert "lossFactor" not in lib[name].constants
        assert lib[name].unsourced_slopes == ("dEdMcR", "dGdMc")


def test_b2_r2_tonewood_values_reproduced_from_raw_table_numbers() -> None:
    """B2-R2: each stored value is reproduced by its stated derivation from the raw numbers."""
    for name, e in _entries().items():
        raw = e["source"]["raw"]
        c = e["constants"]
        sl = e["moistureSlopes"]
        e12, eg, mp = raw["EbendingMPa12"], raw["EbendingMPaGreen"], raw["MpPercent"]
        el = raw["shearDeflectionFactor"] * e12  # MPa; handbook's 10 % shear-deflection correction
        assert raw["shearDeflectionFactor"] == 1.10
        assert c["youngsL"] == pytest.approx(el * 1e6, rel=1e-12), name
        assert c["youngsR"] == pytest.approx(raw["ratios"]["ER"] * el * 1e6, rel=1e-12), name
        assert c["shearLR"] == pytest.approx(raw["ratios"]["GLR"] * el * 1e6, rel=1e-12), name
        assert c["poissonLR"] == raw["poissonLR"]
        if raw["poissonRL"] is not None:
            assert c["poissonRL"] == raw["poissonRL"]
        else:  # SPEC-free: handbook Eq. 5-1 reciprocity
            assert c["poissonRL"] == pytest.approx(
                raw["poissonLR"] * raw["ratios"]["ER"], rel=1e-12
            )
        s0, mc_fs, gb = (
            raw["volumetricShrinkagePercent"],
            raw["fiberSaturationPercent"],
            raw["basicSpecificGravity"],
        )
        if raw["specificGravity12"] is not None:
            rho12 = RHO_W * raw["specificGravity12"] * 1.12
        else:
            s12 = s0 * (1 - 12 / mc_fs)  # Eq. 4-9
            rho12 = RHO_W * gb / (1 - s12 / 100) * 1.12  # Eqs. 4-11, 4-14
        assert c["density"] == pytest.approx(rho12, rel=1e-12), name

        # Eq. 5-3 slope: finite difference of P(M) agrees with the stored analytic derivative.
        def p_of(m: float, e12: float = e12, eg: float = eg, mp: float = mp) -> float:
            return float(1.10 * e12 * (e12 / eg) ** ((12 - m) / (mp - 12)))

        h = 1e-4
        fd = (p_of(12 + h) - p_of(12 - h)) / (2 * h)  # MPa per %MC
        assert sl["dEdMcL"] == pytest.approx(fd * 100 * 1e6, rel=1e-6), name

        # Eqs. 4-9/4-11/4-14 slope.
        def rho_of(m: float, gb: float = gb, s0: float = s0, mc_fs: float = mc_fs) -> float:
            return RHO_W * gb * (1 + m / 100) / (1 - s0 / 100 * (1 - m / mc_fs))

        fd_rho = (rho_of(12 + h) - rho_of(12 - h)) / (2 * h)
        assert sl["dRhodMc"] == pytest.approx(fd_rho * 100, rel=1e-6), name
        assert sl["dEdMcR"] is None and sl["dGdMc"] is None and c["lossFactor"] is None


def test_b2_r2_tonewood_spot_values_match_the_handbook_tables() -> None:
    """B2-R2: spot checks of raw numbers against the handbook tables as read (Sitka spruce)."""
    sitka = _entries()["sitka_spruce"]["source"]["raw"]
    assert sitka["EbendingMPa12"] == 10800.0 and sitka["EbendingMPaGreen"] == 8500.0  # Table 5-3a
    assert sitka["specificGravity12"] == 0.40 and sitka["basicSpecificGravity"] == 0.37
    assert sitka["ratios"]["ER"] == 0.078 and sitka["ratios"]["GLR"] == 0.064  # Table 5-1
    assert sitka["poissonLR"] == 0.372 and sitka["poissonRL"] == 0.040  # Table 5-2
    assert (
        sitka["MpPercent"] == 27.0 and sitka["volumetricShrinkagePercent"] == 11.5
    )  # Tables 5-13, 4-3
    lib = library.load_library()["sitka_spruce"]
    assert lib.constants["density"] == pytest.approx(448.0)
    assert lib.constants["youngsL"] == pytest.approx(11.88e9)


def test_b2_r2_apply_on_a_spruce_top_needs_an_explicit_loss_factor() -> None:
    """B2-R2: apply() works on a spruce top; the loss factor must be supplied."""
    spruce = library.load_library()["sitka_spruce"]
    base = p05_plate()
    with pytest.raises(ValueError, match="loss"):
        library.apply(spruce, base)
    with pytest.raises(ValueError, match="loss"):
        spruce.loss_factor(100.0)
    plate = library.apply(spruce, base, loss_factor=0.012)
    assert plate.lossFactor == 0.012
    assert plate.youngsL == spruce.constants["youngsL"]
    assert plate.density == spruce.constants["density"]
    assert plate.dEdMcL == spruce.slopes["dEdMcL"] < 0.0
    assert plate.dEdMcR == 0.0 and plate.dGdMc == 0.0  # unsourced: zero
    assert plate.dRhodMc == spruce.slopes["dRhodMc"] > 0.0
    assert plate.lengthX == base.lengthX
    geometry.from_plate(plate)
    # Wood moves the right way with moisture over a plausible range (E_L falls, rho rises).
    dry = geometry.effective_material(plate)
    wet = geometry.effective_material(dataclasses.replace(plate, moisture=0.20))
    assert wet.rho > dry.rho and wet.E_L < dry.E_L
    assert wet.E_R == dry.E_R and wet.G_LR == dry.G_LR  # unsourced slopes: zero
    assert dry.E_L == pytest.approx(spruce.constants["youngsL"])  # MC = MC_ref


def test_b2_r2_missing_species_are_listed_as_omissions() -> None:
    """B2-R2: Norway spruce, Indian rosewood, ebony and the loss factor are listed as omissions."""
    text = " ".join(library.omissions())
    for key in ("Norway", "Indian rosewood", "Ebony", "Loss factor", "dE_R/dMC"):
        assert key in text
    assert "norway_spruce" not in _entries()


def test_b2_r2_unsourced_slopes_warn_when_moisture_differs_from_reference() -> None:
    """B2-R2: apply() warns, naming material and slopes, when MC differs and slopes are null."""
    spruce = library.load_library()["sitka_spruce"]
    wet = dataclasses.replace(p05_plate(), moisture=0.18)
    with pytest.warns(library.UnsourcedMoistureSlopeWarning) as rec:
        plate = library.apply(spruce, wet, loss_factor=0.012)
    msg = str(rec[0].message)
    assert "sitka_spruce" in msg
    for f in spruce.unsourced_slopes:
        assert f in msg
    assert plate.dEdMcR == 0.0 and plate.dGdMc == 0.0


def test_b2_r2_no_unsourced_slope_warning_at_reference_moisture() -> None:
    """B2-R2: no warning at MC_ref, or when the entry sources every slope."""
    lib = library.load_library()
    base = p05_plate()
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        library.apply(lib["sitka_spruce"], base, loss_factor=0.012)
        sourced = dataclasses.replace(lib["sitka_spruce"], unsourced_slopes=())
        library.apply(sourced, dataclasses.replace(base, moisture=0.2), loss_factor=0.012)


def test_b2_r2_apply_validates_the_loss_factor() -> None:
    """B2-R2: apply() rejects a non-finite or non-positive explicit loss factor."""
    spruce = library.load_library()["sitka_spruce"]
    for bad in (0.0, -0.01, float("nan"), float("inf")):
        with pytest.raises(ValueError, match="loss_factor"):
            library.apply(spruce, p05_plate(), loss_factor=bad)


def test_b2_r2_compile_notes_zero_slopes_when_moisture_differs() -> None:
    """B2-R2: CompileInfo.notes reports zero moisture slopes when moisture != MC_ref."""
    base = p05_plate()
    dry = plate_compile.compile_plate(base, [], mesh_size=0.1, grid_order=0)
    assert dry.info.notes == ()
    wet = dataclasses.replace(base, moisture=0.2, dEdMcR=0.0)
    notes = plate_compile.compile_plate(wet, [], mesh_size=0.1, grid_order=0).info.notes
    assert len(notes) == 1 and "dEdMcR" in notes[0]
