# SPDX-License-Identifier: MIT
"""B4-T3 and B4-R1: the MAESSTRO-layout adapter on the synthetic fixture yields a valid blob."""

from __future__ import annotations

import importlib.util
import math
import shutil
from pathlib import Path

import numpy as np
import pytest
from scipy.io import loadmat, savemat

from pme_compiler.body import modal_blob
from pme_compiler.importers._common import ImportDataError, bridge_positions, finish, load_base
from pme_compiler.importers.maesstro import read_maesstro
from pme_compiler.schema import load

ROOT = Path(__file__).resolve().parents[2]
DATA = Path(__file__).resolve().parent / "data" / "b4"
FIXTURE = DATA / "synthetic_maesstro.mat"
GUITAR = ROOT / "instruments" / "examples" / "test-guitar-6" / "instrument.json"

A, B, RHO_H = 0.30, 0.40, 3.2


def _instrument(tmp_path: Path) -> Path:
    dst = tmp_path / "instrument.json"
    shutil.copy(GUITAR, dst)
    return dst


def _analytic(m: int, n: int, x: float, y: float) -> float:
    return (
        2.0
        / math.sqrt(RHO_H * A * B)
        * math.sin(m * math.pi * x / A)
        * math.sin(n * math.pi * y / B)
    )


def test_b4_t3_synthetic_sample_yields_a_valid_blob(tmp_path: Path) -> None:
    """B4-T3: validate, load with verify_blobs, bit-identical read-back, fixture values."""
    inst = _instrument(tmp_path)
    base = load_base(inst)
    pts = bridge_positions(base.desc)
    table = read_maesstro(FIXTURE, pts)
    out = tmp_path / "out" / "instrument.json"
    out.parent.mkdir()
    written = finish(base, table, "ModalTable", out)

    assert modal_blob.validate(table) == []
    loaded = load(out, verify_blobs=True)
    assert loaded.desc.body.source == "ModalTable"
    assert loaded.desc.body.modalTableHash == loaded.blobs[0].hash == written.blobs[0].hash
    back = modal_blob.read(loaded.blobs[0], out.parent)
    for name in ("omega", "sigma", "phi", "points"):
        assert np.array_equal(getattr(back, name), getattr(table, name))

    # Frequencies equal the fixture's (sorted ascending), sigma = eta Omega / 2.
    raw = loadmat(FIXTURE)
    order = np.argsort(raw["frequencies"].ravel())
    freq = raw["frequencies"].ravel()[order]
    eta = raw["loss_factor"].ravel()[order]
    assert np.array_equal(table.omega, 2 * math.pi * freq)
    assert np.allclose(table.sigma, 0.5 * eta * table.omega, rtol=1e-15, atol=0)
    assert table.phi.shape == (9, 6)

    # Shapes: the fixture is scaled to unit peak; the adapter mass-normalises to the analytic
    # Phi = 2 / sqrt(rho h a b) sin sin. Bridge point 0 (0.12, 0.15) is a mesh node: exact up to
    # the sum rounding; the others are interpolated linearly (error O(pitch^2)).
    modes = [(m, n) for m in range(1, 4) for n in range(1, 4)]
    f_of = {
        mn: math.pi / 2 * math.sqrt(8.0 / RHO_H) * ((mn[0] / A) ** 2 + (mn[1] / B) ** 2)
        for mn in modes
    }
    for k, mn in enumerate(sorted(modes, key=lambda t: f_of[t])):
        assert table.omega[k] == pytest.approx(2 * math.pi * f_of[mn], rel=1e-12)
        assert table.phi[k, 0] == pytest.approx(_analytic(*mn, 0.12, 0.15), abs=1e-12)
        for b in range(1, 6):
            want = _analytic(*mn, pts[b, 0], pts[b, 1])
            assert table.phi[k, b] == pytest.approx(want, abs=0.02 * 2.0 / math.sqrt(RHO_H * A * B))


def test_b4_r1_checks_declared_mass_normalisation(tmp_path: Path) -> None:
    """B4-R1: shapes declared mass-normalised are checked; undeclared ones need nodal_mass."""
    raw = {k: v for k, v in loadmat(FIXTURE).items() if not k.startswith("__")}
    pts = bridge_positions(load_base(GUITAR).desc)

    bad = dict(raw, normalisation=np.array("mass"))
    savemat(tmp_path / "declared.mat", bad)
    with pytest.raises(ImportDataError, match="mass-normalised"):
        read_maesstro(tmp_path / "declared.mat", pts)

    no_mass = {k: v for k, v in raw.items() if k != "nodal_mass"}
    savemat(tmp_path / "nomass.mat", no_mass)
    with pytest.raises(ImportDataError, match="nodal_mass"):
        read_maesstro(tmp_path / "nomass.mat", pts)

    # A file that is already mass-normalised passes unchanged when declared.
    t = read_maesstro(FIXTURE, pts)
    mass = raw["nodal_mass"].ravel()
    scale = 1.0 / np.sqrt(mass @ raw["modes"] ** 2)
    good = dict(raw, modes=raw["modes"] * scale, normalisation=np.array("mass"))
    savemat(tmp_path / "good.mat", good)
    t2 = read_maesstro(tmp_path / "good.mat", pts)
    assert np.array_equal(t.phi, t2.phi)


def test_b4_r1_rejections(tmp_path: Path) -> None:
    """B4-R1: missing variables, shape errors and points outside the mesh are rejected."""
    raw = {k: v for k, v in loadmat(FIXTURE).items() if not k.startswith("__")}
    pts = bridge_positions(load_base(GUITAR).desc)
    savemat(tmp_path / "nofreq.mat", {k: v for k, v in raw.items() if k != "frequencies"})
    with pytest.raises(ImportDataError, match="frequencies"):
        read_maesstro(tmp_path / "nofreq.mat", pts)
    savemat(tmp_path / "shape.mat", dict(raw, modes=raw["modes"][:, :4]))
    with pytest.raises(ImportDataError, match="modes must be"):
        read_maesstro(tmp_path / "shape.mat", pts)
    outside = pts.copy()
    outside[2, 0] = 5.0
    with pytest.raises(ImportDataError, match="bridge point 2"):
        read_maesstro(FIXTURE, outside)
    with pytest.raises(ImportDataError, match="unsupported"):
        read_maesstro(tmp_path / "x.txt", pts)


def test_b4_r1_names_mapping_and_npz(tmp_path: Path) -> None:
    """B4-R1: the variable names can be remapped and .npz files are read."""
    raw = {k: v for k, v in loadmat(FIXTURE).items() if not k.startswith("__")}
    pts = bridge_positions(load_base(GUITAR).desc)
    renamed = {
        "fq": raw["frequencies"],
        "eta": raw["loss_factor"],
        "xy": raw["nodes"],
        "psi": raw["modes"],
        "mass": raw["nodal_mass"],
        "normalisation": raw["normalisation"],
    }
    np.savez(tmp_path / "r.npz", **renamed)
    t = read_maesstro(
        tmp_path / "r.npz",
        pts,
        names={
            "frequencies": "fq",
            "loss_factor": "eta",
            "nodes": "xy",
            "modes": "psi",
            "nodal_mass": "mass",
        },
    )
    assert np.array_equal(t.phi, read_maesstro(FIXTURE, pts).phi)


def test_b4_r1_fixture_matches_its_generator(tmp_path: Path) -> None:
    """B4-R1: the committed synthetic fixture is what its generator writes."""
    spec = importlib.util.spec_from_file_location("make_fixture", DATA / "make_maesstro_fixture.py")
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    fresh = mod.build()
    committed = loadmat(FIXTURE)
    for k, v in fresh.items():
        if k == "normalisation":
            continue
        assert np.allclose(
            np.asarray(committed[k]).squeeze(), np.asarray(v).squeeze(), rtol=0, atol=1e-12
        )
