# SPDX-License-Identifier: MIT
"""Golden-feature files (P0.3-R4): Python mirror of ``pme/analysis/golden.hpp``.

Reader, writer and comparator for the JSON format described by ``golden-features.schema.json``.
The comparator returns the same verdicts and feature names as the C++ one.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

INF = math.inf


class GoldenError(ValueError):
    """Malformed golden-feature file."""


@dataclass
class GoldenDecay2:
    sigma_per_s: float
    knee_s: float


@dataclass
class GoldenPartial:
    index: int
    frequency_hz: float
    amplitude: float
    sigma_per_s: float
    t60_s: float
    second_decay: GoldenDecay2 | None = None


@dataclass
class GoldenEnvelope:
    frequencies_hz: list[float]
    magnitude_db: list[float]


@dataclass
class GoldenEnergy:
    max_relative_drift: float
    max_relative_step_change: float


@dataclass
class PartialTolerance:
    frequency_cents: float | None = None
    amplitude_db: float | None = None
    decay_rate_relative: float | None = None
    knee_relative: float | None = None


@dataclass
class GoldenTolerances:
    """Defaults: SPEC-ORC for frequency, decay, envelope, contact time; planner's for the rest."""

    frequency_cents: float = 0.01
    amplitude_db: float = 0.1
    decay_rate_relative: float = 0.005
    spectral_envelope_db: float = 1.0
    contact_time_relative: float = 0.01
    energy_absolute: float = 1e-9
    knee_relative: float = 0.05
    partials: dict[int, PartialTolerance] = field(default_factory=dict)


@dataclass
class GoldenFeatures:
    scenario: str
    sample_rate_hz: float
    producer_tool: str
    producer_version: str
    partials: list[GoldenPartial] = field(default_factory=list)
    spectral_envelope: GoldenEnvelope | None = None
    contact_time_s: float | None = None
    energy: GoldenEnergy | None = None
    tolerances: GoldenTolerances = field(default_factory=GoldenTolerances)
    schema_version: int = 1


@dataclass
class FeatureResult:
    feature: str
    difference: float
    tolerance: float
    unit: str
    passed: bool
    note: str = ""


@dataclass
class ComparisonResult:
    passed: bool = True
    results: list[FeatureResult] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


# ---- Parsing -----------------------------------------------------------------------------------


def _fail(message: str) -> GoldenError:
    return GoldenError(f"golden features: {message}")


def _require(obj: Any, key: str, where: str) -> Any:
    if not isinstance(obj, dict) or key not in obj:
        raise _fail(f"missing field '{where}{key}'")
    return obj[key]


def _number(obj: Any, key: str, where: str) -> float:
    v = _require(obj, key, where)
    if isinstance(v, bool) or not isinstance(v, int | float):
        raise _fail(f"field '{where}{key}' must be a number")
    return float(v)


def _optional_number(obj: Any, key: str, where: str) -> float | None:
    return _number(obj, key, where) if isinstance(obj, dict) and key in obj else None


def _text(obj: Any, key: str, where: str) -> str:
    v = _require(obj, key, where)
    if not isinstance(v, str):
        raise _fail(f"field '{where}{key}' must be a string")
    return v


def _array(obj: Any, key: str, where: str) -> list[float]:
    v = _require(obj, key, where)
    if not isinstance(v, list) or not all(
        isinstance(e, int | float) and not isinstance(e, bool) for e in v
    ):
        raise _fail(f"field '{where}{key}' must be an array of numbers")
    return [float(e) for e in v]


def parse_golden(text: str) -> GoldenFeatures:
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as exc:
        raise _fail(f"not valid JSON: {exc}") from exc
    if _number(doc, "schema_version", "") != 1.0:
        raise _fail(f"unsupported schema_version {doc['schema_version']}")
    producer = _require(doc, "producer", "")
    g = GoldenFeatures(
        scenario=_text(doc, "scenario", ""),
        sample_rate_hz=_number(doc, "sample_rate_hz", ""),
        producer_tool=_text(producer, "tool", "producer."),
        producer_version=_text(producer, "version", "producer."),
    )
    plist = _require(doc, "partials", "")
    if not isinstance(plist, list):
        raise _fail("field 'partials' must be an array")
    for i, p in enumerate(plist):
        where = f"partials[{i}]."
        second = None
        if isinstance(p, dict) and "second_decay" in p:
            s = p["second_decay"]
            second = GoldenDecay2(
                _number(s, "sigma_per_s", where + "second_decay."),
                _number(s, "knee_s", where + "second_decay."),
            )
        g.partials.append(
            GoldenPartial(
                index=int(_number(p, "index", where)),
                frequency_hz=_number(p, "frequency_hz", where),
                amplitude=_number(p, "amplitude", where),
                sigma_per_s=_number(p, "sigma_per_s", where),
                t60_s=_number(p, "t60_s", where),
                second_decay=second,
            )
        )
    if "spectral_envelope" in doc:
        e = doc["spectral_envelope"]
        env = GoldenEnvelope(
            _array(e, "frequencies_hz", "spectral_envelope."),
            _array(e, "magnitude_db", "spectral_envelope."),
        )
        if len(env.frequencies_hz) != len(env.magnitude_db):
            raise _fail("spectral_envelope arrays differ in length")
        g.spectral_envelope = env
    g.contact_time_s = _optional_number(doc, "contact_time_s", "")
    if "energy" in doc:
        en = doc["energy"]
        g.energy = GoldenEnergy(
            _number(en, "max_relative_drift", "energy."),
            _number(en, "max_relative_step_change", "energy."),
        )
    tol = doc.get("tolerances")
    if tol is not None:
        default = tol.get("default", {})
        t = g.tolerances
        for name in (
            "frequency_cents",
            "amplitude_db",
            "decay_rate_relative",
            "spectral_envelope_db",
            "contact_time_relative",
            "energy_absolute",
            "knee_relative",
        ):
            v = _optional_number(default, name, "tolerances.default.")
            if v is not None:
                setattr(t, name, v)
        for key, value in tol.get("partials", {}).items():
            where = f"tolerances.partials.{key}."
            try:
                index = int(key)
            except ValueError as exc:
                raise _fail(f"tolerances.partials key '{key}' is not a partial index") from exc
            t.partials[index] = PartialTolerance(
                _optional_number(value, "frequency_cents", where),
                _optional_number(value, "amplitude_db", where),
                _optional_number(value, "decay_rate_relative", where),
                _optional_number(value, "knee_relative", where),
            )
    return g


def serialize_golden(g: GoldenFeatures) -> str:
    doc: dict[str, Any] = {
        "schema_version": g.schema_version,
        "scenario": g.scenario,
        "sample_rate_hz": g.sample_rate_hz,
        "producer": {"tool": g.producer_tool, "version": g.producer_version},
        "units": {
            "frequency": "Hz",
            "sigma": "1/s",
            "t60": "s",
            "amplitude": "linear",
            "magnitude_db": "dB",
            "contact_time": "s",
            "tolerances": "see schema",
        },
    }
    partials = []
    for p in g.partials:
        jp: dict[str, Any] = {
            "index": p.index,
            "frequency_hz": p.frequency_hz,
            "amplitude": p.amplitude,
            "sigma_per_s": p.sigma_per_s,
            "t60_s": p.t60_s,
        }
        if p.second_decay is not None:
            jp["second_decay"] = {
                "sigma_per_s": p.second_decay.sigma_per_s,
                "knee_s": p.second_decay.knee_s,
            }
        partials.append(jp)
    doc["partials"] = partials
    if g.spectral_envelope is not None:
        doc["spectral_envelope"] = {
            "frequencies_hz": g.spectral_envelope.frequencies_hz,
            "magnitude_db": g.spectral_envelope.magnitude_db,
        }
    if g.contact_time_s is not None:
        doc["contact_time_s"] = g.contact_time_s
    if g.energy is not None:
        doc["energy"] = {
            "max_relative_drift": g.energy.max_relative_drift,
            "max_relative_step_change": g.energy.max_relative_step_change,
        }
    t = g.tolerances
    tol: dict[str, Any] = {
        "default": {
            "frequency_cents": t.frequency_cents,
            "amplitude_db": t.amplitude_db,
            "decay_rate_relative": t.decay_rate_relative,
            "spectral_envelope_db": t.spectral_envelope_db,
            "contact_time_relative": t.contact_time_relative,
            "energy_absolute": t.energy_absolute,
            "knee_relative": t.knee_relative,
        }
    }
    if t.partials:
        overrides: dict[str, Any] = {}
        for index in sorted(t.partials):
            pt = t.partials[index]
            o = {
                k: v
                for k, v in (
                    ("frequency_cents", pt.frequency_cents),
                    ("amplitude_db", pt.amplitude_db),
                    ("decay_rate_relative", pt.decay_rate_relative),
                    ("knee_relative", pt.knee_relative),
                )
                if v is not None
            }
            overrides[str(index)] = o
        tol["partials"] = overrides
    doc["tolerances"] = tol
    return json.dumps(doc, indent=2) + "\n"


def read_golden_file(path: str | Path) -> GoldenFeatures:
    try:
        text = Path(path).read_text(encoding="utf-8")
    except OSError as exc:
        raise _fail(f"cannot open {path}") from exc
    return parse_golden(text)


def write_golden_file(path: str | Path, features: GoldenFeatures) -> None:
    Path(path).write_text(serialize_golden(features), encoding="utf-8")


# ---- Comparison --------------------------------------------------------------------------------


def _cents(candidate: float, reference: float) -> float:
    if candidate > 0.0 and reference > 0.0:
        return abs(1200.0 * math.log2(candidate / reference))
    return 0.0 if candidate == reference else INF


def _db(candidate: float, reference: float) -> float:
    if candidate > 0.0 and reference > 0.0:
        return abs(20.0 * math.log10(candidate / reference))
    return 0.0 if candidate == reference else INF


def _relative(candidate: float, reference: float) -> float:
    d = abs(candidate - reference)
    return d if reference == 0.0 else d / abs(reference)


def _add(
    out: ComparisonResult,
    feature: str,
    difference: float,
    tolerance: float,
    unit: str,
    note: str = "",
) -> None:
    ok = difference <= tolerance  # False for NaN
    out.passed = out.passed and ok
    out.results.append(FeatureResult(feature, difference, tolerance, unit, ok, note))


def _missing(out: ComparisonResult, feature: str, note: str) -> None:
    out.passed = False
    out.results.append(FeatureResult(feature, INF, 0.0, "-", False, note))


def compare_golden(ref: GoldenFeatures, cand: GoldenFeatures) -> ComparisonResult:
    """Compare ``cand`` against ``ref`` using the reference's tolerances."""
    out = ComparisonResult()
    if ref.scenario != cand.scenario:
        out.notes.append(
            f"scenario names differ: reference '{ref.scenario}', candidate '{cand.scenario}'"
        )
    tol = ref.tolerances
    by_index = {p.index: p for p in cand.partials}
    for rp in ref.partials:
        name = f"partial[{rp.index}]"
        cp = by_index.get(rp.index)
        if cp is None:
            _missing(out, name, "partial present in the reference is missing from the candidate")
            continue
        over = tol.partials.get(rp.index, PartialTolerance())
        f_tol = tol.frequency_cents if over.frequency_cents is None else over.frequency_cents
        a_tol = tol.amplitude_db if over.amplitude_db is None else over.amplitude_db
        d_tol = (
            tol.decay_rate_relative
            if over.decay_rate_relative is None
            else over.decay_rate_relative
        )
        _add(out, f"{name}.frequency", _cents(cp.frequency_hz, rp.frequency_hz), f_tol, "cent")
        _add(out, f"{name}.amplitude", _db(cp.amplitude, rp.amplitude), a_tol, "dB")
        _add(
            out, f"{name}.decay_rate", _relative(cp.sigma_per_s, rp.sigma_per_s), d_tol, "relative"
        )
        if rp.second_decay is not None:
            if cp.second_decay is None:
                _missing(
                    out,
                    f"{name}.second_decay_rate",
                    "second decay present in the reference is missing from the candidate",
                )
            else:
                _add(
                    out,
                    f"{name}.second_decay_rate",
                    _relative(cp.second_decay.sigma_per_s, rp.second_decay.sigma_per_s),
                    d_tol,
                    "relative",
                )
                _add(
                    out,
                    f"{name}.knee_s",
                    _relative(cp.second_decay.knee_s, rp.second_decay.knee_s),
                    tol.knee_relative if over.knee_relative is None else over.knee_relative,
                    "relative",
                )
    known = {p.index for p in ref.partials}
    for c in cand.partials:
        if c.index not in known:
            out.notes.append(f"candidate has partial {c.index} that the reference does not")

    if ref.spectral_envelope is not None:
        if cand.spectral_envelope is None:
            _missing(
                out, "spectral_envelope", "reference has a spectral envelope, candidate has none"
            )
        else:
            re, ce = ref.spectral_envelope, cand.spectral_envelope
            same_grid = len(re.frequencies_hz) == len(ce.frequencies_hz) and all(
                abs(a - b) <= 1e-9 * abs(a)
                for a, b in zip(re.frequencies_hz, ce.frequencies_hz, strict=True)
            )
            if not same_grid:
                _missing(out, "spectral_envelope", "frequency grids differ")
            else:
                worst, at = 0.0, 0
                for i, (a, b) in enumerate(zip(re.magnitude_db, ce.magnitude_db, strict=True)):
                    d = abs(b - a)
                    if not d <= worst:
                        worst, at = d, i
                where = re.frequencies_hz[at] if re.frequencies_hz else 0.0
                _add(
                    out,
                    "spectral_envelope",
                    worst,
                    tol.spectral_envelope_db,
                    "dB",
                    f"largest difference at {where:.6f} Hz",
                )
    if ref.contact_time_s is not None:
        if cand.contact_time_s is None:
            _missing(out, "contact_time", "reference has a contact time, candidate has none")
        else:
            _add(
                out,
                "contact_time",
                _relative(cand.contact_time_s, ref.contact_time_s),
                tol.contact_time_relative,
                "relative",
            )
    if ref.energy is not None:
        if cand.energy is None:
            _missing(out, "energy", "reference has energy values, candidate has none")
        else:
            _add(
                out,
                "energy.max_relative_drift",
                abs(cand.energy.max_relative_drift - ref.energy.max_relative_drift),
                tol.energy_absolute,
                "absolute",
            )
            _add(
                out,
                "energy.max_relative_step_change",
                abs(cand.energy.max_relative_step_change - ref.energy.max_relative_step_change),
                tol.energy_absolute,
                "absolute",
            )
    return out


def format_comparison(result: ComparisonResult, failures_only: bool = True) -> str:
    lines = []
    for r in result.results:
        if failures_only and r.passed:
            continue
        line = (
            f"{'ok  ' if r.passed else 'FAIL'} {r.feature:<34} difference {r.difference:.6g} "
            f"{r.unit} (tolerance {r.tolerance:.6g})"
        )
        if r.note:
            line += f" - {r.note}"
        lines.append(line)
    lines += [f"note: {n}" for n in result.notes]
    failed = sum(1 for r in result.results if not r.passed)
    lines.append(
        f"{'PASS' if result.passed else 'FAIL'}: {failed} of {len(result.results)} "
        "features outside tolerance"
    )
    return "\n".join(lines) + "\n"
