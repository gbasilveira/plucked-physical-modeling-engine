// SPDX-License-Identifier: MIT
#include "pme/ref/refine.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

#include "pme/analysis/partials.hpp"
#include "pme/ref/common.hpp"
#include "pme/ref/simulation.hpp"

namespace pme::ref {

RefinementReport refine_partial_frequencies(const Scenario& scenario, const std::vector<int>& grids,
                                            int partials, std::size_t string_index) {
    if (grids.size() < 3) {
        throw RefError("refine: at least three grids are required");
    }
    if (partials < 1) {
        throw RefError("refine: partials must be at least 1");
    }
    if (string_index >= scenario.strings.size()) {
        throw RefError("refine: no such string");
    }
    if (scenario.observations.empty() ||
        (scenario.observations[0].kind != ObservationKind::StringDisplacement &&
         scenario.observations[0].kind != ObservationKind::StringVelocity)) {
        throw RefError("refine: the first observation must be a string displacement or velocity");
    }
    const double ratio = static_cast<double>(grids[1]) / grids[0];
    for (std::size_t i = 1; i < grids.size(); ++i) {
        if (grids[i] <= grids[i - 1] || grids[i] % grids[i - 1] != 0 ||
            static_cast<double>(grids[i]) / grids[i - 1] != ratio) {
            throw RefError(
                "refine: grids must increase with a constant integer ratio (for example "
                "64,128,256)");
        }
    }
    RefinementReport rep;
    rep.grid_ratio = ratio;
    const StringPhysics ph = derive_string(scenario.strings[string_index].params);
    for (int n = 1; n <= partials; ++n) {
        rep.analytic_hz.push_back(stiff_string_frequency_hz(ph, n));
    }
    for (const int g : grids) {
        Scenario sc = scenario;
        sc.strings[string_index].intervals = g;
        // Delta t is refined with the grid spacing so that both error terms scale as h^2.
        const long os = static_cast<long>(scenario.oversampling) * g / grids[0];
        if (os * grids[0] != static_cast<long>(scenario.oversampling) * g) {
            throw RefError("refine: the grid ratio must divide the oversampling");
        }
        sc.oversampling = static_cast<int>(os);
        SimulationOptions so;
        so.record_energy = false;
        const Results r = simulate(sc, so);
        pme::analysis::PartialTrackingOptions opt;
        opt.approx_frequencies_hz = rep.analytic_hz;
        opt.envelope_frame_s = 0.0;
        const auto fit = pme::analysis::track_partials(r.signals[0].data, r.sim_rate, opt);
        RefinementLevel lv;
        lv.intervals = g;
        lv.oversampling = sc.oversampling;
        for (const auto& p : fit.partials) {
            lv.frequencies_hz.push_back(p.frequency_hz);
        }
        rep.levels.push_back(std::move(lv));
    }
    const std::size_t m = rep.levels.size();
    for (int n = 0; n < partials; ++n) {
        const auto i = static_cast<std::size_t>(n);
        const double f1 = rep.levels[m - 3].frequencies_hz[i];
        const double f2 = rep.levels[m - 2].frequencies_hz[i];
        const double f3 = rep.levels[m - 1].frequencies_hz[i];
        const double d12 = std::fabs(f1 - f2);
        const double d23 = std::fabs(f2 - f3);
        const double order = d23 > 0.0 && d12 > 0.0 ? std::log(d12 / d23) / std::log(ratio) : 0.0;
        rep.observed_order.push_back(order);
        rep.extrapolated_hz.push_back(order > 0.0 ? f3 + (f3 - f2) / (std::pow(ratio, order) - 1.0)
                                                  : f3);
    }
    return rep;
}

std::string refinement_to_json(const RefinementReport& r) {
    using nlohmann::ordered_json;
    ordered_json j;
    j["schema_version"] = 1;
    j["quantity"] = "partial-frequencies";
    j["grid_ratio"] = r.grid_ratio;
    ordered_json levels = ordered_json::array();
    for (const auto& l : r.levels) {
        levels.push_back({{"intervals", l.intervals},
                          {"oversampling", l.oversampling},
                          {"frequencies_hz", l.frequencies_hz}});
    }
    j["levels"] = levels;
    j["observed_order"] = r.observed_order;
    j["extrapolated_hz"] = r.extrapolated_hz;
    j["analytic_hz"] = r.analytic_hz;
    return j.dump(1);
}

}  // namespace pme::ref
