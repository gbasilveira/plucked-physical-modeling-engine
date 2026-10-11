# SPDX-License-Identifier: MIT
"""P0.3-R5 (cross-language): a WAV written by the C++ library reads identically in Python and
vice versa, for 16-, 24- and 32-bit integer and 32- and 64-bit float, with 1 and 3 channels.

The C++ side is the tool ``pme_wav_roundtrip`` (tools/analysis/wav_roundtrip_main.cpp), found
through the environment variable ``PME_WAV_TOOL``; the tests are skipped without it. The signal
is built from dyadic rationals k/1024, which every format represents exactly, so comparisons are
exact.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

import numpy as np
import pytest
from numpy.typing import NDArray
from pme_analysis import WavData, read_wav, write_wav
from pme_analysis.wav import WavFormat

TOOL = os.environ.get("PME_WAV_TOOL", "")
FRAMES = 480
RATE = 48000.0
FORMATS: list[WavFormat] = ["pcm16", "pcm24", "pcm32", "float32", "float64"]
BITS = {"pcm16": 16, "pcm24": 24, "pcm32": 32}

needs_tool = pytest.mark.skipif(not TOOL, reason="PME_WAV_TOOL is not set (C++ tool path)")


def expected_signal(channels: int) -> NDArray[np.float64]:
    """The interleaved signal of wav_roundtrip_main.cpp (test_sample)."""
    i = np.arange(FRAMES * channels, dtype=np.int64)
    samples = ((37 * i + 11) % 2001 - 1000).astype(np.float64) / 1024.0
    samples[0] = -1.0
    return samples


def run_tool(*args: str) -> str:
    done = subprocess.run([TOOL, *args], check=True, capture_output=True, text=True)
    return done.stdout


def assert_exact(fmt: WavFormat, got: NDArray[np.float64], want: NDArray[np.float64]) -> None:
    """Exact equality; for integer formats also the integer codes."""
    assert got.shape == want.shape
    if fmt in BITS:
        scale = float(2 ** (BITS[fmt] - 1))
        codes_got = got * scale
        codes_want = want * scale
        assert np.array_equal(codes_got, np.rint(codes_got)), "reader returned a non-integer code"
        assert np.array_equal(codes_got.astype(np.int64), codes_want.astype(np.int64))
    assert np.array_equal(got, want)


@needs_tool
@pytest.mark.parametrize("channels", [1, 3])
@pytest.mark.parametrize("fmt", FORMATS)
def test_p0_3_r5_cpp_writes_python_reads(fmt: WavFormat, channels: int, tmp_path: Path) -> None:
    """P0.3-R5: a WAV written by the C++ library reads sample-exactly in Python."""
    path = tmp_path / "cpp.wav"
    run_tool("write", str(path), fmt, str(channels))
    wav = read_wav(path)
    assert wav.format == fmt
    assert wav.channels == channels
    assert wav.sample_rate_hz == RATE
    assert wav.frames == FRAMES
    assert_exact(fmt, wav.samples, expected_signal(channels))


@needs_tool
@pytest.mark.parametrize("channels", [1, 3])
@pytest.mark.parametrize("fmt", FORMATS)
def test_p0_3_r5_python_writes_cpp_reads(fmt: WavFormat, channels: int, tmp_path: Path) -> None:
    """P0.3-R5: a WAV written by Python reads sample-exactly in the C++ library."""
    path = tmp_path / "py.wav"
    write_wav(path, WavData(RATE, channels, fmt, expected_signal(channels)))
    header, *lines = run_tool("read", str(path)).splitlines()
    name, chan, rate, frames = header.split()
    assert name == fmt
    assert int(chan) == channels
    assert float(rate) == RATE
    assert int(frames) == FRAMES
    got = np.array([float(line) for line in lines], dtype=np.float64)
    assert_exact(fmt, got, expected_signal(channels))


@needs_tool
@pytest.mark.parametrize("channels", [1, 3])
@pytest.mark.parametrize("fmt", FORMATS)
def test_p0_3_r5_both_writers_produce_the_same_bytes(
    fmt: WavFormat, channels: int, tmp_path: Path
) -> None:
    """P0.3-R5: both writers emit the same canonical bytes for the same signal."""
    cpp = tmp_path / "cpp.wav"
    py = tmp_path / "py.wav"
    run_tool("write", str(cpp), fmt, str(channels))
    write_wav(py, WavData(RATE, channels, fmt, expected_signal(channels)))
    assert cpp.read_bytes() == py.read_bytes()
