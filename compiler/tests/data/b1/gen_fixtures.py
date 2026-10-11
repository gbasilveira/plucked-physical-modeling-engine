# SPDX-License-Identifier: MIT
"""Regenerate the shared B1 fixtures (read by the Python and C++ tests).

    uv run python compiler/tests/data/b1/gen_fixtures.py          # rewrite the files
    uv run python compiler/tests/data/b1/gen_fixtures.py --check  # fail if a file differs

Writes next to this script: ``all-fields.json``, ``body-measured.json``, ``numbers.json``,
``blobs/modal-small.bin``, ``blobs/modal-second.bin``, ``malformed/*`` (with ``expected.json``) and
``v0/*``. Every value is nominal test data.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import random
import struct
import sys
from pathlib import Path
from typing import Any

from pme_compiler.examples import small
from pme_compiler.schema import models
from pme_compiler.schema.canonical import dumps_document
from pme_compiler.schema.codec import file_to_json
from pme_compiler.schema.fields import BY_NAME, ENUMS, REQUIRED, Field
from pme_compiler.schema.loader import dumps

HERE = Path(__file__).resolve().parent


# --- binary blobs --------------------------------------------------------------------------------


def modal_small_bytes() -> bytes:
    """96 bytes of float64 (4 x 3), 40 bytes of float32 (10), 32 bytes of uint32 (8)."""
    out = b"".join(struct.pack("<d", 100.0 + i * 1.5) for i in range(12))
    out += b"".join(struct.pack("<f", 0.25 * i) for i in range(10))
    out += b"".join(struct.pack("<I", 7 * i) for i in range(8))
    return out


def modal_second_bytes() -> bytes:
    """32 bytes of int32 (2 x 4)."""
    return b"".join(struct.pack("<i", i - 3) for i in range(8))


def sha(data: bytes) -> str:
    return "sha256:" + hashlib.sha256(data).hexdigest()


def small_manifest(path: str = "blobs/modal-small.bin") -> models.BlobEntry:
    return models.BlobEntry(
        hash=sha(modal_small_bytes()),
        path=path,
        producer=models.BlobProducer(tool="pme-plate-compiler", version="0.0.1"),
        arrays=[
            models.BlobArray(name="omega", dtype="float64", shape=[4, 3], unit="rad", offset=0),
            models.BlobArray(name="phi", dtype="float32", shape=[10], unit="1", offset=96),
            models.BlobArray(name="index", dtype="uint32", shape=[8], unit="index", offset=136),
        ],
    )


def second_manifest(path: str = "blobs/modal-second.bin") -> models.BlobEntry:
    return models.BlobEntry(
        hash=sha(modal_second_bytes()),
        path=path,
        producer=models.BlobProducer(tool="pme-fit", version="1.2.3"),
        arrays=[
            models.BlobArray(name="shifts", dtype="int32", shape=[2, 4], unit="index", offset=0)
        ],
    )


# --- all-fields ----------------------------------------------------------------------------------


def _number(f: Field, seed: int) -> float:
    lo, hi = f.lo, f.hi
    if lo is not None and hi is not None:
        v = lo + (hi - lo) * (0.31 + 0.07 * (seed % 5))
    elif lo is not None:
        v = lo + 1.25 + 0.5 * (seed % 7)
    elif hi is not None:
        v = hi - 1.25 - 0.5 * (seed % 7)
    else:
        v = -0.75 + 0.5 * (seed % 9)
    v = float(f"{v:.6g}")
    while f.default == v:
        v += 0.0625
    return v


def _value(f: Field, seed: int) -> Any:
    kind = f.kind
    if kind == "num":
        return _number(f, seed)
    if kind == "int":
        if f.one_of:
            return -1 if f.default != -1 else 1
        v = 3 + seed % 4
        while f.default == v:
            v += 1
        return v
    if kind == "bool":
        return f.default is not True if f.default is not REQUIRED else True
    if kind == "str":
        return f"all-fields {f.name} {seed}"
    if kind == "enum":
        values = [v for v in ENUMS[f.ref] if v != f.default]
        return values[seed % len(values)]
    if kind == "numarray":
        return [_number(f, seed + k) for k in range(3)]
    if kind in ("obj", "optobj"):
        return filled(f.ref, seed + 1)
    if kind == "objarray":
        return [filled(f.ref, seed + k + 1) for k in range(2)]
    raise ValueError(kind)


def filled(name: str, seed: int, /, **override: Any) -> Any:
    """A model with a non-default value in every member (overrides win)."""
    struct_ = BY_NAME[name]
    kwargs: dict[str, Any] = {}
    for f in struct_.fields:
        if f.name in override:
            kwargs[f.name] = override[f.name]
        else:
            kwargs[f.name] = _value(f, seed + len(kwargs))
    return getattr(models, name)(**kwargs)


def all_fields() -> models.InstrumentFile:
    """Every member of every struct non-default; every enum value and exciter alternative occurs."""
    # Boundary, BridgeEnd, FeltLaw, DamperMode, BarrierKind: all values appear across the groups.
    kinds = ENUMS["BarrierKind"]
    barriers = [
        filled("BarrierParams", 10 + i, kind=kinds[i], side=-1 if i % 2 else 1)
        for i in range(len(kinds))
    ]
    # Replace the filled sides by explicit values: -1 and 1 both occur (the default is 1, so the
    # 1s below are the only default-valued members; the other members are all non-default).

    def segment(
        seed: int, end: str, boundary: str, with_damper: bool, mode: str, **kw: Any
    ) -> models.SegmentDesc:
        string = filled("StringParams", seed, bridgeEnd=end, boundary=boundary)
        string = models.StringParams(**{**string.__dict__, "dOuter": string.dCore * 1.5})
        result: models.SegmentDesc = filled(
            "SegmentDesc",
            seed,
            string=string,
            bridgePoint=seed % 2,
            damper=filled("DamperParams", seed, mode=mode) if with_damper else None,
            **kw,
        )
        return result

    group0 = filled(
        "NoteGroupDesc",
        1,
        key=60,
        segments=[
            segment(20, "AtL", "Clamped", True, "RankOne", barriers=barriers),
            segment(30, "At0", "Pinned", False, "Diagonal", barriers=[]),
        ],
        exciter=models.ExciterParams(e=filled("HammerParams", 40, law="Stulov")),
    )
    group1 = filled(
        "NoteGroupDesc", 2, key=61,
        segments=[segment(50, "AtL", "Pinned", True, "Diagonal", barriers=[])],
        exciter=models.ExciterParams(e=filled("PluckPulse", 51)),
    )  # fmt: skip
    group2 = filled(
        "NoteGroupDesc", 3, key=62,
        segments=[segment(60, "AtL", "Pinned", False, "Diagonal", barriers=[])],
        exciter=models.ExciterParams(e=filled("Plectrum", 61)),
    )  # fmt: skip
    group3 = filled(
        "NoteGroupDesc", 4, key=-1,
        segments=[segment(70, "AtL", "Pinned", False, "Diagonal", barriers=[])],
        exciter=models.ExciterParams(e=filled("Finger", 71)),
    )  # fmt: skip
    group4 = filled(
        "NoteGroupDesc", 5, key=-1,
        segments=[segment(80, "AtL", "Pinned", False, "Diagonal", barriers=[])],
        exciter=models.ExciterParams(e=filled("InitialDisplacement", 81)),
    )  # fmt: skip
    group5 = filled(
        "NoteGroupDesc", 6, key=63,
        segments=[segment(90, "AtL", "Pinned", False, "Diagonal", barriers=[])],
        exciter=None,
    )  # fmt: skip
    plate = filled(
        "PlateParams",
        100,
        outline=[
            models.Point(x=0.1, y=0.0, z=0.0),
            models.Point(x=1.1, y=0.1, z=0.0),
            models.Point(x=0.6, y=0.9, z=0.0),
        ],
        thicknessNx=2,
        thicknessNy=3,
        thicknessGrid=[0.004, 0.0045, 0.005, 0.0055, 0.006, 0.0065],
    )
    body = filled(
        "BodyParams", 110, source="ModalTable", plate=plate, modalTableHash=small_manifest().hash
    )
    desc = filled(
        "InstrumentDesc",
        120,
        name="all-fields",
        groups=[group0, group1, group2, group3, group4, group5],
        bridgePoints=[filled("BridgePointDesc", 130), filled("BridgePointDesc", 131)],
        body=body,
        channels=[filled("ChannelDesc", 140), filled("ChannelDesc", 141)],
    )
    return models.InstrumentFile(desc=desc, blobs=[small_manifest(), second_manifest()])


def body_measured() -> models.InstrumentFile:
    """The monochord with a MeasuredMobilityFit body (the third BodySource value)."""
    base = small.monochord()
    desc = models.InstrumentDesc(
        **{
            **base.desc.__dict__,
            "name": "body-measured",
            "body": models.BodyParams(
                source="MeasuredMobilityFit",
                plate=base.desc.body.plate,
                modalTableHash=small_manifest().hash,
            ),
        }
    )
    return models.InstrumentFile(desc=desc, blobs=[small_manifest()])


def numbers() -> models.InstrumentFile:
    """The monochord whose tuning curve holds edge-case and random numbers (number formatting)."""
    rng = random.Random(20261011)
    edge = [
        0.0, -0.0, 1.0, -1.0, 0.1, 0.5, 100.0, 1e-5, 1.5e-5, 0.0001, 0.00012, 123456.789, 1e15,
        1e16, 123456789012345.0, 1234567890123456.0, 1e21, 1.5e300, 5e-324, 1.7976931348623157e308,
        2.2250738585072014e-308, 0.3, 2.0 / 3.0, 1.0 / 3.0, 9007199254740993.0, 4.35, 100.0001,
    ]  # fmt: skip
    rand = [struct.unpack("<d", struct.pack("<Q", rng.getrandbits(64)))[0] for _ in range(300)]
    rand = [v for v in rand if v == v and abs(v) != float("inf")]
    rand += [rng.uniform(-1000.0, 1000.0) for _ in range(300)]
    base = small.monochord()
    tuning = models.TuningCurve(stretchCents=edge + rand)
    desc = models.InstrumentDesc(**{**base.desc.__dict__, "name": "numbers", "tuning": tuning})
    return models.InstrumentFile(desc=desc, blobs=[])


# --- malformed documents -------------------------------------------------------------------------


def base_document() -> dict[str, Any]:
    """A valid ModalTable monochord with one blob (modal-small.bin next to the file)."""
    base = small.monochord()
    desc = models.InstrumentDesc(
        **{
            **base.desc.__dict__,
            "body": models.BodyParams(
                source="ModalTable",
                plate=base.desc.body.plate,
                modalTableHash=small_manifest().hash,
            ),
        }
    )
    return file_to_json(models.InstrumentFile(desc=desc, blobs=[small_manifest("modal-small.bin")]))


def _segment(doc: dict[str, Any]) -> dict[str, Any]:
    segment: dict[str, Any] = doc["instrument"]["groups"][0]["segments"][0]
    return segment


def malformed() -> dict[str, tuple[str, list[tuple[str, str]]]]:
    """Fixture name -> (text, expected (path, code) pairs)."""
    out: dict[str, tuple[str, list[tuple[str, str]]]] = {}

    def add(name: str, doc: Any, expected: list[tuple[str, str]]) -> None:
        text = doc if isinstance(doc, str) else json.dumps(doc, indent=2) + "\n"
        out[name] = (text, expected)

    seg = "/instrument/groups/0/segments/0"

    def mutated(fn: Any) -> dict[str, Any]:
        doc = base_document()
        fn(doc)
        return doc

    add("parse-error", '{\n  "format": "pme-instrument",\n  "schemaVersion": 1,\n', [("", "parse")])
    add("root-not-object", [1, 2, 3], [("", "type")])
    add(
        "wrong-type-deep",
        mutated(lambda d: _segment(d)["string"].update(L="long")),
        [(f"{seg}/string/L", "type")],
    )
    add(
        "bool-as-number",
        mutated(lambda d: _segment(d)["string"].update(L=True)),
        [(f"{seg}/string/L", "type")],
    )
    add(
        "integer-given-float",
        mutated(lambda d: d["instrument"]["groups"][0].update(key=60.5)),
        [("/instrument/groups/0/key", "type")],
    )
    add(
        "missing-member",
        mutated(lambda d: _segment(d)["string"].pop("dCore")),
        [(f"{seg}/string/dCore", "missing")],
    )
    add(
        "unknown-key",
        mutated(lambda d: d["instrument"].update(colour="red")),
        [("/instrument/colour", "unknown_key")],
    )
    add(
        "range-packing",
        mutated(lambda d: _segment(d)["string"].update(packing=1.2)),
        [(f"{seg}/string/packing", "range")],
    )
    add(
        "range-key",
        mutated(lambda d: d["instrument"]["groups"][0].update(key=128)),
        [("/instrument/groups/0/key", "range")],
    )
    add(
        "bad-enum",
        mutated(lambda d: _segment(d)["string"].update(boundary="Free")),
        [(f"{seg}/string/boundary", "enum")],
    )
    add(
        "bad-side",
        mutated(lambda d: _segment(d).update(barriers=[_barrier(side=0)])),
        [(f"{seg}/barriers/0/side", "enum")],
    )
    add(
        "bad-side-huge",
        mutated(lambda d: _segment(d).update(barriers=[_barrier(side=2**63)])),
        [(f"{seg}/barriers/0/side", "enum"), (f"{seg}/barriers/0/side", "range")],
    )
    add(
        "lone-surrogate",
        json.dumps(base_document(), indent=2).replace(
            '"name": "test-monochord"', '"name": "\\ud800"'
        ),
        [("", "parse")],
    )

    def dup(d: dict[str, Any]) -> None:
        d["instrument"]["groups"].append(copy.deepcopy(d["instrument"]["groups"][0]))

    add("duplicate-key", mutated(dup), [("/instrument/groups/1/key", "duplicate")])
    add(
        "bridge-point-range",
        mutated(lambda d: _segment(d).update(bridgePoint=3)),
        [(f"{seg}/bridgePoint", "reference")],
    )
    add(
        "rear-segment-first",
        mutated(lambda d: _segment(d)["string"].update(bridgeEnd="At0")),
        [(f"{seg}/string/bridgeEnd", "structure")],
    )
    add(
        "outer-diameter-small",
        mutated(lambda d: _segment(d)["string"].update(dOuter=0.0005)),
        [(f"{seg}/string/dOuter", "range")],
    )
    add(
        "barrier-lengths",
        mutated(lambda d: _segment(d).update(barriers=[_barrier(w=[0.1])])),
        [(f"{seg}/barriers/0", "structure")],
    )

    def two_exciters(d: dict[str, Any]) -> None:
        g = d["instrument"]["groups"][0]
        g["exciter"]["pluckPulse"] = json.loads(
            dumps_doc(
                models.PluckPulse(
                    duration=0.001, peakForceMax=1.0, position=0.1, width=0.01, angle=0.0
                )
            )
        )

    add("two-exciters", mutated(two_exciters), [("/instrument/groups/0/exciter", "structure")])
    add(
        "empty-exciter",
        mutated(lambda d: d["instrument"]["groups"][0].update(exciter={})),
        [("/instrument/groups/0/exciter", "structure")],
    )

    def plate_bad(d: dict[str, Any]) -> None:
        d["instrument"]["body"].update(source="PlateModel", modalTableHash="")
        d["instrument"]["body"]["plate"].update(
            lengthX=0.6,
            lengthY=0.4,
            thickness=0.006,
            youngsL=0.0,
            youngsR=9e8,
            shearLR=7e8,
            density=420.0,
            fMax=3000.0,
        )
        d["blobs"] = []

    add("plate-nonpositive", mutated(plate_bad), [("/instrument/body/plate/youngsL", "range")])

    def grid_bad(d: dict[str, Any]) -> None:
        plate = d["instrument"]["body"]["plate"]
        plate.update(thicknessNx=2, thicknessNy=3, thicknessGrid=[0.005] * 5)

    add("grid-size", mutated(grid_bad), [("/instrument/body/plate/thicknessGrid", "structure")])
    add(
        "bridge-beam-short",
        mutated(lambda d: d["instrument"]["body"]["plate"].update(bridges=[_beam(1)])),
        [("/instrument/body/plate/bridges/0/path", "structure")],
    )
    add(
        "outline-two-vertices",
        mutated(lambda d: d["instrument"]["body"]["plate"].update(outline=[_pt(0), _pt(1)])),
        [("/instrument/body/plate/outline", "structure")],
    )
    add(
        "modal-hash-unresolved",
        mutated(lambda d: d["instrument"]["body"].update(modalTableHash="sha256:" + "ab" * 32)),
        [("/instrument/body/modalTableHash", "reference")],
    )
    add(
        "modal-hash-malformed",
        mutated(lambda d: d["instrument"]["body"].update(modalTableHash="sha256:xyz")),
        [("/instrument/body/modalTableHash", "type")],
    )
    add(
        "blob-hash-form",
        mutated(lambda d: d["blobs"][0].update(hash="sha256:abc")),
        [("/blobs/0/hash", "type")],
    )
    add(
        "blob-hash-wrong",
        mutated(
            lambda d: (
                d["blobs"][0].update(hash="sha256:" + "cd" * 32)
                or d["instrument"]["body"].update(modalTableHash="sha256:" + "cd" * 32)
            )
        ),
        [("/blobs/0/hash", "blob")],
    )
    add(
        "blob-offset-past-end",
        mutated(lambda d: d["blobs"][0]["arrays"][1].update(offset=4000)),
        [("/blobs/0/arrays/1/offset", "blob")],
    )
    add(
        "blob-misaligned",
        mutated(lambda d: d["blobs"][0]["arrays"][0].update(offset=4)),
        [("/blobs/0/arrays/0/offset", "blob"), ("/blobs/0/arrays/1/offset", "blob")],
    )
    add(
        "blob-overlap",
        mutated(lambda d: d["blobs"][0]["arrays"][1].update(offset=88)),
        [("/blobs/0/arrays/1/offset", "blob")],
    )
    add(
        "blob-path-parent",
        mutated(lambda d: d["blobs"][0].update(path="../modal-small.bin")),
        [("/blobs/0/path", "blob")],
    )
    add(
        "blob-path-absolute",
        mutated(lambda d: d["blobs"][0].update(path="/etc/passwd")),
        [("/blobs/0/path", "blob")],
    )
    add(
        "blob-dtype",
        mutated(lambda d: d["blobs"][0]["arrays"][0].update(dtype="float16")),
        [("/blobs/0/arrays/0/dtype", "enum")],
    )
    add(
        "blob-shape-zero",
        mutated(lambda d: d["blobs"][0]["arrays"][1].update(shape=[0])),
        [("/blobs/0/arrays/1/shape/0", "range")],
    )
    add(
        "blob-missing-file",
        mutated(lambda d: d["blobs"][0].update(path="no-such-file.bin")),
        [("/blobs/0/path", "blob")],
    )

    def dup_blob(d: dict[str, Any]) -> None:
        d["blobs"].append(copy.deepcopy(d["blobs"][0]))

    add(
        "blob-duplicate",
        mutated(dup_blob),
        [("/blobs/1/hash", "duplicate"), ("/blobs/1/path", "duplicate")],
    )

    def dup_array(d: dict[str, Any]) -> None:
        d["blobs"][0]["arrays"][1]["name"] = "omega"

    add("blob-array-name-twice", mutated(dup_array), [("/blobs/0/arrays/1/name", "duplicate")])
    add(
        "missing-version",
        mutated(lambda d: d.pop("schemaVersion")),
        [("/schemaVersion", "missing")],
    )
    add(
        "version-future",
        mutated(lambda d: d.update(schemaVersion=2)),
        [("/schemaVersion", "version")],
    )
    add(
        "version-fractional",
        mutated(lambda d: d.update(schemaVersion=1.5)),
        [("/schemaVersion", "version")],
    )
    add(
        "version-text",
        mutated(lambda d: d.update(schemaVersion="1")),
        [("/schemaVersion", "version")],
    )
    add("format-wrong", mutated(lambda d: d.update(format="other")), [("/format", "enum")])

    def three(d: dict[str, Any]) -> None:
        _segment(d)["string"].update(packing=1.2, boundary="Free")
        d["instrument"]["groups"][0]["key"] = 128

    add(
        "three-errors",
        mutated(three),
        [
            (f"{seg}/string/packing", "range"),
            (f"{seg}/string/boundary", "enum"),
            ("/instrument/groups/0/key", "range"),
        ],
    )

    def three_cross(d: dict[str, Any]) -> None:
        group = d["instrument"]["groups"][0]
        group["segments"][0]["bridgePoint"] = 5
        group["segments"][0]["string"]["bridgeEnd"] = "At0"
        d["instrument"]["groups"].append(copy.deepcopy(group))

    add(
        "cross-field-errors",
        mutated(three_cross),
        [
            (f"{seg}/bridgePoint", "reference"),
            (f"{seg}/string/bridgeEnd", "structure"),
            ("/instrument/groups/1/key", "duplicate"),
            ("/instrument/groups/1/segments/0/bridgePoint", "reference"),
            ("/instrument/groups/1/segments/0/string/bridgeEnd", "structure"),
        ],
    )
    return out


def _pt(i: int) -> dict[str, float]:
    return {"x": 0.1 * i, "y": 0.2 * i, "z": 0.0}


def _beam(vertices: int) -> dict[str, Any]:
    return {
        "path": [_pt(i) for i in range(vertices)],
        "width": 0.025,
        "height": 0.03,
        "youngs": 12e9,
        "density": 700.0,
    }


def _barrier(**kw: Any) -> dict[str, Any]:
    barrier: dict[str, Any] = {
        "kind": "Fret", "x": [0.1, 0.2], "b": [0.001, 0.001], "w": [0.01, 0.01], "side": 1,
        "K": 1e6, "alpha": 1.5, "muB": 0.1,
    }  # fmt: skip
    barrier.update(kw)
    return barrier


def dumps_doc(model: Any) -> str:
    """Canonical JSON of one model, for embedding."""
    from pme_compiler.schema.codec import struct_to_json

    return dumps_document(struct_to_json(type(model).__name__, model))


# --- version 0 -----------------------------------------------------------------------------------


def piano_v1() -> models.InstrumentFile:
    """The expected version 1 form of piano-v0: two hammer groups, one pluckPulse group."""
    mono = small.monochord().desc
    hammer0 = small.hammer()
    hammer1 = models.HammerParams(**{**hammer0.__dict__, "mass": 0.007, "width": 0.012})

    def group(key: int, exciter: models.ExciterParams, detune: float) -> models.NoteGroupDesc:
        seg = models.SegmentDesc(
            **{**mono.groups[0].segments[0].__dict__, "unisonDetuneCents": 0.0}
        )
        seg = models.SegmentDesc(**{**seg.__dict__, "unisonDetuneCents": detune})
        return models.NoteGroupDesc(
            key=key, segments=[seg], exciter=exciter, unaCorda=models.UnaCordaParams()
        )

    pulse = models.PluckPulse(
        duration=0.002, peakForceMax=5.0, position=0.25, width=0.01, angle=0.1, strengthGamma=1.0
    )
    groups = [
        group(45, models.ExciterParams(e=hammer0), 0.0),
        group(46, models.ExciterParams(e=hammer1), 0.0),
        group(-1, models.ExciterParams(e=pulse), 0.0),
    ]
    desc = models.InstrumentDesc(**{**mono.__dict__, "name": "piano-v0", "groups": groups})
    return models.InstrumentFile(desc=desc, blobs=[])


def to_v0(doc: dict[str, Any]) -> dict[str, Any]:
    """Reverse of the 0 -> 1 migration, applied to a version 1 tree (independent of migrate.py)."""
    v0 = copy.deepcopy(doc)
    v0["schemaVersion"] = 0
    for group in v0["instrument"]["groups"]:
        for segment in group["segments"]:
            segment.pop("unisonDetuneCents")
        pulse = group.get("exciter", {}).get("pluckPulse")
        if pulse is not None:
            pulse["peakForce"] = pulse.pop("peakForceMax")
            pulse.pop("strengthGamma")
    plate = v0["instrument"]["body"]["plate"]
    for key in (
        "outline",
        "thicknessNx",
        "thicknessNy",
        "thicknessGrid",
        "bridges",
        "rimRotationalStiffness",
    ):
        plate.pop(key)
    return v0


def v0_files() -> dict[str, str]:
    """File name -> text of the version 0 fixtures."""
    expected = piano_v1()
    doc = file_to_json(expected)
    v0 = to_v0(doc)
    out = {
        "piano-v0.json": json.dumps(v0, indent=2) + "\n",
        "piano-v0.expected.json": dumps(expected),
    }
    plectrum = copy.deepcopy(v0)
    plectrum["instrument"]["groups"][2]["exciter"] = {
        "plectrum": {
            "tipMass": 0.0005,
            "driverStiffness": 800.0,
            "driverSpeed": 0.5,
            "slipHeight": 0.0005,
            "contactK": 1e6,
            "contactAlpha": 1.5,
        }
    }
    out["plectrum-v0.json"] = json.dumps(plectrum, indent=2) + "\n"
    finger = copy.deepcopy(v0)
    finger["instrument"]["groups"][2]["exciter"] = {
        "finger": {
            "mass": 0.002,
            "stiffness": 1e5,
            "alpha": 1.5,
            "muFlesh": 0.5,
            "width": 0.01,
            "driverSpeed": 1.0,
            "slipHeight": 0.002,
        }
    }
    out["finger-v0.json"] = json.dumps(finger, indent=2) + "\n"
    return out


V0_EXPECTED = {
    "plectrum-v0.json": [("/instrument/groups/2/exciter/plectrum", "migration")],
    "finger-v0.json": [("/instrument/groups/2/exciter/finger", "migration")],
}


# --- driver --------------------------------------------------------------------------------------


def all_outputs() -> dict[Path, bytes]:
    """Every fixture file and its bytes."""
    files: dict[Path, bytes] = {}

    def text(path: str, content: str) -> None:
        files[HERE / path] = content.encode("utf-8")

    text("all-fields.json", dumps(all_fields()))
    text("body-measured.json", dumps(body_measured()))
    text("numbers.json", dumps(numbers()))
    files[HERE / "blobs" / "modal-small.bin"] = modal_small_bytes()
    files[HERE / "blobs" / "modal-second.bin"] = modal_second_bytes()
    files[HERE / "malformed" / "modal-small.bin"] = modal_small_bytes()
    expected: dict[str, list[list[str]]] = {}
    for name, (content, pairs) in malformed().items():
        text(f"malformed/{name}.json", content)
        expected[f"{name}.json"] = [list(p) for p in sorted(pairs)]
    text("malformed/expected.json", json.dumps(expected, indent=2) + "\n")
    for name, content in v0_files().items():
        text(f"v0/{name}", content)
    v0_expected = {name: [list(p) for p in pairs] for name, pairs in V0_EXPECTED.items()}
    text("v0/expected.json", json.dumps(v0_expected, indent=2) + "\n")
    return files


def main(argv: list[str] | None = None) -> int:
    """Write the fixtures, or with --check verify that the committed files are current."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    stale = []
    for path, data in all_outputs().items():
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                stale.append(path)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    for path in stale:
        print(f"stale: {path.relative_to(HERE)}", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    raise SystemExit(main())
