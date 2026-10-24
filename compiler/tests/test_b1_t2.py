# SPDX-License-Identifier: MIT
"""B1-T2 and B1-R5: malformed files fail with the expected (path, code) pairs.

The C++ test (engine/tests/loader) loads the same fixtures and asserts the same pairs.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from pme_compiler.schema import CODES, InstrumentError, load, try_load, try_loads, validate

DATA = Path(__file__).resolve().parent / "data" / "b1"
MALFORMED = DATA / "malformed"
EXPECTED: dict[str, list[list[str]]] = json.loads((MALFORMED / "expected.json").read_text())


def _pairs(errors: list) -> list[tuple[str, str]]:  # type: ignore[type-arg]
    return sorted((e.path, e.code) for e in errors)


@pytest.mark.parametrize("name", sorted(EXPECTED))
def test_b1_t2_malformed_fixture_reports_expected_pairs(name: str) -> None:
    """B1-T2: every fixture fails with exactly the listed (path, code) pairs, all at once."""
    result, errors = try_load(MALFORMED / name, verify_blobs=True)
    assert result is None
    assert _pairs(errors) == sorted((p, c) for p, c in EXPECTED[name])
    assert all(e.message for e in errors)


def test_b1_t2_every_error_code_is_exercised() -> None:
    """B1-T2: at least one fixture per error code (migration is in the v0 fixtures)."""
    used = {code for pairs in EXPECTED.values() for _, code in pairs}
    used |= {
        code
        for pairs in json.loads((DATA / "v0" / "expected.json").read_text()).values()
        for _, code in pairs
    }
    assert used == set(CODES)


def test_b1_t2_required_cases_are_present() -> None:
    """B1-T2: the cases the brief names exist."""
    for name in [
        "parse-error", "wrong-type-deep", "missing-member", "unknown-key", "range-packing",
        "range-key", "bad-enum", "duplicate-key", "bridge-point-range", "rear-segment-first",
        "modal-hash-unresolved", "blob-offset-past-end", "three-errors",
    ]:  # fmt: skip
        assert f"{name}.json" in EXPECTED
    assert len(EXPECTED["three-errors.json"]) == 3


def test_b1_t2_load_raises_with_all_errors() -> None:
    """B1-R5: load raises InstrumentError carrying every error, not just the first."""
    with pytest.raises(InstrumentError) as info:
        load(MALFORMED / "three-errors.json")
    assert len(info.value.errors) == 3


def test_b1_r5_paths_are_json_pointers() -> None:
    """B1-R5: paths are RFC 6901 pointers, with ~ and / escaped in keys."""
    text = json.dumps(
        {"format": "pme-instrument", "schemaVersion": 1, "instrument": {}, "a/b~c": 1}
    )
    _, errors = try_loads(text)
    assert ("/a~1b~0c", "unknown_key") in _pairs(errors)


def test_b1_r5_at_most_one_hundred_errors() -> None:
    """B1-R5: the list is capped at 100 errors."""
    text = json.dumps(
        {
            "format": "pme-instrument",
            "schemaVersion": 1,
            "instrument": {},
            "blobs": [],
            **{f"k{i}": 1 for i in range(250)},
        }
    )
    _, errors = try_loads(text)
    assert len(errors) == 100


def test_b1_r5_type_error_hides_other_errors_at_the_path() -> None:
    """B1-R5: a value of the wrong type reports only `type`."""
    errors = validate(
        {"format": "pme-instrument", "schemaVersion": 1, "instrument": "x", "blobs": []}
    )
    assert _pairs(errors) == [("/instrument", "type")]


def test_b1_t2_blob_checks_need_a_file_only_when_asked() -> None:
    """B1-R3: a missing blob file is an error only when blobs are verified."""
    result, errors = try_load(MALFORMED / "blob-missing-file.json", verify_blobs=False)
    assert result is not None
    assert errors == []
