// SPDX-License-Identifier: MIT
// Geometric nonlinearity of a single fixed-end string (P0.5-R6, SPEC-GEO): Kirchhoff-Carrier
// tension modulation and longitudinal waves. Internal to the simulation; use simulate().
#pragma once

#include "pme/ref/simulation.hpp"

namespace pme::ref {

Results simulate_geometric(const Scenario& scenario, const SimulationOptions& options);

}  // namespace pme::ref
