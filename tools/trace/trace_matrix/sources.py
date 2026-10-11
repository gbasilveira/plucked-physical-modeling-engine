# SPDX-License-Identifier: MIT
"""Extraction of test names and ``Covers:`` annotations from the code (V1-R1).

Sources scanned under the given directories: C++ ``TEST_CASE("...")`` (doctest), CMake
``add_test(NAME "...")`` and Python ``def test_...`` (with docstring); GitHub workflow steps and
jobs annotated ``# Covers: <ID>[, <ID>...]``. Directories named ``build``, ``testdata`` and the like
are skipped (relative to the scanned root).
"""

from __future__ import annotations

import ast
import re
from dataclasses import dataclass, field
from pathlib import Path

from .ids import DOC_ID_RE, ID_RE, name_id, python_name_to_id

SKIP_DIRS = frozenset(
    {
        "build",
        "testdata",
        "node_modules",
        "__pycache__",
        ".git",
        ".venv",
        "venv",
        ".cpm-cache",
        "_deps",
        ".mypy_cache",
        ".ruff_cache",
        ".pytest_cache",
    }
)
CPP_SUFFIXES = frozenset({".cpp", ".cc", ".cxx", ".hpp", ".h", ".inl"})

_TEST_CASE_RE = re.compile(r'\bTEST_CASE\s*\(\s*"((?:[^"\\]|\\.)*)"')
_ADD_TEST_RE = re.compile(r'\badd_test\s*\(\s*NAME\s+"([^"]+)"', re.IGNORECASE)
_COVERS_RE = re.compile(r"\bCovers:\s*(.*)$")


@dataclass
class SourceTest:
    """A test found in the sources."""

    kind: str  # 'doctest' | 'cmake' | 'pytest'
    name: str  # TEST_CASE name, ctest name or function name
    file: str  # POSIX path relative to the scanned root
    line: int
    ids: list[str] = field(default_factory=list)  # name ID, docstring ID, then Covers IDs
    covers: list[str] = field(default_factory=list)

    @property
    def key(self) -> tuple[str, str]:
        """Join key with a JUnit result: ctest names are exact, pytest uses the function name."""
        return ("p" if self.kind == "pytest" else "c", self.name)


@dataclass(frozen=True)
class StepAnnotation:
    """A ``# Covers:`` annotation on a workflow step or job."""

    workflow: str  # file name, for example 'pr.yml'
    job: str  # job key
    step: str  # step name, or '(job)' when the annotation sits above a job
    ids: tuple[str, ...]
    line: int


def parse_covers(comment_text: str) -> list[str]:
    """Return the IDs named after 'Covers:' in one comment line (empty when there is none)."""
    match = _COVERS_RE.search(comment_text)
    if match is None:
        return []
    return ID_RE.findall(match.group(1))


def _covers_above(
    lines: list[str], index: int, markers: tuple[str, ...], skip: str = ""
) -> list[str]:
    """IDs of the contiguous comment block above line ``index`` (0-based).

    ``skip`` is an optional line prefix (Python decorators) passed over without ending the block.
    """
    found: list[str] = []
    i = index - 1
    while i >= 0:
        text = lines[i].strip()
        if text.startswith(markers):
            for item in parse_covers(text):
                if item not in found:
                    found.append(item)
        elif not (skip and text.startswith(skip)):
            break
        i -= 1
    return found


def _add_unique(target: list[str], items: list[str]) -> None:
    for item in items:
        if item not in target:
            target.append(item)


def scan_cpp(text: str, rel: str) -> list[SourceTest]:
    """Find doctest ``TEST_CASE`` names in one C++ file."""
    lines = text.splitlines()
    tests: list[SourceTest] = []
    for match in _TEST_CASE_RE.finditer(text):
        line_no = text.count("\n", 0, match.start())
        name = match.group(1)
        covers = _covers_above(lines, line_no, ("//",))
        ids: list[str] = []
        found = name_id(name)
        if found:
            ids.append(found)
        _add_unique(ids, covers)
        tests.append(SourceTest("doctest", name, rel, line_no + 1, ids, covers))
    return tests


def scan_cmake(text: str, rel: str) -> list[SourceTest]:
    """Find ``add_test(NAME "...")`` registrations in one CMake file."""
    lines = text.splitlines()
    tests: list[SourceTest] = []
    for match in _ADD_TEST_RE.finditer(text):
        line_no = text.count("\n", 0, match.start())
        name = match.group(1)
        covers = _covers_above(lines, line_no, ("#",))
        ids: list[str] = []
        found = name_id(name)
        if found:
            ids.append(found)
        _add_unique(ids, covers)
        tests.append(SourceTest("cmake", name, rel, line_no + 1, ids, covers))
    return tests


def scan_python(text: str, rel: str) -> list[SourceTest]:
    """Find ``def test_...`` functions in one Python file; the docstring ID is read too."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return []
    lines = text.splitlines()
    tests: list[SourceTest] = []
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        if not node.name.startswith("test"):
            continue
        first = min([d.lineno for d in node.decorator_list] + [node.lineno])
        covers = _covers_above(lines, first - 1, ("#",))
        ids: list[str] = []
        by_name = python_name_to_id(node.name)
        if by_name:
            ids.append(by_name)
        doc = ast.get_docstring(node)
        if doc:
            doc_match = DOC_ID_RE.match(doc)
            if doc_match:
                _add_unique(ids, [doc_match.group(1)])
        _add_unique(ids, covers)
        tests.append(SourceTest("pytest", node.name, rel, node.lineno, ids, covers))
    tests.sort(key=lambda t: (t.file, t.line))
    return tests


def _iter_files(root: Path, directories: list[str]) -> list[Path]:
    files: list[Path] = []
    for directory in directories:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file():
                continue
            parts = path.relative_to(root).parts
            if any(part in SKIP_DIRS for part in parts[:-1]):
                continue
            files.append(path)
    return files


def scan_sources(root: Path, directories: list[str]) -> list[SourceTest]:
    """Scan ``directories`` (relative to ``root``) for tests."""
    tests: list[SourceTest] = []
    for path in _iter_files(root, directories):
        rel = path.relative_to(root).as_posix()
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        if path.suffix in CPP_SUFFIXES:
            tests.extend(scan_cpp(text, rel))
        elif path.suffix == ".py" and "/tests/" in f"/{rel}":
            tests.extend(scan_python(text, rel))
        elif path.name == "CMakeLists.txt" or path.suffix == ".cmake":
            tests.extend(scan_cmake(text, rel))
    return tests


# ---- Workflows -------------------------------------------------------------------------------

_JOBS_RE = re.compile(r"^jobs:\s*$")
_JOB_KEY_RE = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$")
_STEP_RE = re.compile(r"^\s*-\s+(name|uses|run):\s*(.*?)\s*$")


def scan_workflow(text: str, workflow: str) -> list[StepAnnotation]:
    """Find ``# Covers:`` comments above steps (``- name:``) and jobs of one workflow file."""
    annotations: list[StepAnnotation] = []
    in_jobs = False
    job = ""
    pending: list[str] = []
    pending_line = 0
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if _JOBS_RE.match(line):
            in_jobs = True
            continue
        if not in_jobs:
            continue
        if stripped.startswith("#"):
            covers = parse_covers(stripped)
            if covers:
                if not pending:
                    pending_line = number
                _add_unique(pending, covers)
            continue
        if not stripped:
            continue
        job_match = _JOB_KEY_RE.match(line)
        if job_match:
            job = job_match.group(1)
            if pending:
                annotations.append(
                    StepAnnotation(workflow, job, "(job)", tuple(pending), pending_line)
                )
                pending = []
            continue
        step = _STEP_RE.match(line)
        if step and pending:
            label = step.group(2).strip("\"'") or step.group(1)
            annotations.append(StepAnnotation(workflow, job, label, tuple(pending), pending_line))
            pending = []
        elif pending:
            # A comment block that does not sit above a step or a job applies to nothing.
            pending = []
    return annotations


def scan_workflows(root: Path, workflow_dir: str = ".github/workflows") -> list[StepAnnotation]:
    """Scan every ``*.yml`` file of the workflow directory."""
    base = root / workflow_dir
    annotations: list[StepAnnotation] = []
    if not base.is_dir():
        return annotations
    for path in sorted(base.glob("*.yml")):
        annotations.extend(scan_workflow(path.read_text(encoding="utf-8"), path.name))
    return annotations
