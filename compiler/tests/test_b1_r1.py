# SPDX-License-Identifier: MIT
"""B1-R1: the JSON Schema mirrors the description structs and records SI units."""

from __future__ import annotations

import dataclasses
from pathlib import Path
from typing import Any

import pytest
from jsonschema import Draft202012Validator

from pme_compiler.schema import models, schema
from pme_compiler.schema.fields import BY_NAME, EXCITER_ALTERNATIVES, STRUCTS, UNITS
from pme_compiler.schema.generate import main as generate_main
from pme_compiler.schema.loader import load

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = sorted((REPO / "instruments" / "examples").glob("*/instrument.json"))

STRUCT_NAMES = [
    "StringParams", "SegmentDesc", "NoteGroupDesc", "HammerParams", "UnaCordaParams", "PluckPulse",
    "Plectrum", "Finger", "InitialDisplacement", "ExciterParams", "DamperParams", "BarrierParams",
    "BridgePointDesc", "Point", "RibDesc", "BridgeBeamDesc", "PlateParams", "BodyParams",
    "ChannelDesc", "TuningCurve", "Defaults", "InstrumentDesc", "BlobEntry", "BlobArray",
]  # fmt: skip


def _is_numeric(prop: dict[str, Any]) -> bool:
    if prop.get("type") in ("number", "integer"):
        return True
    items = prop.get("items")
    return (
        prop.get("type") == "array"
        and isinstance(items, dict)
        and items.get("type") in ("number", "integer")
    )


def test_b1_r1_schema_is_valid_draft_2020_12() -> None:
    """B1-R1: the schema is itself valid draft 2020-12."""
    Draft202012Validator.check_schema(schema())
    assert schema()["$schema"] == "https://json-schema.org/draft/2020-12/schema"


def test_b1_r1_has_one_def_per_struct() -> None:
    """B1-R1: one $defs entry per struct of the brief."""
    defs = schema()["$defs"]
    for name in STRUCT_NAMES:
        assert name in defs, name


def test_b1_r1_every_numeric_property_has_a_unit_from_the_vocabulary() -> None:
    """B1-R1: x-unit on every numeric property and number array, from the closed vocabulary."""
    count = 0
    for name, definition in schema()["$defs"].items():
        for key, prop in definition["properties"].items():
            if _is_numeric(prop):
                assert prop.get("x-unit") in UNITS, f"{name}.{key}"
                count += 1
            assert prop["description"], f"{name}.{key}"
    assert count > 100
    assert schema()["properties"]["schemaVersion"]["x-unit"] in UNITS


def test_b1_r1_descriptions_cite_a_spec_key() -> None:
    """B1-R1: every numeric property's description cites a SPEC key (directly or via its struct)."""
    for name, definition in schema()["$defs"].items():
        assert "(SPEC-" in definition["description"], name
        for key, prop in definition["properties"].items():
            if _is_numeric(prop):
                assert "SPEC-" in prop["description"] or "SPEC-" in definition["description"], (
                    f"{name}.{key}"
                )


def test_b1_r1_properties_equal_dataclass_fields() -> None:
    """B1-R1: the properties of each $defs entry equal the dataclass field names, in order."""
    defs = schema()["$defs"]
    for struct in STRUCTS:
        properties = list(defs[struct.name]["properties"])
        if struct.extra.get("variant"):
            assert properties == [key for key, _ in EXCITER_ALTERNATIVES]
            continue
        fields = [f.name for f in dataclasses.fields(getattr(models, struct.name))]
        assert properties == fields, struct.name
        assert properties == [f.name for f in BY_NAME[struct.name].fields]


def test_b1_r1_required_members_are_those_without_default() -> None:
    """B1-R1: members with a default initialiser in the header are optional; the rest required."""
    defs = schema()["$defs"]
    assert "longitudinalModes" not in defs["SegmentDesc"]["required"]
    assert "kappaL" not in defs["SegmentDesc"]["required"]
    assert "unaCorda" not in defs["NoteGroupDesc"]["required"]
    assert "exciter" not in defs["NoteGroupDesc"]["required"]
    assert "damper" not in defs["SegmentDesc"]["required"]
    assert "defaults" not in defs["InstrumentDesc"]["required"]
    assert "tau0" not in defs["DamperParams"]["required"]
    assert "referencePitch" not in defs["TuningCurve"]["required"]
    assert {"L", "dCore", "f1Target", "sigmaOverride"} <= set(defs["StringParams"]["required"])
    assert defs["StringParams"]["additionalProperties"] is False


@pytest.mark.parametrize("path", EXAMPLES, ids=lambda p: p.parent.name)
def test_b1_r1_example_instruments_validate(path: Path) -> None:
    """B1-R1: every example validates against the schema and loads."""
    assert load(path).desc.name == path.parent.name


def test_b1_r1_defaults_match_the_header() -> None:
    """B1-R1: model defaults equal the member initialisers of the frozen headers."""
    seg = models.SegmentDesc.__dataclass_fields__
    assert seg["longitudinalModes"].default == 8
    assert seg["kappaL"].default == 0.026
    assert models.TuningCurve.__dataclass_fields__["referencePitch"].default == 440.0
    d = models.Defaults()
    assert (d.halfPedalP0, d.halfPedalP1, d.sympatheticGain) == (0.35, 0.65, 1.0)
    assert (d.sympatheticLowPass, d.localBridgeModes, d.localBridgeFSplit) == (5000.0, 64, 2000.0)
    u = models.UnaCordaParams()
    assert (u.s0, u.s1, u.kappaUC, u.deltaPUC, u.rhoUpright, u.leavingString) == (
        0.3, 0.9, 0.6, 0.0, 0.3, 0
    )  # fmt: skip
    k = models.DamperParams(xRatio=0.1, width=0.01, Kd=1.0, Rd=1.0)
    assert (k.tau0, k.kRelease, k.tauUp, k.etaD, k.hasDamper, k.mode) == (
        0.012, 2.0, 0.004, 2.0, True, "Diagonal"
    )  # fmt: skip
    h = models.HammerParams(
        mass=1, K=1, p=1, muHC=0, width=1, strikeRatio=0.1, stulovEps=0, stulovTau=0
    )
    assert (h.law, h.vMin, h.vMax, h.gammaV, h.dCheck) == ("HuntCrossley", 0.3, 6.0, 1.5, 0.005)


def test_b1_r1_generated_files_are_current() -> None:
    """B1-R1: the schema, the models and the C++ reader/writer are the generator's output."""
    assert generate_main(["--check"]) == 0
