# SPDX-License-Identifier: MIT
"""P0.3-R5 (Python): WAV round trips for every supported format, WAVE_FORMAT_EXTENSIBLE on read,
errors for malformed and unsupported files, and byte-level agreement with the C++ writer's layout."""

from __future__ import annotations

import math
import struct
from pathlib import Path

import numpy as np
import pytest
from pme_analysis import WavData, WavError, encode_wav, parse_wav, read_wav, write_wav
from pme_analysis.wav import WavFormat

FORMATS: list[WavFormat] = ["pcm16", "pcm24", "pcm32", "float32", "float64"]
STEP = {"pcm16": 1 / 32768, "pcm24": 1 / 8388608, "pcm32": 1 / 2147483648, "float32": 0.0, "float64": 0.0}


def make_signal(fmt: WavFormat, channels: int) -> WavData:
    frames = np.arange(200)[:, None]
    ch = np.arange(channels)[None, :]
    samples = (0.8 * np.sin(0.05 * frames * (ch + 1))).reshape(-1)
    samples[0] = -1.0
    if fmt == "float32":
        samples = samples.astype(np.float32).astype(np.float64)
    return WavData(44100.0, channels, fmt, samples)


@pytest.mark.parametrize("fmt", FORMATS)
@pytest.mark.parametrize("channels", [1, 2, 6])
def test_p0_3_r5_round_trip_every_format(fmt: WavFormat, channels: int) -> None:
    """P0.3-R5: write then read returns the same format, rate, channels and samples."""
    wav = make_signal(fmt, channels)
    out = parse_wav(encode_wav(wav))
    assert (out.format, out.channels, out.sample_rate_hz) == (fmt, channels, 44100.0)
    assert out.samples.shape == wav.samples.shape
    assert np.max(np.abs(out.samples - wav.samples)) <= 0.5 * STEP[fmt]
    assert out.frames == 200


def test_p0_3_r5_integer_formats_clamp() -> None:
    """P0.3-R5: out-of-range samples clamp and +1.0 stays below plus one."""
    wav = WavData(48000.0, 1, "pcm16", np.array([1.5, -1.5, 1.0]))
    out = parse_wav(encode_wav(wav))
    assert out.samples[0] == pytest.approx(32767 / 32768)
    assert out.samples[1] == -1.0
    assert out.samples[2] < 1.0


def test_p0_3_r5_file_round_trip(tmp_path: Path) -> None:
    """P0.3-R5: write_wav and read_wav round trip through a file; a missing file raises WavError."""
    wav = make_signal("float64", 2)
    path = tmp_path / "x.wav"
    write_wav(path, wav)
    assert np.array_equal(read_wav(path).samples, wav.samples)
    with pytest.raises(WavError):
        read_wav(tmp_path / "missing.wav")


def test_p0_3_r5_reads_extensible_and_skips_unknown_chunks() -> None:
    """P0.3-R5: WAVE_FORMAT_EXTENSIBLE 24-bit stereo with a padded LIST chunk in front decodes."""
    fmt = struct.pack("<HHIIHHHHI", 0xFFFE, 2, 48000, 48000 * 6, 6, 24, 22, 24, 3) + struct.pack(
        "<H14x", 1
    )
    values = [0x400000, -0x200000, -0x800000, 0]
    payload = b"".join(struct.pack("<i", v)[:3] for v in values)
    body = (
        b"WAVE"
        + b"LIST" + struct.pack("<I", 3) + b"abc\x00"
        + b"fmt " + struct.pack("<I", len(fmt)) + fmt
        + b"data" + struct.pack("<I", len(payload)) + payload
    )  # fmt: skip
    wav = parse_wav(b"RIFF" + struct.pack("<I", len(body)) + body)
    assert (wav.format, wav.channels, wav.sample_rate_hz) == ("pcm24", 2, 48000.0)
    assert list(wav.samples) == [0.5, -0.25, -1.0, 0.0]


def test_p0_3_r5_rejects_malformed_and_unsupported_files() -> None:
    """P0.3-R5: malformed and unsupported files raise WavError with a clear message."""
    good = encode_wav(make_signal("pcm16", 1))
    with pytest.raises(WavError, match="shorter"):
        parse_wav(b"RIFF")
    with pytest.raises(WavError, match="not a RIFF/WAVE"):
        parse_wav(good[:8] + b"XXXX" + good[12:])
    with pytest.raises(WavError, match="RF64"):
        parse_wav(b"RF64" + good[4:])
    adpcm = bytearray(good)
    adpcm[20] = 2
    with pytest.raises(WavError, match="unsupported format"):
        parse_wav(bytes(adpcm))
    with pytest.raises(WavError, match="truncated"):
        parse_wav(good[:-10])
    with pytest.raises(WavError, match="integer"):
        encode_wav(WavData(44100.5, 1, "pcm16", np.zeros(4)))
    with pytest.raises(WavError, match="multiple"):
        encode_wav(WavData(44100.0, 2, "pcm16", np.zeros(5)))


def test_p0_3_r5_header_layout_is_canonical() -> None:
    """P0.3-R5: the writer emits the canonical 44-byte header, little-endian on every system."""
    data = encode_wav(WavData(48000.0, 2, "pcm16", np.array([0.0, 0.5, -0.5, 0.25])))
    assert data[:4] == b"RIFF" and data[8:16] == b"WAVEfmt "
    assert struct.unpack_from("<IHHIIHH", data, 16) == (16, 1, 2, 48000, 192000, 4, 16)
    assert data[36:40] == b"data" and struct.unpack_from("<I", data, 40)[0] == 8
    assert struct.unpack_from("<I", data, 4)[0] == len(data) - 8
    assert struct.unpack_from("<4h", data, 44) == (0, 16384, -16384, 8192)
    assert math.isclose(parse_wav(data).samples[1], 0.5)
