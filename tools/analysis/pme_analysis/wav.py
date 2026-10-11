# SPDX-License-Identifier: MIT
"""RIFF/WAVE reading and writing for tests and tools (P0.3-R5). Never imported by the engine.

Reads PCM 16/24/32-bit integer and IEEE float 32/64-bit, any channel count, including
``WAVE_FORMAT_EXTENSIBLE``; samples are float64, integer formats scaled to [-1, 1). Little-endian on
every system. Mirror of ``pme/analysis/wav.hpp``.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

import numpy as np
from numpy.typing import NDArray

WavFormat = Literal["pcm16", "pcm24", "pcm32", "float32", "float64"]

_TAG_PCM = 1
_TAG_FLOAT = 3
_TAG_EXTENSIBLE = 0xFFFE
_BITS: dict[str, int] = {"pcm16": 16, "pcm24": 24, "pcm32": 32, "float32": 32, "float64": 64}


class WavError(ValueError):
    """Malformed or unsupported WAV data."""


@dataclass
class WavData:
    sample_rate_hz: float = 48000.0
    channels: int = 1
    format: WavFormat = "pcm16"
    samples: NDArray[np.float64] = field(default_factory=lambda: np.zeros(0))  # interleaved

    @property
    def frames(self) -> int:
        return 0 if self.channels == 0 else self.samples.size // self.channels


def parse_wav(data: bytes) -> WavData:
    """Decode a WAV file held in memory; raises :class:`WavError`."""
    if len(data) < 12:
        raise WavError("WAV: file shorter than the RIFF header")
    if data[:4] in (b"RF64", b"BW64"):
        raise WavError("WAV: RF64/BW64 files are not supported")
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise WavError("WAV: not a RIFF/WAVE file")
    fmt: tuple[int, int, int, int, int] | None = None  # tag, channels, rate, block_align, bits
    audio: bytes | None = None
    pos = 12
    while pos + 8 <= len(data):
        chunk_id = data[pos : pos + 4]
        (size,) = struct.unpack_from("<I", data, pos + 4)
        body = pos + 8
        if body + size > len(data):
            if chunk_id in (b"data", b"fmt "):
                raise WavError(f"WAV: chunk '{chunk_id.decode('latin-1')}' is truncated")
            break
        if chunk_id == b"fmt ":
            if size < 16:
                raise WavError("WAV: fmt chunk shorter than 16 bytes")
            tag, channels, rate, _, block_align, bits = struct.unpack_from("<HHIIHH", data, body)
            if tag == _TAG_EXTENSIBLE:
                if size < 40:
                    raise WavError("WAV: WAVE_FORMAT_EXTENSIBLE fmt chunk shorter than 40 bytes")
                (tag,) = struct.unpack_from("<H", data, body + 24)
            fmt = (tag, channels, rate, block_align, bits)
        elif chunk_id == b"data":
            audio = data[body : body + size]
        pos = body + size + (size & 1)
    if fmt is None:
        raise WavError("WAV: missing fmt chunk")
    if audio is None:
        raise WavError("WAV: missing data chunk")
    tag, channels, rate, block_align, bits = fmt
    if channels == 0:
        raise WavError("WAV: zero channels")
    formats: dict[tuple[int, int], WavFormat] = {
        (_TAG_PCM, 16): "pcm16",
        (_TAG_PCM, 24): "pcm24",
        (_TAG_PCM, 32): "pcm32",
        (_TAG_FLOAT, 32): "float32",
        (_TAG_FLOAT, 64): "float64",
    }
    fmt_name = formats.get((tag, bits))
    if fmt_name is None:
        raise WavError(f"WAV: unsupported format tag {tag} with {bits} bits per sample")
    if block_align != channels * bits // 8:
        raise WavError("WAV: block alignment does not match channels and bit depth")
    if len(audio) % block_align:
        raise WavError("WAV: data chunk is not a whole number of frames")
    samples: NDArray[np.float64]
    if fmt_name == "pcm16":
        samples = np.frombuffer(audio, dtype="<i2").astype(np.float64) / 32768.0
    elif fmt_name == "pcm24":
        raw = np.frombuffer(audio, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        value = raw[:, 0] | (raw[:, 1] << 8) | (raw[:, 2] << 16)
        value = np.where(value & 0x800000, value - 0x1000000, value)
        samples = value.astype(np.float64) / 8388608.0
    elif fmt_name == "pcm32":
        samples = np.frombuffer(audio, dtype="<i4").astype(np.float64) / 2147483648.0
    elif fmt_name == "float32":
        samples = np.frombuffer(audio, dtype="<f4").astype(np.float64)
    else:
        samples = np.frombuffer(audio, dtype="<f8").astype(np.float64)
    return WavData(float(rate), channels, fmt_name, samples)


def encode_wav(wav: WavData) -> bytes:
    """Encode to a plain RIFF/WAVE file (format tag 1 or 3)."""
    if not 1 <= wav.channels <= 0xFFFF:
        raise WavError("WAV: channel count must be in 1 .. 65535")
    samples = np.asarray(wav.samples, dtype=np.float64)
    if samples.size % wav.channels:
        raise WavError("WAV: sample count is not a multiple of the channel count")
    rate = wav.sample_rate_hz
    if not rate >= 1.0 or rate != int(rate) or rate > 4294967295.0:
        raise WavError("WAV: sample rate must be a positive integer")
    bits = _BITS[wav.format]
    if wav.format in ("pcm16", "pcm24", "pcm32"):
        scale = float(1 << (bits - 1))
        ints = np.clip(np.round(samples * scale), -scale, scale - 1.0).astype(np.int64)
        if wav.format == "pcm16":
            payload = ints.astype("<i2").tobytes()
        elif wav.format == "pcm32":
            payload = ints.astype("<i4").tobytes()
        else:
            as_bytes = ints.astype("<i4").tobytes()
            payload = np.frombuffer(as_bytes, dtype=np.uint8).reshape(-1, 4)[:, :3].tobytes()
    elif wav.format == "float32":
        payload = samples.astype("<f4").tobytes()
    else:
        payload = samples.astype("<f8").tobytes()
    if len(payload) > 0xFFFFFFFF - 64:
        raise WavError("WAV: data larger than 4 GiB is not supported")
    tag = _TAG_FLOAT if wav.format.startswith("float") else _TAG_PCM
    bytes_per_sample = bits // 8
    block_align = wav.channels * bytes_per_sample
    pad = b"\x00" if len(payload) & 1 else b""
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + len(payload) + len(pad),
        b"WAVE",
        b"fmt ",
        16,
        tag,
        wav.channels,
        int(rate),
        int(rate) * block_align,
        block_align,
        bits,
        b"data",
        len(payload),
    )
    return header + payload + pad


def read_wav(path: str | Path) -> WavData:
    try:
        data = Path(path).read_bytes()
    except OSError as exc:
        raise WavError(f"WAV: cannot open {path}") from exc
    return parse_wav(data)


def write_wav(path: str | Path, wav: WavData) -> None:
    Path(path).write_bytes(encode_wav(wav))
