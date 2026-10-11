# SPDX-License-Identifier: MIT
"""Signal-analysis tools for validation tests and golden features (P0.3-R3, R4, R5).

Python mirror of the C++ library ``pme_analysis`` (tools/analysis/include/pme/analysis); same
function set, same results within the P0.3-T1 tolerances. NumPy and SciPy only; never used by the
engine. See docs/dev/signal-analysis.md.
"""

from __future__ import annotations

from pme_analysis.golden import (
    ComparisonResult,
    FeatureResult,
    GoldenDecay2,
    GoldenEnergy,
    GoldenEnvelope,
    GoldenError,
    GoldenFeatures,
    GoldenPartial,
    GoldenTolerances,
    PartialTolerance,
    compare_golden,
    format_comparison,
    parse_golden,
    read_golden_file,
    serialize_golden,
    write_golden_file,
)
from pme_analysis.measures import (
    ContactInterval,
    DecayFit,
    DoubleDecayFit,
    EnergyReport,
    InharmonicityFit,
    SpectralEnvelope,
    SpectralEnvelopeOptions,
    account_energy,
    fit_decay,
    fit_double_decay,
    fit_inharmonicity,
    measure_contact,
    spectral_envelope,
    t60_from_sigma,
)
from pme_analysis.partials import (
    PartialEstimate,
    PartialTrackingOptions,
    PartialTrackingResult,
    track_partials,
)
from pme_analysis.wav import WavData, WavError, encode_wav, parse_wav, read_wav, write_wav

__all__ = [
    "ComparisonResult",
    "ContactInterval",
    "DecayFit",
    "DoubleDecayFit",
    "EnergyReport",
    "FeatureResult",
    "GoldenDecay2",
    "GoldenEnergy",
    "GoldenEnvelope",
    "GoldenError",
    "GoldenFeatures",
    "GoldenPartial",
    "GoldenTolerances",
    "InharmonicityFit",
    "PartialEstimate",
    "PartialTolerance",
    "PartialTrackingOptions",
    "PartialTrackingResult",
    "SpectralEnvelope",
    "SpectralEnvelopeOptions",
    "WavData",
    "WavError",
    "account_energy",
    "compare_golden",
    "encode_wav",
    "fit_decay",
    "fit_double_decay",
    "fit_inharmonicity",
    "format_comparison",
    "measure_contact",
    "parse_golden",
    "parse_wav",
    "read_golden_file",
    "read_wav",
    "serialize_golden",
    "spectral_envelope",
    "t60_from_sigma",
    "track_partials",
    "write_golden_file",
    "write_wav",
]
