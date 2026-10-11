// SPDX-License-Identifier: MIT
// Shared fixtures for the A4 tests: plain and muted strings, a rig that runs segments on a bridge
// point in the SPEC-BRG coupling order, and the partitioned scheme's conserved energy.
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "pme/bridge.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "pme/bridge/modal_segment.hpp"
#include "pme/string.hpp"

namespace pme::bridge_test {

inline constexpr double kFs = 48000.0;

/// Plain steel string d = 1 mm; `lossless` sets b1 = b2 = b3 = 0.
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
    const Status s = compileString(p, fs, fMax, {}, m);
    if (s != Status::Ok) {
        return StringModes{};
    }
    return m;
}

/// Segments plus one bridge point, stepped in the SPEC-BRG order.
template <class S>
struct Rig {
    bridge::BridgePoint<S> point;
    std::vector<bridge::ModalSegment<S>> segs;

    Status addSegment(const StringModes& m, BridgeEnd end) {
        bridge::ModalSegment<S> s;
        const Status st = s.configure(m, end);
        if (st == Status::Ok) {
            segs.push_back(std::move(s));
        }
        return st;
    }

    /// One sample: forces in segment order, advance, modal forces (SPEC-BRG "Discrete coupling
    /// order" steps 1 to 3).
    void step() noexcept {
        const S wv = point.wV();
        const S wh = point.wH();
        for (const auto& s : segs) {
            point.addForce(s.forceV(wv), s.forceH(wh));
        }
        point.advance();
        for (auto& s : segs) {
            s.advance(point.accelV(), point.accelH());
        }
    }

    /// Total energy of the partitioned scheme at n + 1/2 (docs/dev/bridge.md).
    [[nodiscard]] double energy() const noexcept {
        double e = point.modeEnergy();
        for (const auto& s : segs) {
            e += s.energy(static_cast<double>(point.wV()), static_cast<double>(point.incrementV()),
                          static_cast<double>(point.wH()), static_cast<double>(point.incrementH()));
        }
        return e;
    }
};

}  // namespace pme::bridge_test
