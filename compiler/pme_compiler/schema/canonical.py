# SPDX-License-Identifier: MIT
"""Canonical text form of an instrument file (B1-R4, docs/dev/instrument-format.md).

2-space indentation, object keys in insertion order (the caller inserts them in schema
``properties`` order), numbers as the shortest decimal that round-trips to the same float64 laid
out like nlohmann/json lays out numbers, LF line ends and a final newline. The C++ writer
(``engine/src/runtime/loader``) produces byte-identical text.
"""

from __future__ import annotations

import json
import math
from typing import Any

# Layout thresholds of nlohmann/json's number formatting (format_buffer with min_exp -4, max_exp 15).
_MIN_EXP = -4
_MAX_EXP = 15


def format_double(value: float) -> str:
    """Return the canonical text of a finite float64 (always has a '.' or an exponent)."""
    if not math.isfinite(value):
        raise ValueError("non-finite number in an instrument file")
    if value == 0.0:
        return "-0.0" if math.copysign(1.0, value) < 0 else "0.0"
    sign = "-" if value < 0 else ""
    # repr() is the shortest round-tripping decimal; recover its digits and decimal exponent.
    digits, exponent = _digits_and_exponent(repr(abs(value)))
    k = len(digits)
    n = k + exponent  # value = 0.DIGITS * 10**n
    if k <= n <= _MAX_EXP:
        body = digits + "0" * (n - k) + ".0"
    elif 0 < n <= _MAX_EXP:
        body = digits[:n] + "." + digits[n:]
    elif _MIN_EXP < n <= 0:
        body = "0." + "0" * (-n) + digits
    else:
        e = n - 1
        lead = digits[0] if k == 1 else digits[0] + "." + digits[1:]
        body = f"{lead}e{'-' if e < 0 else '+'}{abs(e):02d}"
    return sign + body


def _digits_and_exponent(text: str) -> tuple[str, int]:
    """Split ``repr(float)`` text into significant digits (no trailing zeros) and the exponent of
    the last digit, so that value = int(digits) * 10**exponent."""
    mant, _, exp = text.partition("e")
    exp10 = int(exp) if exp else 0
    whole, _, frac = mant.partition(".")
    digits = whole + frac
    exp10 -= len(frac)
    stripped = digits.lstrip("0")
    digits = stripped.rstrip("0")
    exp10 += len(stripped) - len(digits)
    return digits, exp10


def dumps_value(value: Any, indent: int = 0) -> str:
    """Serialise a JSON-like tree (dict, list, str, bool, int, float, None) canonically."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return format_double(value)
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if value is None:
        return "null"
    pad = " " * (indent + 2)
    end = " " * indent
    if isinstance(value, dict):
        if not value:
            return "{}"
        items = [f"{pad}{json.dumps(k, ensure_ascii=False)}: {dumps_value(v, indent + 2)}" for k, v in value.items()]
        return "{\n" + ",\n".join(items) + "\n" + end + "}"
    if isinstance(value, list | tuple):
        if not value:
            return "[]"
        items = [pad + dumps_value(v, indent + 2) for v in value]
        return "[\n" + ",\n".join(items) + "\n" + end + "]"
    raise TypeError(f"cannot serialise {type(value).__name__}")


def dumps_document(value: Any) -> str:
    """Canonical document text: the tree followed by one LF."""
    return dumps_value(value) + "\n"
