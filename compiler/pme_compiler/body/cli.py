# SPDX-License-Identifier: MIT
"""Command line: ``python -m pme_compiler.body <instrument.json> -o <out-dir>`` (B2-R7).

Compiles the instrument's ``PlateModel`` body to a modal table: writes ``<out-dir>/instrument.json``
(``body.source`` = ``ModalTable``, the plate block kept as provenance) and the blob
``<out-dir>/blobs/body-modal-table.bin``. Other blobs of the input are copied beside it.
"""

from __future__ import annotations

import argparse
import dataclasses
import shutil
import sys
from pathlib import Path

from pme_compiler.body import compile as plate_compile
from pme_compiler.body import modal_blob
from pme_compiler.schema import InstrumentError, dump, load


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="python -m pme_compiler.body",
        description="Compile an instrument's plate body to a mass-normalised modal table.",
    )
    ap.add_argument("instrument", type=Path, help="instrument file with a PlateModel body")
    ap.add_argument("-o", "--out-dir", type=Path, required=True, help="output directory")
    ap.add_argument(
        "--mesh-size", type=float, default=None, help="element size in m (default auto)"
    )
    ap.add_argument("--f-max", type=float, default=None, help="override plate.fMax (Hz)")
    ap.add_argument(
        "--grid-order",
        type=int,
        default=plate_compile.DEFAULT_GRID_ORDER,
        help="quadrature order of the radiation grid; 0 omits the grid (default %(default)s)",
    )
    ap.add_argument(
        "--force",
        action="store_true",
        help="recompile from the plate block of an instrument that already has a modal table",
    )
    return ap


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        file = load(args.instrument, verify_blobs=True)
    except InstrumentError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    body = file.desc.body
    if body.source != "PlateModel" and not args.force:
        print(
            f"error: body.source is {body.source}; the body is already compiled "
            "(use --force to recompile from the plate block)",
            file=sys.stderr,
        )
        return 2
    try:
        result = plate_compile.compile_plate(
            body.plate,
            [bp.position for bp in file.desc.bridgePoints],
            mesh_size=args.mesh_size,
            f_max=args.f_max,
            grid_order=args.grid_order,
        )
    except (ValueError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    src_dir = args.instrument.resolve().parent
    old = [b for b in file.blobs if b.hash == body.modalTableHash]
    keep = [b for b in file.blobs if b not in old]
    for b in keep:
        dest = out_dir / b.path
        dest.parent.mkdir(parents=True, exist_ok=True)
        if (src_dir / b.path).resolve() != dest.resolve():
            shutil.copyfile(src_dir / b.path, dest)
    base = dataclasses.replace(file, blobs=keep)
    entry = modal_blob.write(result.table, out_dir, modal_blob.DEFAULT_RELPATH, "pme_compiler.body")
    compiled = modal_blob.attach(base, entry, "ModalTable")
    dump(compiled, out_dir / "instrument.json")
    load(out_dir / "instrument.json", verify_blobs=True)  # round trip
    info = result.info
    print(
        f"compiled {file.desc.name}: {info.n_modes} modes ({info.n_rigid_dropped} rigid-body "
        f"dropped), {info.n_dofs} dofs, {info.n_elements} elements of {info.mesh_size:.4g} m, "
        f"{info.windows} window(s), {info.seconds:.2f} s -> {out_dir / 'instrument.json'}"
    )
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
