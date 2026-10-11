# SPDX-License-Identifier: MIT
"""B2-R7 output: the CLI compiles the example instruments; the result loads with blob checks."""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

from pme_compiler.body import cli, modal_blob
from pme_compiler.schema import load

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = REPO / "instruments" / "examples"


def _compile(name: str, out: Path, *extra: str) -> int:
    return cli.main([str(EXAMPLES / name / "instrument.json"), "-o", str(out), *extra])


def test_b2_r7_compiled_test_monochord_loads_and_round_trips(tmp_path: Path) -> None:
    out = tmp_path / "mono"
    assert _compile("test-monochord", out, "--f-max", "400", "--mesh-size", "0.04") == 0
    compiled = load(out / "instrument.json", verify_blobs=True)
    body = compiled.desc.body
    assert body.source == "ModalTable"
    assert body.modalTableHash == compiled.blobs[0].hash
    source = load(EXAMPLES / "test-monochord" / "instrument.json")
    assert body.plate == source.desc.body.plate  # the plate block is kept as provenance
    entry = compiled.blobs[0]
    assert entry.path == modal_blob.DEFAULT_RELPATH
    assert entry.producer.tool == "pme_compiler.body"
    assert [a.name for a in entry.arrays] == [
        "omega",
        "sigma",
        "phi",
        "points",
        "gridPoints",
        "gridWeights",
        "gridPhi",
    ]
    assert all(a.offset % 64 == 0 for a in entry.arrays)
    table = modal_blob.read(entry, out)
    assert modal_blob.validate(table) == []
    k = table.omega.size
    assert table.phi.shape == (k, 1)
    assert table.points.tolist() == [[0.3, 0.2, 0.0]]
    assert np.all(np.diff(table.omega) >= 0.0) and table.omega[0] > 2.0 * np.pi
    assert np.array_equal(table.sigma, 0.5 * 0.01 * table.omega)
    assert table.grid_weights is not None
    assert float(table.grid_weights.sum()) == pytest.approx(0.6 * 0.4, rel=1e-12)
    # the file bytes match the manifest hash, and re-reading is bit-identical
    data = (out / entry.path).read_bytes()
    assert "sha256:" + hashlib.sha256(data).hexdigest() == entry.hash
    again = modal_blob.read(entry, out)
    assert again.phi.tobytes() == table.phi.tobytes()


def test_b2_r7_compiled_test_guitar_6_has_one_row_per_bridge_point(tmp_path: Path) -> None:
    out = tmp_path / "guitar"
    assert _compile("test-guitar-6", out, "--f-max", "500", "--mesh-size", "0.02") == 0
    compiled = load(out / "instrument.json", verify_blobs=True)
    src = load(EXAMPLES / "test-guitar-6" / "instrument.json")
    table = modal_blob.read(compiled.blobs[0], out)
    p = len(src.desc.bridgePoints)
    assert p == 6 and table.phi.shape[1] == p
    assert table.points.shape == (p, 3)
    expected = [[b.position.x, b.position.y, b.position.z] for b in src.desc.bridgePoints]
    assert table.points.tolist() == expected
    assert compiled.desc.groups == src.desc.groups  # nothing but the body changed
    assert compiled.desc.bridgePoints == src.desc.bridgePoints


def test_b2_r7_compilation_is_deterministic(tmp_path: Path) -> None:
    args = ("--f-max", "300", "--mesh-size", "0.05", "--grid-order", "0")
    assert _compile("test-monochord", tmp_path / "a", *args) == 0
    assert _compile("test-monochord", tmp_path / "b", *args) == 0
    a = (tmp_path / "a" / modal_blob.DEFAULT_RELPATH).read_bytes()
    b = (tmp_path / "b" / modal_blob.DEFAULT_RELPATH).read_bytes()
    assert a == b


def test_b2_r7_grid_order_zero_omits_the_radiation_grid(tmp_path: Path) -> None:
    out = tmp_path / "nogrid"
    assert _compile("test-monochord", out, "--f-max", "300", "--grid-order", "0") == 0
    names = {a.name for a in load(out / "instrument.json").blobs[0].arrays}
    assert names == {"omega", "sigma", "phi", "points"}


def test_b2_r7_a_compiled_instrument_needs_force_to_recompile(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    first = tmp_path / "first"
    assert _compile("test-monochord", first, "--f-max", "300", "--grid-order", "0") == 0
    capsys.readouterr()
    args = [str(first / "instrument.json"), "-o", str(tmp_path / "second"), "--f-max", "300"]
    assert cli.main(args) == 2
    assert "already compiled" in capsys.readouterr().err
    assert cli.main([*args, "--force"]) == 0
    second = load(tmp_path / "second" / "instrument.json", verify_blobs=True)
    assert len(second.blobs) == 1 and second.desc.body.source == "ModalTable"


def test_b2_r7_invalid_input_exits_with_an_error(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    bad = tmp_path / "bad.json"
    bad.write_text("{}", encoding="utf-8")
    assert cli.main([str(bad), "-o", str(tmp_path / "o")]) == 2
    assert "error" in capsys.readouterr().err


def test_b2_r7_python_dash_m_entry_point_runs(tmp_path: Path) -> None:
    out = tmp_path / "m"
    res = subprocess.run(
        [
            sys.executable,
            "-m",
            "pme_compiler.body",
            str(EXAMPLES / "test-monochord" / "instrument.json"),
            "-o",
            str(out),
            "--f-max",
            "250",
            "--mesh-size",
            "0.06",
            "--grid-order",
            "0",
        ],
        capture_output=True,
        text=True,
        check=False,
        cwd=tmp_path,
    )
    assert res.returncode == 0, res.stderr
    assert "compiled test-monochord" in res.stdout
    load(out / "instrument.json", verify_blobs=True)
