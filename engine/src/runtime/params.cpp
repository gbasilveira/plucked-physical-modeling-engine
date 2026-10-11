// SPDX-License-Identifier: MIT
// Parameter registry (P0.4-R4). Ids are stable and never reused; see docs/dev/interfaces.md.
// The table must stay sorted by id (checked at compile time below).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

#include "pme/params.hpp"

namespace pme {
namespace {

constexpr ParamClass Cont = ParamClass::Continuous;
constexpr ParamClass Struc = ParamClass::Structural;

// clang-format off
constexpr ParamInfo kRegistry[] = {
    // 1000: performance controls (continuous)
    {1001, "pedal.sustain", "1", 0.0, 1.0, 0.0, Cont, "SPEC-API Events; SPEC-DMP Lift fraction",
     "Sustain pedal depth P, 0 up to 1 fully down."},
    {1002, "pedal.sostenuto", "1", 0.0, 1.0, 0.0, Cont, "SPEC-API Events; SPEC-DMP Sostenuto",
     "Sostenuto pedal depth, 0 up to 1 fully down."},
    {1003, "pedal.soft", "1", 0.0, 1.0, 0.0, Cont, "SPEC-API Events; SPEC-HAM Una corda",
     "Soft pedal depth: una corda shift s on a grand, hammer-rail position on an upright."},
    {1010, "unaCorda.shift", "1", 0.0, 1.0, 0.0, Cont, "SPEC-HAM Una corda (grand)",
     "Una corda shift s in [0, 1], continuous."},
    // 2000: output (continuous)
    {2001, "output.gain", "dB", -96.0, 24.0, 0.0, Cont, "SPEC-API Parameter classes",
     "Master output gain in decibels (not linear). Range provisional."},
    // 3000: sympathetic path
    {3001, "sympathetic.gain", "1", 0.0, 1.0, 1.0, Cont, "SPEC-BODY Sympathetic path",
     "Sympathetic coupling gain kappa_s (linear), default 1."},
    {3101, "sympathetic.lowPass", "Hz", 500.0, 20000.0, 5000.0, Struc, "SPEC-BODY Sympathetic path; SPEC-INST Piano defaults",
     "Second-order low-pass frequency f_symp of the sympathetic path. Range provisional."},
    // 4000: dampers
    {4001, "damper.tauUp", "s", 0.001, 0.05, 0.004, Cont, "SPEC-DMP Timing",
     "Damper lift time constant tau_up. Range provisional."},
    {4002, "damper.tau0", "s", 0.002, 0.1, 0.012, Cont, "SPEC-DMP Timing",
     "Damper landing time constant tau_0 at zero release velocity. Range provisional."},
    {4003, "damper.kRelease", "1", 0.0, 10.0, 2.0, Cont, "SPEC-DMP Timing",
     "Release-velocity factor k_r of the landing time. Range provisional."},
    {4101, "damper.halfPedalStart", "1", 0.0, 1.0, 0.35, Struc, "SPEC-DMP Lift fraction; SPEC-INST Piano defaults",
     "Half-pedal region start P_0 (must stay below P_1). Class provisional."},
    {4102, "damper.halfPedalEnd", "1", 0.0, 1.0, 0.65, Struc, "SPEC-DMP Lift fraction; SPEC-INST Piano defaults",
     "Half-pedal region end P_1 (must stay above P_0). Class provisional."},
    {4103, "damper.eta", "1", 0.5, 8.0, 2.0, Struc, "SPEC-DMP Lift fraction; SPEC-INST Piano defaults",
     "Progressive felt contact exponent eta_d. Range and class provisional."},
    // 5000: una corda felt map
    {5001, "unaCorda.s0", "1", 0.0, 1.0, 0.3, Struc, "SPEC-HAM Una corda; SPEC-INST Piano defaults",
     "Smoothstep lower bound s_0."},
    {5002, "unaCorda.s1", "1", 0.0, 1.0, 0.9, Struc, "SPEC-HAM Una corda; SPEC-INST Piano defaults",
     "Smoothstep upper bound s_1."},
    {5003, "unaCorda.kappaUC", "1", 0.0, 1.0, 0.6, Struc, "SPEC-HAM Una corda; SPEC-INST Piano defaults",
     "Felt stiffness factor kappa_uc. Range provisional."},
    {5004, "unaCorda.deltaP", "1", -2.0, 2.0, 0.0, Struc, "SPEC-HAM Una corda; SPEC-INST Piano defaults",
     "Felt exponent shift Delta p_uc. Range provisional."},
    // 6000: strings and geometry
    {6001, "longitudinal.kappaL", "1", 0.0, 0.2, 0.026, Struc, "SPEC-GEO Transmission to the body; SPEC-INST Piano defaults",
     "Longitudinal transmission kappa_L (about sin 1.5 degrees). Range provisional."},
    {6002, "longitudinal.modes", "1", 0.0, 32.0, 8.0, Struc, "SPEC-GEO Longitudinal modes; SPEC-INST Piano defaults",
     "Number of longitudinal modes M_L per string. Range provisional."},
    {6003, "strings.fMax", "Hz", 12000.0, 20000.0, 16000.0, Struc, "SPEC-INST Piano defaults",
     "Highest kept string frequency f_max. Range from SPEC (12 to 20 kHz); default provisional."},
    // 7000: body
    {7001, "body.localBridgeModes", "1", 1.0, 64.0, 64.0, Struc, "SPEC-BODY Local bridge model; SPEC-INST Piano defaults",
     "Private body modes M_b per bridge point (64 SHOULD hold)."},
};
// clang-format on

constexpr bool isSortedById() {
    for (std::size_t i = 1; i < std::size(kRegistry); ++i) {
        if (kRegistry[i - 1].id >= kRegistry[i].id) {
            return false;
        }
    }
    return true;
}
static_assert(isSortedById(), "kRegistry must be sorted by strictly ascending id");

}  // namespace

std::span<const ParamInfo> paramRegistry() noexcept { return {kRegistry, std::size(kRegistry)}; }

const ParamInfo* findParam(ParamId id) noexcept {
    const auto it = std::lower_bound(std::begin(kRegistry), std::end(kRegistry), id,
                                     [](const ParamInfo& p, ParamId v) { return p.id < v; });
    if (it != std::end(kRegistry) && it->id == id) {
        return &*it;
    }
    return nullptr;
}

}  // namespace pme
