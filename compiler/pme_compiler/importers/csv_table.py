# SPDX-License-Identifier: MIT
"""Modal table import from CSV (B4-R2). (SPEC-BODY "Modal data (offline output)")

One header row, SI units with the unit in the column name:

- ``frequency_hz`` (Omega / 2 pi) and exactly one of ``sigma_per_s`` (sigma), ``loss_factor``
  (eta, sigma = eta Omega / 2) or ``q_factor`` (sigma = Omega / (2 Q));
- ``phi_<b>`` for b = 0 .. P-1, mass-normalised, kg^(-1/2);
- optionally ``alpha_<c>`` and ``beta_<c>`` for every channel c = 0 .. C-1 (both or neither), with
  the channel delays (s) given separately.

Errors name the 1-based file line (header = line 1) and the column.
"""

from __future__ import annotations

import csv
import math
import re
from pathlib import Path

import numpy as np
from numpy.typing import NDArray

from pme_compiler.importers._common import (
    BodyModalTable,
    ImportDataError,
    sigma_from_loss,
    sigma_from_q,
)

F64 = NDArray[np.float64]
_COL = re.compile(r"^(phi|alpha|beta)_(\d+)$")
_DAMPING = ("sigma_per_s", "loss_factor", "q_factor")


def read_channel_delays(path: Path) -> F64:
    """Read ``channel_delay_s`` from a one-column CSV (header ``channel_delay_s``)."""
    with path.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.reader(fh))
    if not rows or [c.strip() for c in rows[0]] != ["channel_delay_s"]:
        raise ImportDataError(f"{path.name}, line 1: header must be exactly 'channel_delay_s'")
    out = []
    for i, row in enumerate(rows[1:], start=2):
        if len(row) != 1:
            raise ImportDataError(
                f"{path.name}, line {i}, column channel_delay_s: one value required"
            )
        out.append(_number(row[0], f"{path.name}, line {i}, column channel_delay_s"))
    return np.array(out, dtype=np.float64)


def _number(text: str, where: str) -> float:
    try:
        v = float(text)
    except ValueError:
        raise ImportDataError(f"{where}: not a number: {text!r}") from None
    if not math.isfinite(v):
        raise ImportDataError(f"{where}: value is not finite: {text!r}")
    return v


def read_modal_csv(
    path: Path,
    points: F64,
    num_channels: int,
    channel_delay: F64 | None = None,
) -> BodyModalTable:
    """Parse ``path`` into a table for an instrument with ``points`` [P, 3] and C channels."""
    p = points.shape[0]
    name = path.name
    with path.open(newline="", encoding="utf-8-sig") as fh:
        rows = list(csv.reader(fh))
    if not rows:
        raise ImportDataError(f"{name}: empty file")
    header = [h.strip() for h in rows[0]]

    seen: set[str] = set()
    for h in header:
        if h in seen:
            raise ImportDataError(f"{name}, line 1, column {h}: duplicate column")
        seen.add(h)

    phi_cols: dict[int, int] = {}
    alpha_cols: dict[int, int] = {}
    beta_cols: dict[int, int] = {}
    freq_col = -1
    damp_cols: dict[str, int] = {}
    for idx, h in enumerate(header):
        if h == "frequency_hz":
            freq_col = idx
        elif h in _DAMPING:
            damp_cols[h] = idx
        elif (m := _COL.match(h)) is not None:
            target = {"phi": phi_cols, "alpha": alpha_cols, "beta": beta_cols}[m.group(1)]
            target[int(m.group(2))] = idx
        else:
            raise ImportDataError(f"{name}, line 1, column {h}: unknown column")

    if freq_col < 0:
        raise ImportDataError(f"{name}, line 1, column frequency_hz: missing column")
    if len(damp_cols) != 1:
        raise ImportDataError(
            f"{name}, line 1, column {'/'.join(_DAMPING)}: exactly one damping column required"
        )
    for b in range(p):
        if b not in phi_cols:
            raise ImportDataError(f"{name}, line 1, column phi_{b}: missing column")
    if set(phi_cols) != set(range(p)):
        extra = sorted(set(phi_cols) - set(range(p)))[0]
        raise ImportDataError(
            f"{name}, line 1, column phi_{extra}: instrument has {p} bridge points"
        )
    have_ab = bool(alpha_cols) or bool(beta_cols)
    if have_ab:
        for c in range(num_channels):
            for label, cols in (("alpha", alpha_cols), ("beta", beta_cols)):
                if c not in cols:
                    raise ImportDataError(f"{name}, line 1, column {label}_{c}: missing column")
        for label, cols in (("alpha", alpha_cols), ("beta", beta_cols)):
            if set(cols) != set(range(num_channels)):
                extra = sorted(set(cols) - set(range(num_channels)))[0]
                raise ImportDataError(
                    f"{name}, line 1, column {label}_{extra}: "
                    f"instrument has {num_channels} channels"
                )

    data = rows[1:]
    if not data:
        raise ImportDataError(f"{name}: no data rows")
    k = len(data)
    values = np.zeros((k, len(header)))
    for r, row in enumerate(data):
        line = r + 2
        if len(row) != len(header):
            raise ImportDataError(
                f"{name}, line {line}: {len(row)} fields, header has {len(header)}"
            )
        for c, text in enumerate(row):
            values[r, c] = _number(text, f"{name}, line {line}, column {header[c]}")

    freq = values[:, freq_col]
    for r in range(k):
        if freq[r] <= 0:
            raise ImportDataError(f"{name}, line {r + 2}, column frequency_hz: must be > 0")
        if (freq[:r] == freq[r]).any():
            raise ImportDataError(f"{name}, line {r + 2}, column frequency_hz: duplicate frequency")
        if r > 0 and freq[r] < freq[r - 1]:
            raise ImportDataError(
                f"{name}, line {r + 2}, column frequency_hz: rows must be in ascending order"
            )
    omega = 2.0 * math.pi * freq

    ((dname, dcol),) = damp_cols.items()
    d = values[:, dcol]
    for r in range(k):
        bad = d[r] < 0 if dname != "q_factor" else d[r] <= 0
        if bad:
            raise ImportDataError(
                f"{name}, line {r + 2}, column {dname}: "
                + ("must be > 0" if dname == "q_factor" else "negative damping")
            )
    if dname == "sigma_per_s":
        sigma = d.copy()
    elif dname == "loss_factor":
        sigma = sigma_from_loss(omega, d)
    else:
        sigma = sigma_from_q(omega, d)

    phi = values[:, [phi_cols[b] for b in range(p)]]
    alpha = beta = delay = None
    if have_ab:
        alpha = values[:, [alpha_cols[c] for c in range(num_channels)]].T.copy()
        beta = values[:, [beta_cols[c] for c in range(num_channels)]].T.copy()
        if channel_delay is None:
            raise ImportDataError("alpha/beta columns need channel_delay_s (one value per channel)")
        delay = np.asarray(channel_delay, dtype=np.float64)
        if delay.shape != (num_channels,) or not np.all(np.isfinite(delay)) or np.any(delay < 0):
            raise ImportDataError(
                f"channel_delay_s: need {num_channels} finite values >= 0, got {delay.tolist()}"
            )

    order = np.argsort(omega, kind="stable")
    return BodyModalTable(
        omega=omega[order],
        sigma=sigma[order],
        phi=phi[order],
        points=points.copy(),
        alpha=None if alpha is None else alpha[:, order],
        beta=None if beta is None else beta[:, order],
        channel_delay=delay,
    )
