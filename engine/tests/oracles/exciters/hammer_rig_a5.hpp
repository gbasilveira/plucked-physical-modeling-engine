// SPDX-License-Identifier: MIT
// A5 backend of the oracle rig: the same observable interface as ScratchRig (hammer_rig.hpp) but
// the note is advanced by the code under test, the A5 `pme::exciters::StruckNote<S>` and
// `HammerPotential<S>` (engine/src/exciters/include/pme/exciters/hammer*.hpp). Compiled only with
// PME_ORACLE_HAMMER_A5 (CMake PME_A5_ORACLE_BACKEND=A5).
//
// WRITTEN BEFORE A5 WAS MERGED. It follows the A5 implementer brief (docs/agents/briefs/
// A5-implementer.md): StruckNote composes launch, per-string bridge force (A4 bridgeForce),
// BridgePoint addForce/advance, explicit modal forces, savStep and the horizontal polarisation, in
// the SPEC-EXEC order. Every line that depends on the merged names is marked "ADAPT"; the file has
// not been compiled. Procedure: (1) fix the ADAPT lines against the merged headers, (2) configure
// with -DPME_A5_ORACLE_BACKEND=A5, (3) run the "(A5 StruckNote)" cases, (4) switch the default of
// PME_A5_ORACLE_BACKEND in CMakeLists.txt to AUTO.
//
// Observable definitions are those of hammer_rig.hpp. Where A5 exposes a quantity (diagnostics,
// SavSegment accessors) the adapter reads it; the energy functional is NOT taken from A5: it is
// rebuilt here from raw state with the same formula as ScratchRig::energy() (SPEC-OSC discrete
// energy per mode, bridge-point terms of docs/dev/bridge.md, m_H H, psi^2/2 - eps0), so the
// energy oracle stays independent of A5's own monitors.
#pragma once

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "hammer_oracle_common.hpp"
#include "hammer_rig.hpp"
#include "pme/exciters/hammer_note.hpp"  // ADAPT: header of StruckNote / HammerConfig

namespace pme::oracle::ham {

template <class S>
class A5Rig {
public:
    using Scalar = S;

    explicit A5Rig(const NoteSetup& cfg) : cfg_(cfg), dt_(1.0 / cfg.fs) {
        namespace ex = pme::exciters;
        // ADAPT: fill A5's configuration from NoteSetup. Expected members (brief A5-R2, R5, R6):
        // strings (compiled StringModes, bridge end AtL), hammer (HammerParams), una corda
        // (UnaCordaParams + shift), approach sign s_E, bridge point description (BridgePointDesc +
        // ParallelModel from massSpringDamperModel / conductanceModel), SavParams
        // {eps0, driftRelaxation}, shank absent.
        ex::StruckNoteConfig<S> nc;                    // ADAPT: type name
        nc.strings = cfg.strings;                      // ADAPT
        nc.hammer = cfg.hammer;                        // ADAPT
        nc.una = cfg.una;                              // ADAPT
        nc.shift = cfg.shift;                          // ADAPT
        nc.approachSign = cfg.approachSign;            // ADAPT
        nc.fs = cfg.fs;                                // ADAPT
        nc.sav.eps0 = cfg.eps0;                        // ADAPT
        nc.sav.driftRelaxation = cfg.driftRelaxation;  // ADAPT
        nc.bridgeTheta = cfg.thetaB;                   // ADAPT
        nc.bridgeLumped = cfg.bridge.lumped;           // ADAPT: or build the ParallelModel here
        nc.bridgeMass = cfg.bridge.mass;               // ADAPT
        nc.bridgeStiffness = cfg.bridge.stiffness;     // ADAPT
        nc.bridgeDamping = cfg.bridge.damping;         // ADAPT
        ok_ = note_.configure(nc) == pme::Status::Ok;  // ADAPT
        // Strings empty = fixed rigid target (A5's test-only variant, brief A5-R2 last bullet).
        // ADAPT: for cfg.strings.empty() construct that variant instead of StruckNote.
        if (ok_) {
            ok_ = note_.launch(cfg.hammerSpeed) == pme::Status::Ok;  // ADAPT: launch at v_H
        }
    }

    [[nodiscard]] bool ok() const { return ok_; }
    void step() { note_.step(); }  // ADAPT: one sample, SPEC-EXEC order
    // ADAPT all accessors below to the merged names.
    [[nodiscard]] double penetration() const { return note_.maxPenetration(); }
    [[nodiscard]] double contactForce() const {
        return note_.contactForce();
    }  // R7: g_H (psi+psi')/2
    [[nodiscard]] double hammerPosition() const { return note_.hammerPosition(); }
    [[nodiscard]] double hammerSpeed() const { return note_.hammerSpeed(); }  // u_H / dt
    [[nodiscard]] double psi() const { return note_.psi(); }
    [[nodiscard]] double bridgeForceV(std::size_t s) const { return note_.bridgeForceV(s); }
    [[nodiscard]] double bridgeW() const { return note_.bridgeWV(); }
    [[nodiscard]] int resets() const { return static_cast<int>(note_.savCounters().resets); }
    [[nodiscard]] std::size_t strings() const { return cfg_.strings.size(); }

    /// ADAPT: same functional as ScratchRig::energy(). Read, from the StruckNote's SavSegment
    /// (q, qPrev, u, omegaHat2(), mass()), the horizontal SavSegment, and the BridgePoint
    /// (w^n, w^(n+1), mode state and omegaHat2), exactly the quantities ScratchRig keeps; g_n from
    /// bridgeG(modes) (common header, SPEC formula). Not implemented in the skeleton.
    [[nodiscard]] double energy() const;

private:
    NoteSetup cfg_;
    double dt_;
    bool ok_{false};
    pme::exciters::StruckNote<S> note_;  // ADAPT: type name
};

}  // namespace pme::oracle::ham
