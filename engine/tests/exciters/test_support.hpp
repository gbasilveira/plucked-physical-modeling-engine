// SPDX-License-Identifier: MIT
// Shared fixtures of the A5 tests: strings, hammers and a note builder.
#pragma once

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "pme/bridge.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "pme/exciters.hpp"
#include "pme/exciters/hammer.hpp"
#include "pme/string.hpp"

namespace pme::exciters_test {

inline constexpr double kFs = 48000.0;

/// Scientific-notation text of a number for the evidence lines the tests print.
inline std::string sci(double v, int precision = 3) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(precision) << v;
    return os.str();
}

/// Plain steel string d = 1 mm; `lossless` sets the intrinsic losses to zero.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): L, f1
inline StringParams plainSteel(double L, double f1, bool lossless) {
    StringParams p;
    p.L = L;
    p.dCore = 1.0e-3;
    p.dOuter = 1.0e-3;
    p.rhoCore = 7850.0;
    p.ECore = 200.0e9;
    p.f1Target = f1;
    if (!lossless) {
        p.b1 = 0.4;
        p.b2 = 1.0e-3;
        p.b3 = 1.0e-11;
    }
    return p;
}

inline StringModes compile(const StringParams& p, double fMax = 14000.0, double fs = kFs) {
    StringModes m;
    if (compileString(p, fs, fMax, {}, m) != Status::Ok) {
        return StringModes{};
    }
    return m;
}

/// A mezzo-forte piano hammer (SPEC-HAM typical values).
inline HammerParams pianoHammer() {
    HammerParams h;
    h.mass = 8.0e-3;
    h.K = 4.5e9;
    h.p = 2.5;
    h.muHC = 0.5;
    h.width = 0.01;
    h.strikeRatio = 1.0 / 8.0;
    return h;
}

/// `n` unison strings of a C4-like note (L = 0.65 m, slightly detuned), compiled at `fs`.
inline std::vector<StringModes> unison(std::size_t n, bool lossless, double fs = kFs,
                                       double fMax = 14000.0) {
    std::vector<StringModes> out;
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(compile(
            plainSteel(0.65, 262.0 * (1.0 + 4.0e-4 * static_cast<double>(i)), lossless), fMax,
            fs));
    }
    return out;
}

/// Description of a struck note over `modes` (pointers into `modes`, which must outlive it).
inline exciters::StruckNoteDesc describe(const std::vector<StringModes>& modes,
                                         const HammerParams& hammer) {
    exciters::StruckNoteDesc d;
    for (const StringModes& m : modes) {
        d.strings.push_back(&m);
    }
    d.hammer = hammer;
    return d;
}

/// A mass-spring-damper bridge point (the RT test and the benchmark).
template <class S>
Status configureBridge(bridge::BridgePoint<S>& point, double fs = kFs) {
    bridge::ParallelModel model;
    if (const Status s = bridge::massSpringDamperModel(2.0, 4.0e7, 40.0, model); s != Status::Ok) {
        return s;
    }
    BridgePointDesc d;
    d.thetaB = 0.1;
    d.conductanceH = 1.0e-4;
    return point.configure(d, model, fs);
}

}  // namespace pme::exciters_test
