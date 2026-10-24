# SPDX-License-Identifier: MIT
# ruff: noqa: E501
"""Generate the JSON Schema, the Python models and the C++ reader/writer from ``fields.py``.

    python -m pme_compiler.schema.generate          # write the three files
    python -m pme_compiler.schema.generate --check  # exit 1 if a committed file is stale

Outputs (B1-R1, B1-R4):

* ``compiler/pme_compiler/schema/instrument.schema.json``
* ``compiler/pme_compiler/schema/models.py``
* ``engine/src/runtime/loader/fields_generated.hpp``
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from pme_compiler.schema.fields import (
    CURRENT_VERSION,
    ENUMS,
    FACTORY,
    FORMAT_NAME,
    REQUIRED,
    SCHEMA_ID,
    STRUCTS,
    Field,
    Struct,
)

SCHEMA_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCHEMA_DIR.parents[2]
SCHEMA_PATH = SCHEMA_DIR / "instrument.schema.json"
MODELS_PATH = SCHEMA_DIR / "models.py"
CPP_PATH = REPO_ROOT / "engine" / "src" / "runtime" / "loader" / "fields_generated.hpp"


def _clean(value: float) -> float | int:
    """Integral floats become ints so that the schema reads naturally."""
    return int(value) if float(value).is_integer() else value


def _description(f: Field, struct_spec: str) -> str:
    return f"{f.desc} ({f.spec or struct_spec})"


INT32_MIN = -(2**31)
INT32_MAX = 2**31 - 1
INT_SAFE_MAX = 2**53 - 1  # manifest integers (C++ int64) stay exactly representable in JSON tools


def effective_bounds(f: Field, cpp: bool) -> tuple[float | None, float | None]:
    """Bounds of a number; integer members also carry the width of their C++ type."""
    lo, hi = f.lo, f.hi
    if f.kind in ("int", "intarray"):
        lo = (INT32_MIN if cpp else -INT_SAFE_MAX) if lo is None else lo
        hi = (INT32_MAX if cpp else INT_SAFE_MAX) if hi is None else hi
    return lo, hi


def _bounds(f: Field, cpp: bool = True) -> dict[str, Any]:
    out: dict[str, Any] = {}
    lo, hi = effective_bounds(f, cpp)
    if lo is not None:
        out["exclusiveMinimum" if f.lo_ex else "minimum"] = _clean(lo)
    if hi is not None:
        out["exclusiveMaximum" if f.hi_ex else "maximum"] = _clean(hi)
    return out


def _property(f: Field, s: Struct) -> dict[str, Any]:
    """The schema of one member."""
    cpp = s.cpp
    prop: dict[str, Any] = {}
    kind = f.kind
    if kind in ("obj", "optobj"):
        prop["$ref"] = f"#/$defs/{f.ref}"
        prop["description"] = _description(f, s.spec)
        return prop
    if kind == "objarray":
        prop["type"] = "array"
        prop["items"] = {"$ref": f"#/$defs/{f.ref}"}
        if f.min_items:
            prop["minItems"] = f.min_items
        prop["description"] = _description(f, s.spec)
        return prop
    if kind in ("numarray", "intarray"):
        item_type = "number" if kind == "numarray" else "integer"
        item: dict[str, Any] = {"type": item_type, "x-unit": f.unit, **_bounds(f, cpp)}
        prop["type"] = "array"
        prop["items"] = item
        if f.min_items:
            prop["minItems"] = f.min_items
        prop["x-unit"] = f.unit
        prop["description"] = _description(f, s.spec)
        return prop
    if kind == "num":
        prop["type"] = "number"
        prop["x-unit"] = f.unit
        prop.update(_bounds(f, cpp))
    elif kind == "int":
        prop["type"] = "integer"
        prop["x-unit"] = f.unit
        prop.update(_bounds(f, cpp))
        if f.one_of:
            prop["enum"] = list(f.one_of)
    elif kind == "bool":
        prop["type"] = "boolean"
    elif kind == "str":
        prop["type"] = "string"
        if f.pattern:
            prop["pattern"] = f.pattern
    elif kind == "enum":
        prop["type"] = "string"
        prop["enum"] = list(ENUMS[f.ref])
    elif kind == "strenum":
        prop["type"] = "string"
        prop["enum"] = list(f.values)
    else:
        raise ValueError(f"unknown kind {kind}")
    if f.default is not REQUIRED and f.default is not FACTORY and f.default is not None:
        prop["default"] = _clean(f.default) if isinstance(f.default, float) else f.default
    prop["description"] = _description(f, s.spec)
    return prop


def _definition(s: Struct) -> dict[str, Any]:
    d: dict[str, Any] = {
        "type": "object",
        "description": f"{s.doc} ({s.spec})",
        "properties": {f.name: _property(f, s) for f in s.fields},
    }
    if s.extra.get("variant"):
        d["minProperties"] = 1
        d["maxProperties"] = 1
    else:
        required = [f.name for f in s.fields if f.default is REQUIRED]
        if required:
            d["required"] = required
    d["additionalProperties"] = False
    return d


def build_schema() -> dict[str, Any]:
    """The complete schema as a dict."""
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": SCHEMA_ID,
        "title": "Plucked physical modelling engine instrument file",
        "description": (
            "One instrument as a versioned, human-readable document (SPEC-INST 'Instrument file'). "
            "Field names are the member names of the description structs of interfaces-v2 "
            "(SPEC-API 'Descriptions'); every numeric field records its SI unit in x-unit."
        ),
        "type": "object",
        "properties": {
            "format": {
                "type": "string",
                "enum": [FORMAT_NAME],
                "description": "File format marker (SPEC-INST Instrument file)",
            },
            "schemaVersion": {
                "type": "integer",
                "enum": [CURRENT_VERSION],
                "x-unit": "index",
                "description": "Schema version of this document; older versions are migrated on load (SPEC-INST Instrument file)",
            },
            "instrument": {
                "$ref": "#/$defs/InstrumentDesc",
                "description": "The instrument description (SPEC-API InstrumentDesc)",
            },
            "blobs": {
                "type": "array",
                "items": {"$ref": "#/$defs/BlobEntry"},
                "default": [],
                "description": "Blob manifest: binary data referenced by hash (SPEC-INST Instrument file)",
            },
        },
        "required": ["format", "schemaVersion", "instrument"],
        "additionalProperties": False,
        "$defs": {s.name: _definition(s) for s in STRUCTS},
    }


def schema_text() -> str:
    """The schema file text."""
    return json.dumps(build_schema(), indent=2, ensure_ascii=False) + "\n"


# --- Python models -------------------------------------------------------------------------------


def _py_type(f: Field) -> str:
    kind = f.kind
    if kind in ("num",):
        return "float"
    if kind == "int":
        return "int"
    if kind == "bool":
        return "bool"
    if kind in ("str", "strenum"):
        return (
            "str"
            if kind == "str"
            else "Literal[" + ", ".join(json.dumps(v) for v in f.values) + "]"
        )
    if kind == "enum":
        return f.ref
    if kind == "numarray":
        return "list[float]"
    if kind == "intarray":
        return "list[int]"
    if kind == "obj":
        return f.ref
    if kind == "objarray":
        return f"list[{f.ref}]"
    if kind == "optobj":
        return f"{f.ref} | None"
    raise ValueError(kind)


def _py_default(f: Field) -> str:
    if f.kind == "optobj":
        return " = None"
    if f.default is REQUIRED:
        return ""
    if f.default is FACTORY:
        return f" = field(default_factory={f.ref})"
    if f.kind == "optobj":
        return " = None"
    return f" = {f.default!r}"


def models_text() -> str:
    """The text of models.py."""
    lines = [
        "# SPDX-License-Identifier: MIT",
        "# Generated by pme_compiler.schema.generate from fields.py. Do not edit.",
        "# ruff: noqa",
        "# fmt: off",
        '"""Python models mirroring the interfaces-v2 description structs (B1-R4)."""',
        "",
        "from __future__ import annotations",
        "",
        "from dataclasses import dataclass, field",
        "from typing import Literal",
        "",
    ]
    for name, values in ENUMS.items():
        lines.append(f"{name} = Literal[" + ", ".join(json.dumps(v) for v in values) + "]")
    lines.append("")
    for s in STRUCTS:
        lines.append("")
        lines.append("@dataclass(frozen=True, kw_only=True)")
        lines.append(f"class {s.name}:")
        lines.append(f'    """{s.doc} ({s.spec})"""')
        lines.append("")
        if s.extra.get("variant"):
            alternatives = " | ".join(f.ref for f in s.fields)
            lines.append(f"    e: {alternatives}")
            continue
        for f in s.fields:
            lines.append(f"    {f.name}: {_py_type(f)}{_py_default(f)}")
    lines += [
        "",
        "",
        "@dataclass(frozen=True, kw_only=True)",
        "class InstrumentFile:",
        '    """A loaded instrument file: the description, its blob manifest and the source version."""',
        "",
        "    desc: InstrumentDesc",
        "    blobs: list[BlobEntry] = field(default_factory=list)",
        f"    source_version: int = {CURRENT_VERSION}",
        "",
    ]
    return "\n".join(lines)


# --- C++ -----------------------------------------------------------------------------------------


def _cpp_double(v: float) -> str:
    text = repr(float(v))
    return text


def _cpp_bound(f: Field) -> str:
    has_lo = f.lo is not None
    has_hi = f.hi is not None
    lo = _cpp_double(f.lo) if has_lo else "0.0"  # type: ignore[arg-type]
    hi = _cpp_double(f.hi) if has_hi else "0.0"  # type: ignore[arg-type]
    return (
        f"Bound{{{str(has_lo).lower()}, {lo}, {str(f.lo_ex).lower()}, "
        f"{str(has_hi).lower()}, {hi}, {str(f.hi_ex).lower()}}}"
    )


def _cpp_int_rule(f: Field) -> str:
    lo_f, hi_f = effective_bounds(f, cpp=True)
    has_lo = lo_f is not None
    has_hi = hi_f is not None
    lo = int(lo_f) if lo_f is not None else 0
    hi = int(hi_f) if hi_f is not None else 0
    one_of = list(f.one_of) + [0] * (2 - len(f.one_of))
    return (
        f"IntRule{{{str(has_lo).lower()}, {lo}, {str(has_hi).lower()}, {hi}, "
        f"{{{one_of[0]}, {one_of[1]}}}, {str(bool(f.one_of)).lower()}}}"
    )


def _read_call(f: Field) -> str:
    req = str(f.default is REQUIRED).lower()
    n = json.dumps(f.name)
    if f.kind == "num":
        return f"s.number({n}, o.{f.name}, {req}, {_cpp_bound(f)});"
    if f.kind == "int":
        return f"s.integer({n}, o.{f.name}, {req}, {_cpp_int_rule(f)});"
    if f.kind == "bool":
        return f"s.boolean({n}, o.{f.name}, {req});"
    if f.kind == "str":
        rule = "TextRule::HashOrEmpty" if f.pattern else "TextRule::Any"
        return f"s.text({n}, o.{f.name}, {req}, {rule});"
    if f.kind == "enum":
        return f"s.enumeration({n}, o.{f.name}, {req}, k{f.ref}Names);"
    if f.kind == "numarray":
        return f"s.numbers({n}, o.{f.name}, {req}, {_cpp_bound(f)}, {f.min_items});"
    if f.kind == "obj":
        return f"s.object({n}, o.{f.name}, {req});"
    if f.kind == "objarray":
        return f"s.objects({n}, o.{f.name}, {req}, {f.min_items});"
    if f.kind == "optobj":
        return f"s.optional({n}, o.{f.name});"
    raise ValueError(f.kind)


def _write_stmt(f: Field) -> list[str]:
    n = json.dumps(f.name)
    if f.kind in ("num", "int", "bool", "str"):
        return [f"j[{n}] = o.{f.name};"]
    if f.kind == "enum":
        return [f"j[{n}] = std::string(name(o.{f.name}));"]
    if f.kind == "numarray":
        return [
            f"j[{n}] = OJson::array();",
            f"for (const double v : o.{f.name}) {{ j[{n}].push_back(v); }}",
        ]
    if f.kind == "obj":
        return [f"write(o.{f.name}, j[{n}]);"]
    if f.kind == "objarray":
        return [
            f"j[{n}] = OJson::array();",
            f"for (const auto& e : o.{f.name}) {{ write(e, j[{n}].emplace_back()); }}",
        ]
    if f.kind == "optobj":
        return [f"if (o.{f.name}.has_value()) {{ write(*o.{f.name}, j[{n}]); }}"]
    raise ValueError(f.kind)


def cpp_text() -> str:
    """The text of fields_generated.hpp."""
    cpp = [s for s in STRUCTS if s.cpp]
    out: list[str] = [
        "// SPDX-License-Identifier: MIT",
        "// Generated by compiler/pme_compiler/schema/generate.py from fields.py. Do not edit.",
        "// Included by instrument_file.cpp inside namespace pme::loader::detail (B1-R1, B1-R4).",
        "#pragma once",
        "",
        "// clang-format off",
        "",
    ]
    for name, values in ENUMS.items():
        names = ", ".join(json.dumps(v) for v in values)
        out.append(
            f"inline constexpr std::array<const char*, {len(values)}> k{name}Names{{{names}}};"
        )
        out.append(
            f"[[nodiscard]] inline const char* name({name} v) {{ return k{name}Names[static_cast<std::size_t>(v)]; }}"
        )
    out.append("")
    out.append("// Member keys of the exciter alternatives.")
    for f in next(s for s in cpp if s.extra.get("variant")).fields:
        out.append(
            f'[[nodiscard]] inline const char* keyOf(const {f.ref}& /*v*/) {{ return "{f.name}"; }}'
        )
    out.append("")
    for s in cpp:
        out.append(f"void read(const Reader& r, {s.name}& o);")
    for s in cpp:
        out.append(f"void write(const {s.name}& o, OJson& j);")
    out.append("")
    for s in cpp:
        out.append(f"// {s.name}: {s.doc} ({s.spec})")
        out.append(f"void read(const Reader& r, {s.name}& o) {{")
        out.append("    ObjectScope s(r);")
        out.append("    if (!s.ok()) {")
        out.append("        return;")
        out.append("    }")
        if s.extra.get("variant"):
            out.append("    if (s.size() != 1) {")
            out.append('        r.error(Code::Structure, "expected exactly one exciter key");')
            out.append("    }")
            for f in s.fields:
                out.append(f'    if (s.has("{f.name}")) {{')
                out.append(f"        {f.ref} alt;")
                out.append(f'        s.object("{f.name}", alt, false);')
                out.append("        o.e = alt;")
                out.append("    }")
        else:
            for f in s.fields:
                out.append("    " + _read_call(f))
        out.append("    s.finish();")
        out.append("}")
        out.append("")
        out.append(f"void write(const {s.name}& o, OJson& j) {{")
        out.append("    j = OJson::object();")
        if s.extra.get("variant"):
            out.append("    std::visit([&j](const auto& v) { write(v, j[keyOf(v)]); }, o.e);")
        else:
            for f in s.fields:
                for line in _write_stmt(f):
                    out.append("    " + line)
        out.append("}")
        out.append("")
    out.append("// clang-format on")
    out.append("")
    return "\n".join(out)


def outputs() -> dict[Path, str]:
    """Every generated file and its text."""
    return {SCHEMA_PATH: schema_text(), MODELS_PATH: models_text(), CPP_PATH: cpp_text()}


def main(argv: list[str] | None = None) -> int:
    """Write the generated files, or with --check verify they are current."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if a committed file is stale")
    args = parser.parse_args(argv)
    stale = []
    for path, text in outputs().items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(path)
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path.relative_to(REPO_ROOT)}")
    if stale:
        for path in stale:
            print(
                f"stale: {path.relative_to(REPO_ROOT)} (run python -m pme_compiler.schema.generate)",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
