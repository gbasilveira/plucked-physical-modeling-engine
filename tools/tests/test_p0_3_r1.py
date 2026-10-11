# SPDX-License-Identifier: MIT
"""P0.3-R1: every test name carries a requirement, test or oracle ID."""

from __future__ import annotations

import ast
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ID = r"((P0\.[1-5]|[ABCPDEVH][0-9]+)-[RT][0-9]+|ORC-[0-9]{2})"
CPP_NAME = re.compile(rf"^{ID} \S")
CPP_FORBIDDEN = re.compile(r"[,*?\\\"]")
PY_NAME = re.compile(r"^test_((p0_[1-5]|[abcpdevh][0-9]+)_[rt][0-9]+|orc_[0-9]{2})_")
PY_DOC_ID = re.compile(rf"^{ID}\b")
TEST_CASE = re.compile(r'\bTEST_CASE\s*\(\s*"((?:[^"\\]|\\.)*)"')

CPP_SUFFIXES = {".cpp", ".cc", ".cxx", ".hpp", ".h", ".inl"}
CPP_ROOTS = ("engine/", "tools/", "reference/")
PY_ROOTS = ("compiler/tests/", "tools/tests/")


def candidate_files() -> list[str]:
    """Tracked files plus untracked files that are not ignored (so new tests are checked)."""
    out = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return sorted(set(line for line in out.splitlines() if line))


def check_cpp_text(text: str) -> list[str]:
    """Return a message for each TEST_CASE name in ``text`` that breaks the convention."""
    problems = []
    for match in TEST_CASE.finditer(text):
        name = match.group(1)
        if not CPP_NAME.match(name):
            problems.append(f'TEST_CASE("{name}") does not begin with an ID and a space')
        elif CPP_FORBIDDEN.search(name):
            problems.append(
                f'TEST_CASE("{name}") contains one of , * ? \\ " (breaks ctest filters)'
            )
    return problems


def check_python_text(text: str) -> list[str]:
    """Return a message for each test function in ``text`` that breaks the convention."""
    problems = []
    tree = ast.parse(text)
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name.startswith(
            "test"
        ):
            if not PY_NAME.match(node.name):
                problems.append(f"{node.name}: name must be test_<id>_<what> (id lower-cased)")
                continue
            doc = ast.get_docstring(node)
            if doc is None or not PY_DOC_ID.match(doc):
                problems.append(f"{node.name}: docstring must begin with the ID")
    return problems


def scan_repository() -> list[str]:
    problems = []
    for rel in candidate_files():
        path = ROOT / rel
        if not path.is_file():
            continue
        if rel.startswith(CPP_ROOTS) and path.suffix in CPP_SUFFIXES:
            problems += [f"{rel}: {m}" for m in check_cpp_text(path.read_text(encoding="utf-8"))]
        elif rel.startswith(PY_ROOTS) and path.suffix == ".py":
            problems += [f"{rel}: {m}" for m in check_python_text(path.read_text(encoding="utf-8"))]
    return problems


def test_p0_3_r1_test_names_carry_ids() -> None:
    """P0.3-R1: every C++ TEST_CASE and Python test function in the repository is named by ID."""
    problems = scan_repository()
    assert not problems, "test names break the convention:\n" + "\n".join(problems)


def test_p0_3_r1_checker_flags_bad_names() -> None:
    """P0.3-R1: the name checker rejects names without an ID and accepts well-formed ones."""
    assert check_cpp_text('TEST_CASE("P0.2-T1 allocation aborts") {}') == []
    assert check_cpp_text('TEST_CASE("ORC-01 oscillator accuracy") {}') == []
    assert check_cpp_text('TEST_CASE("A2-R3 wound string") {}') == []
    assert check_cpp_text('TEST_CASE("wound string mass") {}')
    assert check_cpp_text('TEST_CASE("P0.9-T1 unknown phase") {}')
    assert check_cpp_text('TEST_CASE("A2-T1no space") {}')
    assert check_cpp_text('TEST_CASE("A2-T1 comma, in name") {}')
    good = 'def test_p0_2_t2_x() -> None:\n    """P0.2-T2: x."""\n'
    assert check_python_text(good) == []
    assert check_python_text("def test_misformatted() -> None:\n    pass\n")
    assert check_python_text("def test_p0_2_t2_x() -> None:\n    pass\n")
    assert check_python_text('def test_p0_2_t2_x() -> None:\n    """No id."""\n')
