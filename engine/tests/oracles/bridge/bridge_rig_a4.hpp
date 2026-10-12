// SPDX-License-Identifier: MIT
// A4 backend of the oracle rig: the same `Rig` as bridge_rig_scratch.hpp, but the bridge coupling
// is done by the code under test, the A4 `pme::bridge::BridgePoint` and `bridgeForce`
// (engine/src/bridge/include/pme/bridge/). Compiled only with PME_ORACLE_BRIDGE_A4 (CMake
// PME_A4_ORACLE_BACKEND=A4, or AUTO once bridge_point.hpp exists).
//
// Written against A4's public headers only (engine/src/bridge/include/pme/bridge/{terms,model,
// bridge_point}.hpp and docs/dev/bridge.md), never its .cpp files. Mapping:
//   * compileBridgeTerms (public)                       -> g_n, a_n, T/L (vertical)
//   * pme::bridge::horizontalForceCoefficients          -> a_h,n (oscH omegaHat2)
//   * pme::bridge::bridgeForce<float,float>(span, T/L, q, w)  -> F_b,s, per polarisation
//   * pme::bridge::BridgePoint<float>: configure(desc, ParallelModel, fs), addForce(fv, fh),
//     advance(), wV()/wH() (= w^n before advance), accelV()/accelH() (= delta_tt w^n after).
// The order of operations (SPEC-BRG "Discrete coupling order") is fixed here:
//   1. F_s = bridgeForce(...) for v and h at (q^n, w^n), added in segment order into the point;
//   2. the point advances (R(theta_b) of SI-0019, parallel model, perpendicular conductance);
//   3. each segment's modes advance with the modal force -g_n delta_tt w.
#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "bridge_rig.hpp"
#include "pme/bridge.hpp"
#include "pme/bridge/bridge_point.hpp"
#include "pme/bridge/model.hpp"
#include "pme/bridge/terms.hpp"

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
            ok_ = ok_ && pme::bridge::horizontalForceCoefficients(m, setup.ends[s], aH_[s]) ==
                             pme::Status::Ok;
        }
        pme::BridgePointDesc desc;
        desc.thetaB = setup.thetaB;
        desc.conductanceH = setup.conductanceH;
        pme::bridge::ParallelModel model;
        const pme::Status ms =
            setup.model.massSpringDamper
                ? pme::bridge::massSpringDamperModel(setup.model.mass, setup.model.stiffness,
                                                     setup.model.damping, model)
                : pme::bridge::conductanceModel(setup.model.conductance, model);
        ok_ = ok_ && ms == pme::Status::Ok &&
              point_.configure(desc, model, setup.fs) == pme::Status::Ok;
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::size_t segmentCount() const { return terms_.size(); }
    [[nodiscard]] std::size_t modeCount(std::size_t s) const { return banks_[2 * s].size(); }
    [[nodiscard]] float* q(std::size_t s) { return banks_[2 * s].q(); }
    [[nodiscard]] float* u(std::size_t s) { return banks_[2 * s].u(); }
    [[nodiscard]] float* qH(std::size_t s) { return banks_[2 * s + 1].q(); }
    [[nodiscard]] float* uH(std::size_t s) { return banks_[2 * s + 1].u(); }
    [[nodiscard]] double bridgeDisplacement() const { return point_.wV(); }

    void step() {
        // 1. forces at step n
        const float wV = point_.wV();
        const float wH = point_.wH();
        for (std::size_t s = 0; s < terms_.size(); ++s) {
            const auto fV = pme::bridge::bridgeForce<float>(terms_[s], banks_[2 * s].q(), wV);
            const auto fH = pme::bridge::bridgeForce<float, float>(
                std::span<const float>(aH_[s]), terms_[s].tOverL, banks_[2 * s + 1].q(), wH);
            point_.addForce(fV, fH);
        }
        // 2. advance
        point_.advance();
        // 3. modal forces -g_n delta_tt w
        const double dttV = point_.accelV();
        const double dttH = point_.accelH();
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
    pme::bridge::BridgePoint<float> point_;
    std::vector<pme::osc::ModeBank> banks_;
    std::vector<pme::SegmentBridgeTerms> terms_;
    std::vector<std::vector<float>> aH_;
    std::vector<std::vector<float>> force_;
};

}  // namespace pme::oracle::brg
