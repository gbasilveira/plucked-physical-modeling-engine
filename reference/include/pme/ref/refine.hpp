// SPDX-License-Identifier: MIT
// Grid-refinement tool (P0.5-R7): runs a scenario on a sequence of grids with the time step
// refined in proportion to the grid spacing, measures the partial frequencies of the first
// string displacement observation and reports the observed convergence orders.
#pragma once

#include <string>
#include <vector>

#include "pme/ref/scenario.hpp"

namespace pme::ref {

struct RefinementLevel {
    int intervals = 0;                   // grid intervals of string `string_index`
    int oversampling = 1;                // time step refined in proportion
    std::vector<double> frequencies_hz;  // measured partials 1 .. K
};

struct RefinementReport {
    std::vector<RefinementLevel> levels;
    double grid_ratio = 0.0;  // N_(i+1) / N_i (constant)
    // From the three finest levels, per partial:
    std::vector<double> observed_order;   // log(|f1 - f2| / |f2 - f3|) / log(ratio)
    std::vector<double> extrapolated_hz;  // Richardson extrapolation to h -> 0
    std::vector<double> analytic_hz;      // SPEC-STR stiff-string values (pinned ends), if known
};

// Requirements: at least three grids with a constant integer ratio, and the first observation of
// the scenario must be a displacement or velocity of string `string_index`. The oversampling of
// level i is the scenario's value times intervals_i / intervals_0, so the grids' ratios must also
// divide the oversampling. `partials` frequencies are measured starting from the stiff-string
// values of SPEC-STR "Modes". Throws RefError.
RefinementReport refine_partial_frequencies(const Scenario& scenario, const std::vector<int>& grids,
                                            int partials, std::size_t string_index = 0);

// JSON text of a report.
std::string refinement_to_json(const RefinementReport& report);

}  // namespace pme::ref
