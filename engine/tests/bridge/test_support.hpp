// SPDX-License-Identifier: MIT
// Shared fixtures for the A4 tests: plain and muted strings, a rig that runs segments on a bridge
// point in the SPEC-BRG coupling order, and the partitioned scheme's conserved energy.
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "pme/body.hpp"
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

/// Three segments (two speaking, one rear) on a bridge point with a 16-mode modal set, the
/// configuration of the RT-safety test and of the benchmark. Returns `Ok` on success.
template <class S>
Status buildThreeSegmentRig(Rig<S>& rig, double fs = kFs) {
    StringParams pa = plainSteel(0.65, 262.0, false);
    StringParams pb = plainSteel(0.65, 262.0 * 1.001, false);
    StringParams pr = plainSteel(0.15, 400.0, false);
    pr.bridgeEnd = BridgeEnd::At0;
    const StringModes a = compile(pa, 16000.0, fs);
    const StringModes b = compile(pb, 16000.0, fs);
    const StringModes r = compile(pr, 16000.0, fs);
    for (const auto& [m, end] : {std::pair{&a, BridgeEnd::AtL}, std::pair{&b, BridgeEnd::AtL},
                                 std::pair{&r, BridgeEnd::At0}}) {
        if (const Status s = rig.addSegment(*m, end); s != Status::Ok) {
            return s;
        }
    }
    LocalBridge local;
    for (int k = 0; k < 16; ++k) {
        local.omega.push_back(2.0 * 3.14159265358979323846 * (120.0 + 230.0 * k));
        local.sigma.push_back(15.0 + 3.0 * k);
        local.phi.push_back((k % 2 == 0 ? 1.0 : -1.0) / (1.0 + 0.2 * k));
        local.modeIndex.push_back(k);
    }
    local.conductanceHf = 1.0e-4;
    bridge::ParallelModel model;
    if (const Status s = bridge::modalSetModel(local, model); s != Status::Ok) {
        return s;
    }
    BridgePointDesc d;
    d.thetaB = 0.1;
    d.conductanceH = 1.0e-4;
    if (const Status s = rig.point.configure(d, model, fs); s != Status::Ok) {
        return s;
    }
    for (auto& seg : rig.segs) {
        for (std::size_t i = 0; i < seg.modeCount(); ++i) {
            seg.qV()[i] = static_cast<S>(1.0e-5 / static_cast<double>((i + 1) * (i + 1)));
            seg.qH()[i] = static_cast<S>(0.5e-5 / static_cast<double>((i + 1) * (i + 1)));
        }
    }
    return Status::Ok;
}

}  // namespace pme::bridge_test
