# SPDX-License-Identifier: MIT
"""P0.3-R1 (ctest timeout): pme_add_doctest gives every discovered test a TIMEOUT (default 300 s,
overridable per target); the rtcheck death tests keep their 120 s limit."""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "cmake" / "pme_testing.cmake"
RTCHECK = ROOT / "engine" / "tests" / "rtcheck" / "CMakeLists.txt"

needs_cmake = pytest.mark.skipif(shutil.which("cmake") is None, reason="cmake not found")


def test_p0_3_r1_helper_declares_a_300_second_default_timeout() -> None:
    """P0.3-R1: pme_add_doctest accepts TIMEOUT and defaults it to 300 seconds."""
    text = MODULE.read_text(encoding="utf-8")
    assert '"RT" "TIMEOUT" "SOURCES;LIBRARIES;LABELS"' in text
    assert "set(ARG_TIMEOUT 300)" in text
    assert '-D "TEST_TIMEOUT=${ARG_TIMEOUT}"' in text
    assert "TIMEOUT ${TEST_TIMEOUT}" in text


def test_p0_3_r1_rtcheck_death_tests_keep_their_120_second_limit() -> None:
    """P0.3-R1: both rtcheck executables pass TIMEOUT 120."""
    text = RTCHECK.read_text(encoding="utf-8")
    assert text.count("pme_add_doctest(") == 2
    assert text.count("TIMEOUT 120") == 2


@needs_cmake
@pytest.mark.skipif(sys.platform == "win32", reason="the fake test executable is a shell script")
def test_p0_3_r1_discovery_writes_the_timeout(tmp_path: Path) -> None:
    """P0.3-R1: the discovery script writes TIMEOUT into set_tests_properties of every case."""
    fake = tmp_path / "fake_tests.sh"
    fake.write_text(
        "#!/bin/sh\n"
        'printf "%s\\n" "[doctest] listing" "P0.3-R1 first case" "P0.3-R1 second case"\n',
        encoding="utf-8",
    )
    fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
    out = tmp_path / "tests.cmake"
    subprocess.run(
        [
            "cmake",
            "-D",
            "PME_DOCTEST_DISCOVER=ON",
            "-D",
            "TEST_TARGET=fake",
            "-D",
            f"TEST_EXECUTABLE={fake}",
            "-D",
            f"TEST_WORKING_DIR={tmp_path}",
            "-D",
            "TEST_LABELS=unit",
            "-D",
            "TEST_TIMEOUT=120",
            "-D",
            f"CTEST_FILE={out}",
            "-P",
            str(MODULE),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    script = out.read_text(encoding="utf-8")
    assert script.count("add_test(") == 2
    assert script.count("TIMEOUT 120") == 2


@needs_cmake
def test_p0_3_r1_ctest_lists_the_timeouts_of_a_built_tree() -> None:
    """P0.3-R1: `ctest -N --show-only=json-v1` lists a TIMEOUT for every doctest case.

    Runs against the build tree named by PME_BUILD_DIR (default build/linux-debug); skipped when
    that tree has not been built.
    """
    build = Path(os.environ.get("PME_BUILD_DIR", ROOT / "build" / "linux-debug"))
    if not (build / "CTestTestfile.cmake").is_file():
        pytest.skip(f"no build tree at {build}")
    done = subprocess.run(
        ["ctest", "-N", "--show-only=json-v1"],
        cwd=build,
        check=True,
        capture_output=True,
        text=True,
    )
    tests = json.loads(done.stdout)["tests"]
    checked = 0
    for test in tests:
        props = {p["name"]: p["value"] for p in test.get("properties", [])}
        exe = Path(test["command"][0]).name if test.get("command") else ""
        if not exe.endswith("_tests") and "_tests." not in exe:
            continue  # only pme_add_doctest executables; benchmarks and CLI tests set their own
        checked += 1
        assert "TIMEOUT" in props, f"{test['name']} has no TIMEOUT"
        expected = 120.0 if exe.startswith("pme_rtcheck") else 300.0
        assert props["TIMEOUT"] == expected, f"{test['name']}: TIMEOUT {props['TIMEOUT']}"
    assert checked > 0, "no doctest cases listed"
