// SPDX-License-Identifier: MIT
#pragma once
#include "report_parameters.hpp"
#include "pme/exciters.hpp"
#include "pme/string.hpp"

namespace piano_mvp_v2 {
struct NoteParameters {
    ReportRow source;
    pme::StringParams string;
    pme::HammerParams hammer;
    bool extrapolated{};
};
// Immutable report data plus the explicitly documented Euler–Bernoulli adaptation.
// Non-audio thread only. Throws for a key outside A0–C8.
NoteParameters parametersForKey(int key);
pme::Status compilePresetString(const NoteParameters& p, double fs, pme::StringModes& modes);
}
