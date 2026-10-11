# SPDX-License-Identifier: MIT
"""Test results: JUnit XML from ctest and pytest, and GitHub job conclusions (V1-R1).

A JUnit file lives in a directory named after its workflow artifact (``junit-build-linux-gcc/``);
the artifact name gives the job label and, through the system names it contains, the system the
tests ran on.
"""

from __future__ import annotations

import json
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

from .ids import detect_system, name_id, python_name_to_id


class ResultsError(Exception):
    """A results file cannot be read."""


@dataclass(frozen=True)
class TestResult:
    """One executed (or skipped) test case of one JUnit file."""

    __test__ = False  # not a pytest class

    name: str  # ctest test name, or pytest function name without parameters
    outcome: str  # 'pass' | 'fail' | 'skipped'
    job: str  # artifact name, or the file stem when the file sits at the top
    system: str | None
    kind: str  # 'c' (ctest) | 'p' (pytest)
    labels: tuple[str, ...]
    file: str

    @property
    def key(self) -> tuple[str, str]:
        """Join key with a source test."""
        return (self.kind, self.name)

    def own_id(self) -> str | None:
        """The ID the name itself carries."""
        return name_id(self.name) if self.kind == "c" else python_name_to_id(self.name)


_PARAM_RE = re.compile(r"\[.*\]$")


def _outcome(case: ET.Element) -> str:
    if case.find("failure") is not None or case.find("error") is not None:
        return "fail"
    if case.find("skipped") is not None:
        return "skipped"
    status = case.get("status", "")
    if status == "fail":
        return "fail"
    if status == "notrun":
        return "skipped"
    return "pass"


def _labels(case: ET.Element) -> tuple[str, ...]:
    for prop in case.iter("property"):
        if prop.get("name") == "cmake_labels":
            return tuple(label for label in (prop.get("value") or "").split(";") if label)
    return ()


def read_junit_file(path: Path, job: str, system: str | None, rel: str) -> list[TestResult]:
    """Read the test cases of one JUnit file (ctest ``--output-junit`` or pytest ``--junitxml``)."""
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as exc:
        raise ResultsError(f"{rel}: cannot read JUnit XML: {exc}") from exc
    results: list[TestResult] = []
    for case in root.iter("testcase"):
        name = case.get("name", "")
        classname = case.get("classname", "")
        # ctest repeats the test name as the class name; pytest gives the module path.
        kind = "c" if (not classname or classname == name) else "p"
        if kind == "p":
            name = _PARAM_RE.sub("", name)
        results.append(TestResult(name, _outcome(case), job, system, kind, _labels(case), rel))
    return results


def read_junit_dir(directory: Path, default_system: str | None) -> list[TestResult]:
    """Read every ``*.xml`` file below ``directory``; the first sub-directory names the job."""
    results: list[TestResult] = []
    if not directory.is_dir():
        raise ResultsError(f"{directory}: not a directory")
    for path in sorted(directory.rglob("*.xml")):
        rel_path = path.relative_to(directory)
        rel = rel_path.as_posix()
        job = rel_path.parts[0] if len(rel_path.parts) > 1 else path.stem
        system = detect_system(job, None) or detect_system(path.stem, default_system)
        results.extend(read_junit_file(path, job, system, rel))
    return results


def read_job_results(path: Path) -> dict[str, str]:
    """Read the ``toJSON(needs)`` document of a workflow: {job id: conclusion}.

    Accepts the GitHub shape ``{"build": {"result": "success", ...}}`` and the flat shape
    ``{"build": "success"}``.
    """
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ResultsError(f"{path}: cannot read job results: {exc}") from exc
    if not isinstance(document, dict):
        raise ResultsError(f"{path}: job results must be a JSON object")
    jobs: dict[str, str] = {}
    for job, value in document.items():
        result = value.get("result") if isinstance(value, dict) else value
        if not isinstance(result, str):
            raise ResultsError(f"{path}: job {job!r} has no 'result' string")
        jobs[str(job)] = result
    return jobs
