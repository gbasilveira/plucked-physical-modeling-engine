// SPDX-License-Identifier: MIT
// A4 backend of the oracle rig: the same `Rig` as bridge_rig_scratch.hpp, but the bridge coupling
// is done by the code under test, the A4 `pme::bridge::BridgePoint` and `bridgeForce`
// (engine/src/bridge/include/pme/bridge/). Compiled only with PME_ORACLE_BRIDGE_A4 (CMake
// PME_A4_ORACLE_BACKEND=A4, or AUTO once bridge_point.hpp exists).
//
// UNVERIFIED AGAINST THE MERGED A4 HEADERS. This file was written before A4 was merged, from the
// A4 brief alone (docs/agents/briefs/A4-implementer.md, A4-R1 to A4-R4), not from the
// implementation. The brief fixes the names `pme::compileBridgeTerms`, `pme::bridge::bridgeForce`
// (template on the state type, signature bridgeForce(terms, q, wb)) and the class `BridgePoint`
// with a non-audio `configure` and per-sample steps (segments add forces per polarisation, the
// point advances, accelerations are read back), but not the member names of BridgePoint. Every
// call that depends on a guessed name is marked ADAPT below and sits in this one file; after A4 is
// merged, make those lines match docs/dev/bridge.md and build with -DPME_A4_ORACLE_BACKEND=A4.
// The order of operations (SPEC-BRG "Discrete coupling order") and everything else is fixed here:
//   1. F_s = bridgeForce(terms_s, q_s, w^n) for v and h (the horizontal one with the coefficients
//      of the horizontal polarisation), added in segment order into the point;
//   2. the point advances (rotation R(theta_b) per SI-0019, parallel model, perpendicular
//      conductance, back-rotation) and gives delta_tt w for v and h;
//   3. each segment's modes advance with the modal force -g_n delta_tt w.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "bridge_rig.hpp"
#include "pme/bridge.hpp"
#include "pme/bridge/bridge_point.hpp"  // ADAPT: header name

namespace pme::oracle::brg {

class Rig {
public:
    explicit Rig(const RigSetup& setup) {
        const std::size_t count = setup.segments.size();
        banks_.resize(2 * count);
        terms_.resize(count);
        aH_.resize(count);
        force_.resize(count);
        for (std::size_t s = 0; s < count; ++s) {
            const pme::StringModes& m = setup.segments[s];
            ok_ = ok_ && pme::compileBridgeTerms(m, setup.ends[s], terms_[s]) == pme::Status::Ok;
            banks_[2 * s].resize(m.omega.size());
            banks_[2 * s + 1].resize(m.omega.size());
            force_[s].assign(m.omega.size(), 0.0F);
            for (std::size_t i = 0; i < m.omega.size(); ++i) {
                banks_[2 * s].setMode(i, m.oscV[i]);
                banks_[2 * s + 1].setMode(i, m.oscH[i]);
            }
            // ADAPT: the horizontal coefficient vector, an internal function per the brief
            // (`horizontalForceCoefficients(modes, end, aH)`).
            ok_ = ok_ && pme::bridge::horizontalForceCoefficients(m, setup.ends[s], aH_[s]) ==
                             pme::Status::Ok;
        }
        pme::BridgePointDesc desc;
        desc.thetaB = setup.thetaB;
        desc.conductanceH = setup.conductanceH;
        // ADAPT: configuration of the pluggable parallel-axis model.
        if (setup.model.massSpringDamper) {
            ok_ = ok_ && point_.configure(
                             desc,
                             pme::bridge::MassSpringDamper{setup.model.mass, setup.model.stiffness,
                                                           setup.model.damping},
                             setup.fs) == pme::Status::Ok;
        } else {
            ok_ = ok_ && point_.configure(desc, pme::bridge::Conductance{setup.model.conductance},
                                          setup.fs) == pme::Status::Ok;
        }
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::size_t segmentCount() const { return terms_.size(); }
    [[nodiscard]] std::size_t modeCount(std::size_t s) const { return banks_[2 * s].size(); }
    [[nodiscard]] float* q(std::size_t s) { return banks_[2 * s].q(); }
    [[nodiscard]] float* u(std::size_t s) { return banks_[2 * s].u(); }
    [[nodiscard]] float* qH(std::size_t s) { return banks_[2 * s + 1].q(); }
    [[nodiscard]] float* uH(std::size_t s) { return banks_[2 * s + 1].u(); }
    [[nodiscard]] double bridgeDisplacement() const { return point_.displacementV(); }  // ADAPT

    void step() {
        // 1. forces at step n (ADAPT: member names of BridgePoint)
        point_.beginSample();
        for (std::size_t s = 0; s < terms_.size(); ++s) {
            const float wV = static_cast<float>(point_.displacementV());
            const float wH = static_cast<float>(point_.displacementH());
            const float fV = pme::bridge::bridgeForce<float>(terms_[s], banks_[2 * s].q(), wV);
            // Horizontal: same dot product with the horizontal coefficients, same T/L term.
            float acc = 0.0F;
            const float* qh = banks_[2 * s + 1].q();
            for (std::size_t i = 0; i < aH_[s].size(); ++i) {
                acc += aH_[s][i] * qh[i];
            }
            const float fH = acc - static_cast<float>(terms_[s].tOverL) * wH;
            point_.addForce(fV, fH);
        }
        // 2. advance
        point_.advance();
        // 3. modal forces -g_n delta_tt w
        const double dttV = point_.accelerationV();  // delta_tt w_v^n
        const double dttH = point_.accelerationH();
        for (std::size_t s = 0; s < terms_.size(); ++s) {
            for (std::size_t i = 0; i < terms_[s].g.size(); ++i) {
                force_[s][i] = static_cast<float>(-static_cast<double>(terms_[s].g[i]) * dttV);
            }
            banks_[2 * s].step(std::span<const float>(force_[s]));
            for (std::size_t i = 0; i < terms_[s].g.size(); ++i) {
                force_[s][i] = static_cast<float>(-static_cast<double>(terms_[s].g[i]) * dttH);
            }
            banks_[2 * s + 1].step(std::span<const float>(force_[s]));
        }
    }

private:
    bool ok_{true};
    pme::bridge::BridgePoint point_;
    std::vector<pme::osc::ModeBank> banks_;
    std::vector<pme::SegmentBridgeTerms> terms_;
    std::vector<std::vector<float>> aH_;
    std::vector<std::vector<float>> force_;
};

}  // namespace pme::oracle::brg
