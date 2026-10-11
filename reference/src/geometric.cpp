// SPDX-License-Identifier: MIT
#include "pme/ref/geometric.hpp"

#include "pme/ref/common.hpp"

namespace pme::ref {

Results simulate_geometric(const Scenario& /*scenario*/, const SimulationOptions& /*options*/) {
    throw RefError("geometric nonlinearity is not implemented yet");
}

}  // namespace pme::ref
