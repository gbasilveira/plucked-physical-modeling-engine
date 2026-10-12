# SPDX-License-Identifier: MIT
"""B4-R2: modal table import from CSV."""

from __future__ import annotations

import math
import shutil
from pathlib import Path

import numpy as np
import pytest

from pme_compiler.body import modal_blob
from pme_compiler.importers._common import ImportDataError, bridge_positions, finish, load_base
from pme_compiler.importers.csv_table import read_channel_delays, read_modal_csv
from pme_compiler.schema import load

GUITAR = Path(__file__).resolve().parents[2] / "instruments" / "examples" / "test-guitar-6"
PHI = ",".join(f"phi_{b}" for b in range(6))


def _pts() -> np.ndarray:
    return bridge_positions(load_base(GUITAR / "instrument.json").desc)


def _write(tmp_path: Path, text: str) -> Path:
    p = tmp_path / "t.csv"
    p.write_text(text, encoding="utf-8")
    return p


def _row(f: float, d: float | str, scale: float = 1.0) -> str:
    return f"{f},{d}," + ",".join(str(scale * (b + 1) * 0.1) for b in range(6))


def test_b4_r2_valid_csv_round_trips_into_a_blob(tmp_path: Path) -> None:
    """B4-R2: sigma, loss factor and Q forms; blob and instrument load back identically."""
    for col, d in (("sigma_per_s", 3.0), ("loss_factor", 0.02), ("q_factor", 50.0)):
        text = f"frequency_hz,{col},{PHI}\n{_row(100.0, d)}\n{_row(250.5, d, -1)}\n"
        t = read_modal_csv(_write(tmp_path, text), _pts(), 1)
        assert np.allclose(t.omega, 2 * math.pi * np.array([100.0, 250.5]))
        want = {
            "sigma_per_s": np.full(2, 3.0),
            "loss_factor": 0.01 * t.omega,
            "q_factor": t.omega / 100.0,
        }
        assert np.allclose(t.sigma, want[col])
        assert t.phi.shape == (2, 6) and t.phi[1, 2] == pytest.approx(-0.3)
        assert t.alpha is None

    shutil.copy(GUITAR / "instrument.json", tmp_path / "in.json")
    out = tmp_path / "o" / "instrument.json"
    out.parent.mkdir()
    finish(load_base(tmp_path / "in.json"), t, "ModalTable", out)
    loaded = load(out, verify_blobs=True)
    back = modal_blob.read(loaded.blobs[0], out.parent)
    assert np.array_equal(back.omega, t.omega) and np.array_equal(back.phi, t.phi)


def test_b4_r2_alpha_beta_and_channel_delay(tmp_path: Path) -> None:
    """B4-R2: alpha_c / beta_c become [C, K] arrays with the channel delays."""
    text = (
        f"frequency_hz,sigma_per_s,{PHI},alpha_0,beta_0\n"
        f"{_row(100, 2)},0.5,0.25\n{_row(200, 2)},0.75,-0.5\n"
    )
    p = _write(tmp_path, text)
    d = tmp_path / "d.csv"
    d.write_text("channel_delay_s\n0.0012\n", encoding="utf-8")
    t = read_modal_csv(p, _pts(), 1, read_channel_delays(d))
    assert t.alpha is not None and t.beta is not None and t.channel_delay is not None
    assert t.alpha.shape == (1, 2) and t.alpha[0, 1] == 0.75 and t.beta[0, 1] == -0.5
    assert t.channel_delay.tolist() == [0.0012]
    with pytest.raises(ImportDataError, match="channel_delay_s"):
        read_modal_csv(p, _pts(), 1)
    assert modal_blob.validate(t) == []


@pytest.mark.parametrize(
    ("text", "needle"),
    [
        (
            f"frequency_hz,sigma_per_s,{PHI},bogus\n{_row(100, 2)},1\n",
            "line 1, column bogus: unknown",
        ),
        (f"frequency_hz,sigma_per_s,{PHI},phi_1\n{_row(100, 2)},1\n", "column phi_1: duplicate"),
        (f"sigma_per_s,{PHI}\n2,{','.join(['1'] * 6)}\n", "column frequency_hz: missing"),
        (f"frequency_hz,{PHI}\n{_row(100, 2)}\n".replace("100,2,", "100,"), "damping"),
        (f"frequency_hz,sigma_per_s,loss_factor,{PHI}\n{_row(100, 2)}\n", "damping"),
        ("frequency_hz,sigma_per_s,phi_0,phi_1\n1,1,1,1\n", "column phi_2: missing"),
        (
            f"frequency_hz,sigma_per_s,{PHI},phi_6\n{_row(100, 2)},1\n",
            "column phi_6: instrument has 6",
        ),
        (
            f"frequency_hz,sigma_per_s,{PHI}\n{_row(100, 2)}\n{_row(200, 'nan')}\n",
            "line 3, column sigma_per_s",
        ),
        (
            f"frequency_hz,sigma_per_s,{PHI}\n{_row(100, 2)}\n{_row(200, 'inf')}\n",
            "line 3, column sigma_per_s",
        ),
        (
            f"frequency_hz,sigma_per_s,{PHI}\n{_row(100, 2)}\n{_row(200, -1)}\n",
            "line 3, column sigma_per_s: negative",
        ),
        (
            f"frequency_hz,sigma_per_s,{PHI}\n{_row(200, 2)}\n{_row(100, 2)}\n",
            "line 3, column frequency_hz: rows must be",
        ),
        (
            f"frequency_hz,sigma_per_s,{PHI}\n{_row(100, 2)}\n{_row(100, 2)}\n",
            "line 3, column frequency_hz: duplicate",
        ),
        (
            f"frequency_hz,sigma_per_s,{PHI}\n{_row(-5, 2)}\n",
            "line 2, column frequency_hz: must be > 0",
        ),
        (f"frequency_hz,sigma_per_s,{PHI}\n{_row(100, 2)}\n100,1,2\n", "line 3: 3 fields"),
        (
            f"frequency_hz,sigma_per_s,{PHI}\n{_row(100, 2)}\n".replace("0.1,", "x,", 1),
            "line 2, column phi_0: not a number",
        ),
        (f"frequency_hz,sigma_per_s,{PHI},alpha_0\n{_row(100, 2)},1\n", "column beta_0: missing"),
        (
            f"frequency_hz,sigma_per_s,{PHI},alpha_0,beta_0,alpha_1,beta_1\n"
            f"{_row(100, 2)},1,1,1,1\n",
            "alpha_1: instrument has 1 channels",
        ),
        (f"frequency_hz,q_factor,{PHI}\n{_row(100, 0)}\n", "line 2, column q_factor: must be > 0"),
        (f"frequency_hz,sigma_per_s,{PHI}\n", "no data rows"),
    ],
)
def test_b4_r2_rejections_name_row_and_column(tmp_path: Path, text: str, needle: str) -> None:
    """B4-R2: every rejection case names the line and column."""
    with pytest.raises(ImportDataError) as exc:
        read_modal_csv(_write(tmp_path, text), _pts(), 1, np.array([0.0]))
    assert needle in str(exc.value)
