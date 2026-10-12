// SPDX-License-Identifier: MIT
// Kirchhoff-Carrier tension-modulation potential: configuration (A7-R1). SPEC-GEO.
#include "pme/nonlinear/tension.hpp"

#include <cmath>

namespace pme::nl {

template <class S>
Status TensionModulationPotential<S>::configure(std::span<const double> k2, double stiffness) {
    if (k2.empty() || !std::isfinite(stiffness) || !(stiffness > 0.0)) {
        return Status::InvalidArgument;
    }
    for (const double v : k2) {
        if (!std::isfinite(v) || v < 0.0) {
            return Status::InvalidArgument;
        }
    }
    k2_.assign(k2.begin(), k2.end());
    c_ = stiffness;
    return Status::Ok;
}

template class TensionModulationPotential<float>;
template class TensionModulationPotential<double>;

}  // namespace pme::nl
