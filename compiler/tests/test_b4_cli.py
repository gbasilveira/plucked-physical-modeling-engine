# SPDX-License-Identifier: MIT
"""B4-R1 to B4-R5: the command line writes a loadable instrument file and blob for each route."""

from __future__ import annotations

import shutil
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from pme_compiler.body import modal_blob
from pme_compiler.importers.__main__ import main
from pme_compiler.importers._common import BodyModalTable
from pme_compiler.importers.vector_fit import transfer_mobility
from pme_compiler.schema import load

ROOT = Path(__file__).resolve().parents[2]
GUITAR = ROOT / "instruments" / "examples" / "test-guitar-6" / "instrument.json"
FIXTURE = Path(__file__).resolve().parent / "data" / "b4" / "synthetic_maesstro.mat"


def _setup(tmp_path: Path) -> Path:
    src = tmp_path / "in.json"
    shutil.copy(GUITAR, src)
    return src


def _out(tmp_path: Path, name: str) -> Path:
    d = tmp_path / name
    d.mkdir()
    return d / "instrument.json"


def test_b4_r1_cli_maesstro(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """B4-R1: the maesstro route writes a ModalTable instrument that loads with verified blobs."""
    out = _out(tmp_path, "m")
    rc = main(
        [
            "maesstro",
            "--instrument",
            str(_setup(tmp_path)),
            "--out",
            str(out),
            "--basis",
            str(FIXTURE),
        ]
    )
    assert rc == 0 and "9 modes" in capsys.readouterr().out
    f = load(out, verify_blobs=True)
    assert f.desc.body.source == "ModalTable"


def test_b4_r2_cli_csv_and_error_exit(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """B4-R2: the csv route works, and a bad file exits 1 with the line and column named."""
    phi = ",".join(f"phi_{b}" for b in range(6))
    row = ",".join(["0.1"] * 6)
    good = tmp_path / "good.csv"
    good.write_text(f"frequency_hz,q_factor,{phi}\n100,20,{row}\n300,30,{row}\n", encoding="utf-8")
    out = _out(tmp_path, "c")
    assert (
        main(
            ["csv", "--instrument", str(_setup(tmp_path)), "--out", str(out), "--table", str(good)]
        )
        == 0
    )
    assert load(out, verify_blobs=True).blobs[0].path == "blobs/body-modal-table.bin"
    bad = tmp_path / "bad.csv"
    bad.write_text(f"frequency_hz,q_factor,{phi}\n100,-1,{row}\n", encoding="utf-8")
    assert (
        main(
            [
                "csv",
                "--instrument",
                str(_setup(tmp_path)),
                "--out",
                str(_out(tmp_path, "c2")),
                "--table",
                str(bad),
            ]
        )
        == 1
    )
    assert "line 2, column q_factor" in capsys.readouterr().err


def test_b4_r3_cli_fit_and_r4_helmholtz_merge(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """B4-R3 and B4-R4: fit from an .npz, then merge a Helmholtz mode into the result."""
    freqs = np.array([90.0, 180.0, 410.0, 800.0])
    truth = BodyModalTable(
        omega=2 * np.pi * freqs,
        sigma=np.array([2.0, 4.0, 8.0, 14.0]),
        phi=np.abs(np.random.default_rng(3).normal(0.4, 0.1, (4, 6)))
        * np.array([1, -1, 1, 1, -1, 1]),
        points=np.zeros((6, 3)),
    )
    truth.phi[:, 0] = np.abs(truth.phi[:, 0])
    f = np.geomspace(40.0, 1200.0, 2000)
    arrays: dict[str, Any] = {
        f"y_0_{b}": transfer_mobility(truth, 0, b, 2 * np.pi * f) for b in range(6)
    }
    np.savez(tmp_path / "mob.npz", frequency_hz=f, **arrays)
    inst = _setup(tmp_path)
    out1 = _out(tmp_path, "fit")
    rc = main(
        [
            "fit",
            "--instrument",
            str(inst),
            "--out",
            str(out1),
            "--mobility",
            str(tmp_path / "mob.npz"),
            "--pairs",
            "4",
        ]
    )
    text = capsys.readouterr().out
    assert rc == 0 and "RMS relative error" in text
    fitted = load(out1, verify_blobs=True)
    assert fitted.desc.body.source == "MeasuredMobilityFit"
    table = modal_blob.read(fitted.blobs[0], out1.parent)
    assert np.allclose(table.omega, truth.omega, rtol=1e-6)

    out2 = _out(tmp_path, "hh")
    rc = main(
        [
            "helmholtz",
            "--instrument",
            str(out1),
            "--out",
            str(out2),
            "--volume",
            "0.012",
            "--hole-area",
            "0.0050265482",
            "--plate-thickness",
            "0.003",
            "--q-factor",
            "30",
            "--phi",
            "0.5,0.5,0.5,0.5,0.5,0.5",
        ]
    )
    assert rc == 0 and "132.59" in capsys.readouterr().out
    merged = load(out2, verify_blobs=True)
    assert merged.desc.body.source == "MeasuredMobilityFit" and len(merged.blobs) == 1
    assert modal_blob.read(merged.blobs[0], out2.parent).omega.size == 5


def test_b4_r5_cli_membranes(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """B4-R5: circle and rectangle routes; the membrane frame holds the bridge points."""
    inst = _setup(tmp_path)
    out = _out(tmp_path, "r")
    # Rectangle a x b contains the guitar bridge points (x = 0.12, y = 0.15 .. 0.21).
    rc = main(
        [
            "rectangle",
            "--instrument",
            str(inst),
            "--out",
            str(out),
            "--tension",
            "1500",
            "--areal-density",
            "0.25",
            "--f-max",
            "2000",
            "--size",
            "0.2,0.3",
            "--q-factor",
            "20",
        ]
    )
    assert rc == 0
    assert load(out, verify_blobs=True).desc.body.source == "ModalTable"
    out2 = _out(tmp_path, "c")
    rc = main(
        [
            "circle",
            "--instrument",
            str(inst),
            "--out",
            str(out2),
            "--tension",
            "1500",
            "--areal-density",
            "0.25",
            "--f-max",
            "2000",
            "--radius",
            "0.3",
            "--loss-factor",
            "0.05",
        ]
    )
    assert rc == 0
    assert (
        main(
            [
                "circle",
                "--instrument",
                str(inst),
                "--out",
                str(_out(tmp_path, "x")),
                "--tension",
                "1500",
                "--areal-density",
                "0.25",
                "--f-max",
                "2000",
                "--radius",
                "0.3",
            ]
        )
        == 1
    )
    assert "exactly one" in capsys.readouterr().err
