# SPDX-License-Identifier: MIT
"""B2-R7 body modal-table blob, layout 1 (round trip, validation, attach, manifest checks)."""

from __future__ import annotations

import dataclasses
from pathlib import Path

import numpy as np
import pytest

from pme_compiler.body import modal_blob
from pme_compiler.body.modal_blob import BodyModalTable
from pme_compiler.schema import InstrumentError, dump, load

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = REPO / "instruments" / "examples"


def _table(k: int = 5, p: int = 2, g: int = 7, c: int = 2, full: bool = True) -> BodyModalTable:
    rng = np.random.default_rng(1)
    omega = np.sort(rng.uniform(100.0, 5000.0, k))
    base = BodyModalTable(
        omega=omega,
        sigma=0.5 * 0.01 * omega,
        phi=rng.normal(size=(k, p)),
        points=rng.uniform(size=(p, 3)),
    )
    if not full:
        return base
    return dataclasses.replace(
        base,
        grid_points=rng.uniform(size=(g, 3)),
        grid_weights=rng.uniform(size=g),
        grid_phi=rng.normal(size=(k, g)),
        sigma_rad=rng.uniform(size=k),
        alpha=rng.normal(size=(c, k)),
        beta=rng.normal(size=(c, k)),
        channel_delay=rng.uniform(size=c),
        residual_mobility=rng.uniform(size=p),
    )


@pytest.mark.parametrize("full", [True, False])
def test_b2_r7_blob_round_trip_bit_identical(tmp_path: Path, full: bool) -> None:
    """B2-R7: blob round trip bit identical."""
    t = _table(full=full)
    assert modal_blob.validate(t) == []
    entry = modal_blob.write(t, tmp_path, modal_blob.DEFAULT_RELPATH, "pme_compiler.body")
    assert entry.hash.startswith("sha256:")
    assert all(a.offset % 64 == 0 and a.dtype == "float64" for a in entry.arrays)
    back = modal_blob.read(entry, tmp_path)
    for f in dataclasses.fields(BodyModalTable):
        a, b = getattr(t, f.name), getattr(back, f.name)
        if a is None:
            assert b is None
        else:
            assert b is not None and a.shape == b.shape
            assert a.tobytes() == b.tobytes()


def test_b2_r7_blob_validate_reports_problems() -> None:
    """B2-R7: blob validate reports problems."""
    t = _table(full=False)
    bad_order = dataclasses.replace(t, omega=t.omega[::-1].copy())
    assert any("sorted" in m for m in modal_blob.validate(bad_order))
    neg = dataclasses.replace(t, sigma=-t.sigma)
    assert any("sigma" in m for m in modal_blob.validate(neg))
    nan = dataclasses.replace(t, phi=np.full_like(t.phi, np.nan))
    assert any("NaN" in m for m in modal_blob.validate(nan))
    wrong = dataclasses.replace(t, phi=t.phi[:, :1])
    assert any("phi: expected shape" in m for m in modal_blob.validate(wrong))
    partial = dataclasses.replace(t, grid_weights=np.ones(3))
    assert any("together" in m for m in modal_blob.validate(partial))
    f32 = dataclasses.replace(t, phi=t.phi.astype(np.float32))
    assert any("float64" in m for m in modal_blob.validate(f32))
    with pytest.raises(ValueError, match="invalid body modal table"):
        modal_blob.write(neg, Path("."), "x.bin", "pme_compiler.body")


def test_b2_r7_blob_attach_and_load_verify(tmp_path: Path) -> None:
    """B2-R7: blob attach and load verify."""
    src = load(EXAMPLES / "test-monochord" / "instrument.json")
    pt = src.desc.bridgePoints[0].position
    t = dataclasses.replace(_table(p=1, full=False), points=np.array([[pt.x, pt.y, pt.z]]))
    entry = modal_blob.write(t, tmp_path, modal_blob.DEFAULT_RELPATH, "pme_compiler.body")
    out = modal_blob.attach(src, entry, "ModalTable")
    assert out.desc.body.source == "ModalTable"
    assert out.desc.body.modalTableHash == entry.hash
    assert out.desc.body.plate == src.desc.body.plate
    assert src.desc.body.source == "PlateModel"
    dump(out, tmp_path / "instrument.json")
    again = load(tmp_path / "instrument.json", verify_blobs=True)
    assert again.blobs[0].hash == entry.hash
    # corrupt the file: the hash check must fail
    p = tmp_path / entry.path
    p.write_bytes(p.read_bytes()[:-1] + b"\x01")
    with pytest.raises(InstrumentError):
        load(tmp_path / "instrument.json", verify_blobs=True)
    with pytest.raises(ValueError, match="source"):
        modal_blob.attach(src, entry, "PlateModel")
