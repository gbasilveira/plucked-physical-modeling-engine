# SPDX-License-Identifier: MIT
"""Command line: ``python -m pme_compiler.importers <route> ...`` (B4).

Routes: ``csv``, ``maesstro``, ``fit``, ``helmholtz``, ``circle``, ``rectangle``. Each reads the
instrument file given by ``--instrument`` (bridge points and channels), writes the modal-table blob
and the instrument file ``--out`` (blob at ``--blob-path`` relative to it).
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import numpy as np
from numpy.typing import NDArray

from pme_compiler.body import modal_blob
from pme_compiler.importers import csv_table, helmholtz, maesstro, membrane, vector_fit
from pme_compiler.importers._common import (
    DEFAULT_BLOB_PATH,
    ImportDataError,
    bridge_positions,
    finish,
    load_base,
)
from pme_compiler.schema import InstrumentError

F64 = NDArray[np.float64]


def _floats(text: str) -> list[float]:
    return [float(t) for t in text.split(",") if t.strip()]


def _damping(p: argparse.ArgumentParser) -> None:
    p.add_argument("--loss-factor", type=float, help="eta, sigma = eta Omega / 2")
    p.add_argument("--q-factor", type=float, help="Q, sigma = Omega / (2 Q)")


def _read_responses(path: Path) -> tuple[F64, dict[tuple[int, int], np.ndarray]]:
    """Mobility data from ``.npz`` (``frequency_hz``, ``y_<a>_<b>`` complex) or CSV."""
    resp: dict[tuple[int, int], np.ndarray] = {}
    if path.suffix == ".npz":
        with np.load(path, allow_pickle=False) as z:
            freq = np.asarray(z["frequency_hz"], dtype=np.float64)
            for key in z.files:
                if key.startswith("y_"):
                    _, a, b = key.split("_")
                    resp[(int(a), int(b))] = np.asarray(z[key], dtype=np.complex128)
        return freq, resp
    with path.open(newline="", encoding="utf-8-sig") as fh:
        rows = list(csv.reader(fh))
    header = [h.strip() for h in rows[0]]
    cols = np.array([[float(x) for x in r] for r in rows[1:]])
    if "frequency_hz" not in header:
        raise ImportDataError(f"{path.name}, line 1, column frequency_hz: missing column")
    freq = cols[:, header.index("frequency_hz")]
    for h in header:
        if h.startswith("re_"):
            a, b = (int(t) for t in h[3:].split("_"))
            im = f"im_{a}_{b}"
            if im not in header:
                raise ImportDataError(f"{path.name}, line 1, column {im}: missing column")
            resp[(a, b)] = cols[:, header.index(h)] + 1j * cols[:, header.index(im)]
    return freq, resp


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="python -m pme_compiler.importers", description=__doc__)
    sub = ap.add_subparsers(dest="route", required=True)

    def common(p: argparse.ArgumentParser) -> None:
        p.add_argument("--instrument", type=Path, required=True, help="input instrument file")
        p.add_argument("--out", type=Path, required=True, help="output instrument file")
        p.add_argument("--blob-path", default=DEFAULT_BLOB_PATH)

    p = sub.add_parser("csv", help="modal table from CSV (B4-R2)")
    common(p)
    p.add_argument("--table", type=Path, required=True)
    p.add_argument("--channel-delay-csv", type=Path)
    p.add_argument("--channel-delay", type=_floats, help="comma separated seconds, one per channel")

    p = sub.add_parser("maesstro", help="MAESSTRO-layout modal basis (B4-R1)")
    common(p)
    p.add_argument("--basis", type=Path, required=True, help=".mat or .npz modal basis")

    p = sub.add_parser("fit", help="vector fit of measured mobilities (B4-R3)")
    common(p)
    p.add_argument("--mobility", type=Path, required=True, help=".npz or .csv")
    p.add_argument("--pairs", type=int, required=True, help="model order in conjugate pole pairs")
    p.add_argument("--f-min", type=float)
    p.add_argument("--f-max", type=float)
    p.add_argument("--max-iter", type=int, default=40)
    p.add_argument("--tol", type=float, default=1e-9)
    p.add_argument("--driving", type=int, default=0, help="driving-point index")

    p = sub.add_parser("helmholtz", help="merge a Helmholtz mode into the instrument's table (B4-R4)")
    common(p)
    p.add_argument("--volume", type=float, required=True, help="m^3")
    p.add_argument("--hole-area", type=float, required=True, help="m^2")
    p.add_argument("--plate-thickness", type=float, default=0.0, help="m")
    p.add_argument("--neck-length", type=float, help="m (overrides t + 1.7 r)")
    p.add_argument("--c0", type=float, default=helmholtz.C0_20C)
    p.add_argument("--q-factor", type=float, required=True)
    p.add_argument("--phi", type=_floats, required=True, help="Phi_H at the bridge points, kg^(-1/2)")
    p.add_argument("--alpha", type=_floats)
    p.add_argument("--beta", type=_floats)

    for name, text in (("circle", "circular"), ("rectangle", "rectangular")):
        p = sub.add_parser(name, help=f"{text} membrane modes (B4-R5)")
        common(p)
        p.add_argument("--tension", type=float, required=True, help="N/m")
        p.add_argument("--areal-density", type=float, required=True, help="kg/m^2")
        p.add_argument("--f-max", type=float, required=True, help="Hz")
        if name == "circle":
            p.add_argument("--radius", type=float, required=True)
        else:
            p.add_argument("--size", type=_floats, required=True, help="a,b in m")
        _damping(p)
    return ap


def run(args: argparse.Namespace) -> str:
    base = load_base(args.instrument)
    pts = bridge_positions(base.desc)
    route: str = args.route
    source = "ModalTable"
    summary = ""
    if route == "csv":
        delay = None
        if args.channel_delay_csv is not None:
            delay = csv_table.read_channel_delays(args.channel_delay_csv)
        elif args.channel_delay is not None:
            delay = np.array(args.channel_delay)
        table = csv_table.read_modal_csv(args.table, pts, len(base.desc.channels), delay)
    elif route == "maesstro":
        table = maesstro.read_maesstro(args.basis, pts)
    elif route == "fit":
        freq, resp = _read_responses(args.mobility)
        result = vector_fit.vector_fit(
            freq, resp, args.pairs, args.f_min, args.f_max, args.max_iter, args.tol
        )
        table = vector_fit.fit_to_table(result, pts, args.driving)
        modal = vector_fit.modal_rms_error(table, freq, resp, args.f_min, args.f_max)
        source = "MeasuredMobilityFit"
        summary = (
            f"; {args.pairs} pole pairs, {result.iterations} iterations "
            f"(converged: {result.converged}), RMS relative error {result.rms_relative_error:.4g} "
            f"(modal table only: {modal:.4g})"
        )
    elif route == "helmholtz":
        if not base.desc.body.modalTableHash:
            raise ImportDataError("the instrument has no modal table to merge the mode into")
        entry = next(b for b in base.blobs if b.hash == base.desc.body.modalTableHash)
        table = modal_blob.read(entry, args.instrument.parent)
        f_h = helmholtz.helmholtz_frequency(
            args.volume, args.hole_area, args.plate_thickness, args.neck_length, args.c0
        )
        table = helmholtz.merge_mode(
            table,
            f_h,
            args.q_factor,
            np.array(args.phi),
            None if args.alpha is None else np.array(args.alpha),
            None if args.beta is None else np.array(args.beta),
        )
        source = base.desc.body.source
        summary = f"; Helmholtz frequency {f_h:.4f} Hz"
    else:
        if (args.loss_factor is None) == (args.q_factor is None):
            raise ImportDataError("give exactly one of --loss-factor and --q-factor")
        kw = {"loss_factor": args.loss_factor, "q_factor": args.q_factor}
        if route == "circle":
            table = membrane.circular_table(
                args.radius, args.tension, args.areal_density, pts, args.f_max, **kw
            )
        else:
            if len(args.size) != 2:
                raise ImportDataError("--size needs a,b")
            table = membrane.rectangular_table(
                args.size[0], args.size[1], args.tension, args.areal_density, pts, args.f_max, **kw
            )
    finish(base, table, source, args.out, args.blob_path)
    return f"{route}: wrote {args.out} with {table.omega.size} modes{summary}"


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        print(run(args))
    except (ImportDataError, InstrumentError, OSError, ValueError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
