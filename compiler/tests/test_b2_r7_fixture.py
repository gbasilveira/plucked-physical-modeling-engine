# SPDX-License-Identifier: MIT
"""B2-R7: the committed compiled fixture used by the C++ loader test stays valid."""

from __future__ import annotations

from pathlib import Path

from pme_compiler.body import modal_blob
from pme_compiler.schema import load

FIXTURE = Path(__file__).parent / "data" / "b2" / "compiled-test-monochord"


def test_b2_r7_committed_fixture_loads_with_blob_checks() -> None:
    """B2-R7: the committed compiled test-monochord loads with verify_blobs and validates."""
    compiled = load(FIXTURE / "instrument.json", verify_blobs=True)
    assert compiled.desc.body.source == "ModalTable"
    table = modal_blob.read(compiled.blobs[0], FIXTURE)
    assert modal_blob.validate(table) == []
