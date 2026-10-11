# SPDX-License-Identifier: MIT
"""B1-R3: blob manifest checks (hash, offsets, shapes, alignment, overlap)."""

from __future__ import annotations

import hashlib
import json
import shutil
import struct
from pathlib import Path

from pme_compiler.schema import try_load

DATA = Path(__file__).resolve().parent / "data" / "b1"


def test_b1_r3_fixture_blob_matches_its_manifest() -> None:
    """B1-R3: hash, offsets and shapes of modal-small.bin as listed in all-fields.json."""
    doc = json.loads((DATA / "all-fields.json").read_text())
    entry = doc["blobs"][0]
    blob = (DATA / entry["path"]).read_bytes()
    assert entry["hash"] == "sha256:" + hashlib.sha256(blob).hexdigest()
    assert len(blob) < 400
    omega, phi, index = entry["arrays"]
    assert (omega["dtype"], omega["shape"], omega["offset"]) == ("float64", [4, 3], 0)
    assert struct.unpack_from("<12d", blob, omega["offset"])[:2] == (100.0, 101.5)
    assert (phi["dtype"], phi["shape"], phi["offset"]) == ("float32", [10], 96)
    assert struct.unpack_from("<10f", blob, phi["offset"])[4] == 1.0
    assert (index["dtype"], index["shape"], index["offset"]) == ("uint32", [8], 136)
    assert struct.unpack_from("<8I", blob, index["offset"])[7] == 49
    assert 136 + 8 * 4 == len(blob)


def test_b1_r3_all_fields_manifest_verifies() -> None:
    """B1-R3: the committed files satisfy their own manifest including SHA-256."""
    result, errors = try_load(DATA / "all-fields.json", verify_blobs=True)
    assert errors == []
    assert result is not None
    assert [b.producer.tool for b in result.blobs] == ["pme-plate-compiler", "pme-fit"]


def test_b1_r3_wrong_hash_fails_with_blob(tmp_path: Path) -> None:
    """B1-R3: a hash that does not match the file fails with `blob` at the hash."""
    shutil.copy(DATA / "malformed" / "modal-small.bin", tmp_path / "modal-small.bin")
    text = (DATA / "malformed" / "blob-hash-wrong.json").read_text()
    (tmp_path / "x.json").write_text(text)
    result, errors = try_load(tmp_path / "x.json", verify_blobs=True)
    assert result is None
    assert [(e.path, e.code) for e in errors] == [("/blobs/0/hash", "blob")]
    # The engine-side load (verify off) does not recompute the hash.
    assert try_load(tmp_path / "x.json", verify_blobs=False)[0] is not None


def test_b1_r3_changed_file_fails_verification(tmp_path: Path) -> None:
    """B1-R3: flipping one byte of the blob breaks verification but not the unverified load."""
    shutil.copy(DATA / "blobs" / "modal-small.bin", tmp_path / "blobs-modal.bin")
    src = json.loads((DATA / "malformed" / "range-key.json").read_text())
    src["instrument"]["groups"][0]["key"] = 45
    src["blobs"][0]["path"] = "blobs-modal.bin"
    (tmp_path / "i.json").write_text(json.dumps(src))
    assert try_load(tmp_path / "i.json", verify_blobs=True)[1] == []
    data = bytearray((tmp_path / "blobs-modal.bin").read_bytes())
    data[3] ^= 0x01
    (tmp_path / "blobs-modal.bin").write_bytes(bytes(data))
    result, errors = try_load(tmp_path / "i.json", verify_blobs=True)
    assert result is None
    assert [(e.path, e.code) for e in errors] == [("/blobs/0/hash", "blob")]
    assert try_load(tmp_path / "i.json", verify_blobs=False)[0] is not None
